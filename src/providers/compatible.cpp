#include "ghostclaw/providers/compatible.hpp"

#include "ghostclaw/common/fs.hpp"

#include <sstream>

namespace ghostclaw::providers {

namespace {

common::Result<std::string> provider_error_result(const ProviderError &error) {
  return common::Result<std::string>::failure(error.to_string());
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

CompatibleProvider::CompatibleProvider(std::string name, std::string base_url, std::string api_key,
                                       std::shared_ptr<HttpClient> http_client,
                                       const bool require_api_key,
                                       std::unordered_map<std::string, std::string> extra_headers)
    : name_(std::move(name)), base_url_(std::move(base_url)), api_key_(std::move(api_key)),
      http_client_(std::move(http_client)), require_api_key_(require_api_key),
      extra_headers_(std::move(extra_headers)) {
  while (!base_url_.empty() && base_url_.back() == '/') {
    base_url_.pop_back();
  }
}

common::Result<std::string> CompatibleProvider::chat(const std::string &message,
                                                     const std::string &model,
                                                     const double temperature) {
  return chat_with_system(std::nullopt, message, model, temperature);
}

std::string CompatibleProvider::build_body(const std::optional<std::string> &system_prompt,
                                           const std::string &message, const std::string &model,
                                           const double temperature,
                                           const std::vector<tools::ToolSpec> &tools,
                                           const bool stream) const {
  std::ostringstream body;
  body << "{";
  body << "\"model\":\"" << json_escape(model) << "\",";
  body << "\"messages\":[";
  bool first = true;
  if (system_prompt.has_value()) {
    body << "{\"role\":\"system\",\"content\":\"" << json_escape(*system_prompt)
         << "\"}";
    first = false;
  }
  if (!first) {
    body << ',';
  }
  body << "{\"role\":\"user\",\"content\":\"" << json_escape(message) << "\"}";
  body << "],";
  if (!tools.empty()) {
    body << "\"tools\":[";
    for (std::size_t i = 0; i < tools.size(); ++i) {
      if (i > 0) {
        body << ',';
      }
      const auto &tool = tools[i];
      body << "{";
      body << "\"type\":\"function\",";
      body << "\"function\":{";
      body << "\"name\":\"" << json_escape(tool.name) << "\",";
      body << "\"description\":\"" << json_escape(tool.description) << "\",";
      body << "\"parameters\":" << tool.parameters_json;
      body << "}";
      body << "}";
    }
    body << "],";
    body << "\"tool_choice\":\"auto\",";
  }
  body << "\"temperature\":" << temperature << ",";
  body << "\"stream\":" << (stream ? "true" : "false");
  body << "}";
  return body.str();
}

common::Status CompatibleProvider::validate_response_status(const HttpResponse &response) const {
  if (response.timeout) {
    return common::Status::error(
        ProviderError{.code = ProviderErrorCode::Timeout, .message = "request timed out"}.to_string());
  }

  if (response.network_error) {
    return common::Status::error(
        ProviderError{.code = ProviderErrorCode::NetworkError, .message = response.network_error_message}
            .to_string());
  }

  if (response.status == 401 || response.status == 403) {
    return common::Status::error(
        ProviderError{.code = ProviderErrorCode::AuthError,
                      .status = response.status,
                      .message = response.body}
            .to_string());
  }

  if (response.status == 404) {
    return common::Status::error(
        ProviderError{.code = ProviderErrorCode::ModelNotFound,
                      .status = response.status,
                      .message = response.body}
            .to_string());
  }

  if (response.status == 429) {
    ProviderError error{.code = ProviderErrorCode::RateLimitError,
                        .status = response.status,
                        .message = response.body};
    auto it = response.headers.find("retry-after");
    if (it != response.headers.end()) {
      try {
        error.retry_after = static_cast<std::uint64_t>(std::stoull(it->second));
      } catch (...) {
      }
    }
    return common::Status::error(error.to_string());
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

bool CompatibleProvider::is_sse_response(const HttpResponse &response) {
  const auto it = response.headers.find("content-type");
  if (it != response.headers.end() &&
      common::to_lower(it->second).find("text/event-stream") != std::string::npos) {
    return true;
  }
  return response.body.find("data:") != std::string::npos;
}

common::Result<std::string> CompatibleProvider::parse_sse_response(const HttpResponse &response) {
  auto parsed = parse_openai_sse_content(response.body);
  if (parsed.ok()) {
    return parsed;
  }
  return common::Result<std::string>::failure(
      ProviderError{.code = ProviderErrorCode::InvalidResponse, .message = parsed.error()}.to_string());
}

common::Result<std::string> CompatibleProvider::handle_response(const HttpResponse &response) const {
  auto status = validate_response_status(response);
  if (!status.ok()) {
    return common::Result<std::string>::failure(status.error());
  }

  if (is_sse_response(response)) {
    return parse_sse_response(response);
  }

  const auto parsed = parse_openai_content(response.body);
  if (!parsed.ok()) {
    return provider_error_result(
        {.code = ProviderErrorCode::InvalidResponse, .message = parsed.error()});
  }

  return parsed;
}

common::Result<std::string>
CompatibleProvider::chat_with_system(const std::optional<std::string> &system_prompt,
                                     const std::string &message, const std::string &model,
                                     const double temperature) {
  return chat_with_system_tools(system_prompt, message, model, temperature, {});
}

common::Result<std::string> CompatibleProvider::chat_with_system_tools(
    const std::optional<std::string> &system_prompt, const std::string &message,
    const std::string &model, const double temperature,
    const std::vector<tools::ToolSpec> &tools) {
  if (require_api_key_ && api_key_.empty()) {
    return provider_error_result(
        {.code = ProviderErrorCode::AuthError, .message = "missing API key"});
  }

  std::unordered_map<std::string, std::string> headers = {
      {"Content-Type", "application/json"},
      {"Authorization", api_key_.empty() ? "" : "Bearer " + api_key_},
  };

  for (const auto &[key, value] : extra_headers_) {
    headers[key] = value;
  }

  if (!require_api_key_ && api_key_.empty()) {
    headers.erase("Authorization");
  }

  const std::string body = build_body(system_prompt, message, model, temperature, tools, false);
  const auto response =
      http_client_->post_json(base_url_ + "/chat/completions", headers, body, 30'000);
  return handle_response(response);
}

common::Result<std::string>
CompatibleProvider::chat_with_system_stream(const std::optional<std::string> &system_prompt,
                                            const std::string &message, const std::string &model,
                                            const double temperature,
                                            const StreamChunkCallback &on_chunk) {
  if (require_api_key_ && api_key_.empty()) {
    return provider_error_result(
        {.code = ProviderErrorCode::AuthError, .message = "missing API key"});
  }

  std::unordered_map<std::string, std::string> headers = {
      {"Content-Type", "application/json"},
      {"Accept", "text/event-stream"},
      {"Authorization", api_key_.empty() ? "" : "Bearer " + api_key_},
  };

  for (const auto &[key, value] : extra_headers_) {
    headers[key] = value;
  }

  if (!require_api_key_ && api_key_.empty()) {
    headers.erase("Authorization");
  }

  std::string aggregated;
  std::string line_buffer;
  std::string event_data;
  bool saw_done = false;
  const auto stream_handler = [&](const std::string_view bytes) {
    parse_sse_bytes(bytes, line_buffer, event_data, [&](const std::string &event) {
      if (common::trim(event) == "[DONE]") {
        saw_done = true;
        return;
      }
      auto delta = parse_openai_sse_event_delta(event);
      if (!delta.ok() || delta.value().empty()) {
        return;
      }
      aggregated += delta.value();
      if (on_chunk) {
        on_chunk(delta.value());
      }
    });
  };

  const std::string body = build_body(system_prompt, message, model, temperature, {}, true);
  const auto response =
      http_client_->post_json_stream(base_url_ + "/chat/completions", headers, body, 30'000,
                                     stream_handler);

  stream_handler("\n\n");

  auto status = validate_response_status(response);
  if (!status.ok()) {
    return common::Result<std::string>::failure(status.error());
  }

  if (is_sse_response(response)) {
    if (!aggregated.empty() || saw_done) {
      return common::Result<std::string>::success(aggregated);
    }
    return parse_sse_response(response);
  }

  auto parsed = handle_response(response);
  if (!parsed.ok()) {
    return parsed;
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

common::Status CompatibleProvider::warmup() {
  const auto response = http_client_->head(base_url_, {}, 5'000);
  if (response.network_error || response.timeout) {
    return common::Status::success();
  }
  return common::Status::success();
}

std::string CompatibleProvider::name() const { return name_; }

} // namespace ghostclaw::providers
