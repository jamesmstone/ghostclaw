#pragma once

#include "ghostclaw/providers/traits.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace ghostclaw::providers {

class AnthropicProvider : public Provider {
public:
  explicit AnthropicProvider(std::string api_key,
                             std::shared_ptr<HttpClient> http_client = std::make_shared<CurlHttpClient>());
  AnthropicProvider(std::string name, std::string api_key, std::string base_url,
                    std::shared_ptr<HttpClient> http_client = std::make_shared<CurlHttpClient>(),
                    bool use_bearer_auth = false,
                    std::unordered_map<std::string, std::string> extra_headers = {});

  [[nodiscard]] common::Result<std::string>
  chat(const std::string &message, const std::string &model, double temperature) override;

  [[nodiscard]] common::Result<std::string>
  chat_with_system(const std::optional<std::string> &system_prompt, const std::string &message,
                   const std::string &model, double temperature) override;
  [[nodiscard]] common::Result<std::string>
  chat_with_system_stream(const std::optional<std::string> &system_prompt,
                          const std::string &message, const std::string &model,
                          double temperature, const StreamChunkCallback &on_chunk) override;

  [[nodiscard]] common::Status warmup() override;
  [[nodiscard]] std::string name() const override;

private:
  [[nodiscard]] std::unordered_map<std::string, std::string>
  build_headers(bool stream) const;
  [[nodiscard]] std::string messages_url() const;

  std::string name_ = "anthropic";
  std::string api_key_;
  std::string base_url_ = "https://api.anthropic.com";
  std::shared_ptr<HttpClient> http_client_;
  bool use_bearer_auth_ = false;
  std::unordered_map<std::string, std::string> extra_headers_;
};

} // namespace ghostclaw::providers
