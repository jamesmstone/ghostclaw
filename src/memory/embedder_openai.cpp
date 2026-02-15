#include "ghostclaw/memory/embedder_openai.hpp"

#include "ghostclaw/common/json_util.hpp"

#include <sstream>

namespace ghostclaw::memory {

namespace {

common::Result<std::vector<float>> parse_embedding_array(const std::string &body) {
  const std::size_t key_pos = body.find("\"embedding\"");
  if (key_pos == std::string::npos) {
    return common::Result<std::vector<float>>::failure("embedding field missing");
  }

  const std::size_t start = body.find('[', key_pos);
  const std::size_t end = body.find(']', start);
  if (start == std::string::npos || end == std::string::npos || end <= start) {
    return common::Result<std::vector<float>>::failure("embedding array parse failed");
  }

  std::vector<float> values;
  std::stringstream stream(body.substr(start + 1, end - start - 1));
  std::string item;
  while (std::getline(stream, item, ',')) {
    try {
      values.push_back(std::stof(item));
    } catch (...) {
      return common::Result<std::vector<float>>::failure("invalid embedding value");
    }
  }

  return common::Result<std::vector<float>>::success(std::move(values));
}

} // namespace

OpenAiEmbedder::OpenAiEmbedder(std::string api_key, std::string model, const std::size_t dimensions,
                               std::shared_ptr<providers::HttpClient> http_client)
    : api_key_(std::move(api_key)), model_(std::move(model)), dimensions_(dimensions),
      http_client_(std::move(http_client)) {}

std::string_view OpenAiEmbedder::name() const { return "openai"; }

common::Result<std::vector<float>> OpenAiEmbedder::embed(const std::string_view text) {
  if (api_key_.empty()) {
    return common::Result<std::vector<float>>::failure("missing API key");
  }

  std::ostringstream body;
  body << "{";
  body << "\"model\":\"" << common::json_escape(model_) << "\",";
  body << "\"input\":\"" << common::json_escape(std::string(text)) << "\"";
  body << "}";

  const std::unordered_map<std::string, std::string> headers = {
      {"Content-Type", "application/json"},
      {"Authorization", "Bearer " + api_key_},
  };

  const auto response =
      http_client_->post_json("https://api.openai.com/v1/embeddings", headers, body.str(), 30'000);
  if (response.timeout) {
    return common::Result<std::vector<float>>::failure("timeout");
  }
  if (response.network_error) {
    return common::Result<std::vector<float>>::failure(response.network_error_message);
  }
  if (response.status < 200 || response.status >= 300) {
    return common::Result<std::vector<float>>::failure("OpenAI embedding API error");
  }

  auto parsed = parse_embedding_array(response.body);
  if (!parsed.ok()) {
    return parsed;
  }

  if (parsed.value().size() != dimensions_) {
    parsed.value().resize(dimensions_, 0.0F);
  }

  return parsed;
}

common::Result<std::vector<std::vector<float>>>
OpenAiEmbedder::embed_batch(const std::vector<std::string> &texts) {
  std::vector<std::vector<float>> out;
  out.reserve(texts.size());
  for (const auto &text : texts) {
    auto emb = embed(text);
    if (!emb.ok()) {
      return common::Result<std::vector<std::vector<float>>>::failure(emb.error());
    }
    out.push_back(std::move(emb.value()));
  }
  return common::Result<std::vector<std::vector<float>>>::success(std::move(out));
}

std::size_t OpenAiEmbedder::dimensions() const { return dimensions_; }

} // namespace ghostclaw::memory
