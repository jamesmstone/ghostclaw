#include "ghostclaw/providers/anthropic.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/providers/traits.hpp"

#include <sstream>

namespace ghostclaw::providers {

namespace {

common::Status validate_anthropic_status(const HttpResponse &response) {
  if (response.timeout) {
    return common::Status::error(
        ProviderError{.code = ProviderErrorCode::Timeout, .message = "request timed out"}.to_string());
  }
  if (response.network_error) {
    return common::Status::error(
        ProviderError{.code = ProviderErrorCode::NetworkError,
                      .message = response.network_error_message}
            .to_string());
  }
  if (response.status == 401 || response.status == 403) {
    return common::Status::error(
        ProviderError{.code = ProviderErrorCode::AuthError,
                      .status = response.status,
                      .message = response.body}
            .to_string());
  }
  if (response.status == 429) {
    return common::Status::error(
        ProviderError{.code = ProviderErrorCode::RateLimitError,
                      .status = response.status,
                      .message = response.body}
            .to_string());
  }
  if (response.status < 200 || response.status >= 300) {
    return common::Status::error(
        ProviderError{.code = ProviderErrorCode::ApiError,
                      .status = response.status,
                      .message = response.body}
            .to_string());
  }
  return common::Status::success();
}

bool is_sse_response(const HttpResponse &response) {
  const auto it = response.headers.find("content-type");
  if (it != response.headers.end() &&
      common::to_lower(it->second).find("text/event-stream") != std::string::npos) {
    return true;
  }
  return response.body.find("data:") != std::string::npos;
}

std::string build_anthropic_body(const std::optional<std::string> &system_prompt,
                                 const std::string &message, const std::string &model,
                                 const double temperature, const bool stream) {
  std::ostringstream body;
  body << "{";
  body << "\"model\":\"" << json_escape(model) << "\",";
  body << "\"max_tokens\":4096,";
  if (system_prompt.has_value()) {
    body << "\"system\":\"" << json_escape(*system_prompt) << "\",";
  }
  body << "\"messages\":[{\"role\":\"user\",\"content\":\"" << json_escape(message) << "\"}],";
  body << "\"temperature\":" << temperature << ",";
  body << "\"stream\":" << (stream ? "true" : "false");
  body << "}";
  return body.str();
}

void parse_sse_bytes(const std::string_view chunk, std::string &line_buffer, std::string &event_data,
                     const std::function<void(const std::string &)> &on_event_data) {
  line_buffer.append(chunk);
  std::size_t line_end = std::string::npos;
  while ((line_end = line_buffer.find('\n')) != std::string::npos) {
    std::string line = line_buffer.substr(0, line_end);
    line_buffer.erase(0, line_end + 1);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.empty()) {
      if (!event_data.empty()) {
        on_event_data(event_data);
        event_data.clear();
      }
      continue;
    }

    if (!common::starts_with(line, "data:")) {
      continue;
    }

    std::string payload = line.substr(5);
    if (!payload.empty() && payload.front() == ' ') {
      payload.erase(payload.begin());
    }
    if (!event_data.empty()) {
      event_data.push_back('\n');
    }
    event_data += payload;
  }
}

} // namespace

AnthropicProvider::AnthropicProvider(std::string api_key, std::shared_ptr<HttpClient> http_client)
    : AnthropicProvider("anthropic", std::move(api_key), "https://api.anthropic.com",
                        std::move(http_client), false, {}) {}

AnthropicProvider::AnthropicProvider(std::string name, std::string api_key, std::string base_url,
                                     std::shared_ptr<HttpClient> http_client,
                                     const bool use_bearer_auth,
                                     std::unordered_map<std::string, std::string> extra_headers)
    : name_(std::move(name)), api_key_(std::move(api_key)), base_url_(std::move(base_url)),
      http_client_(std::move(http_client)), use_bearer_auth_(use_bearer_auth),
      extra_headers_(std::move(extra_headers)) {
  while (!base_url_.empty() && base_url_.back() == '/') {
    base_url_.pop_back();
  }
}

common::Result<std::string> AnthropicProvider::chat(const std::string &message,
                                                    const std::string &model,
                                                    const double temperature) {
  return chat_with_system(std::nullopt, message, model, temperature);
}

common::Result<std::string>
AnthropicProvider::chat_with_system(const std::optional<std::string> &system_prompt,
                                    const std::string &message, const std::string &model,
                                    const double temperature) {
  if (api_key_.empty()) {
    return common::Result<std::string>::failure(
        ProviderError{.code = ProviderErrorCode::AuthError, .message = "missing API key"}.to_string());
  }

  const auto headers = build_headers(false);

  const auto response = http_client_->post_json(
      messages_url(), headers, build_anthropic_body(system_prompt, message, model, temperature, false),
      30'000);

  auto status = validate_anthropic_status(response);
  if (!status.ok()) {
    return common::Result<std::string>::failure(status.error());
  }

  if (is_sse_response(response)) {
    const auto parsed = parse_anthropic_sse_content(response.body);
    if (!parsed.ok()) {
      return common::Result<std::string>::failure(
          ProviderError{.code = ProviderErrorCode::InvalidResponse, .message = parsed.error()}
              .to_string());
    }
    return parsed;
  }

  const auto parsed = parse_anthropic_content(response.body);
  if (!parsed.ok()) {
    return common::Result<std::string>::failure(
        ProviderError{.code = ProviderErrorCode::InvalidResponse, .message = parsed.error()}.to_string());
  }

  return parsed;
}

common::Result<std::string>
AnthropicProvider::chat_with_system_stream(const std::optional<std::string> &system_prompt,
                                           const std::string &message, const std::string &model,
                                           const double temperature,
                                           const StreamChunkCallback &on_chunk) {
  if (api_key_.empty()) {
    return common::Result<std::string>::failure(
        ProviderError{.code = ProviderErrorCode::AuthError, .message = "missing API key"}.to_string());
  }

  const auto headers = build_headers(true);

  std::string aggregated;
  std::string line_buffer;
  std::string event_data;
  const auto stream_handler = [&](const std::string_view bytes) {
    parse_sse_bytes(bytes, line_buffer, event_data, [&](const std::string &event) {
      auto delta = parse_anthropic_sse_event_delta(event);
      if (!delta.ok() || delta.value().empty()) {
        return;
      }
      aggregated += delta.value();
      if (on_chunk) {
        on_chunk(delta.value());
      }
    });
  };

  const auto response = http_client_->post_json_stream(
      messages_url(), headers, build_anthropic_body(system_prompt, message, model, temperature, true),
      30'000, stream_handler);
  stream_handler("\n\n");

  auto status = validate_anthropic_status(response);
  if (!status.ok()) {
    return common::Result<std::string>::failure(status.error());
  }

  if (is_sse_response(response)) {
    if (!aggregated.empty()) {
      return common::Result<std::string>::success(aggregated);
    }
    const auto parsed = parse_anthropic_sse_content(response.body);
    if (!parsed.ok()) {
      return common::Result<std::string>::failure(
          ProviderError{.code = ProviderErrorCode::InvalidResponse, .message = parsed.error()}
              .to_string());
    }
    return parsed;
  }

  const auto parsed = parse_anthropic_content(response.body);
  if (!parsed.ok()) {
    return common::Result<std::string>::failure(
        ProviderError{.code = ProviderErrorCode::InvalidResponse, .message = parsed.error()}.to_string());
  }
  if (on_chunk) {
    std::istringstream stream(parsed.value());
    std::string token;
    bool emitted = false;
    while (stream >> token) {
      on_chunk(token);
      emitted = true;
    }
    if (!emitted && !parsed.value().empty()) {
      on_chunk(parsed.value());
    }
  }
  return parsed;
}

common::Status AnthropicProvider::warmup() {
  const auto response = http_client_->head(base_url_, {}, 5'000);
  if (response.network_error || response.timeout) {
    return common::Status::success();
  }
  return common::Status::success();
}

std::string AnthropicProvider::name() const { return name_; }

std::unordered_map<std::string, std::string> AnthropicProvider::build_headers(const bool stream) const {
  std::unordered_map<std::string, std::string> headers = {
      {"Content-Type", "application/json"},
      {"anthropic-version", "2023-06-01"},
  };
  if (stream) {
    headers["Accept"] = "text/event-stream";
  }

  if (use_bearer_auth_) {
    headers["Authorization"] = "Bearer " + api_key_;
  } else {
    headers["x-api-key"] = api_key_;
  }

  for (const auto &[key, value] : extra_headers_) {
    headers[key] = value;
  }
  return headers;
}

std::string AnthropicProvider::messages_url() const { return base_url_ + "/v1/messages"; }

} // namespace ghostclaw::providers
