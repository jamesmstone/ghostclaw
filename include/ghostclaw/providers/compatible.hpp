#pragma once

#include "ghostclaw/providers/traits.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace ghostclaw::providers {

class CompatibleProvider : public Provider {
public:
  CompatibleProvider(std::string name, std::string base_url, std::string api_key,
                     std::shared_ptr<HttpClient> http_client = std::make_shared<CurlHttpClient>(),
                     bool require_api_key = true,
                     std::unordered_map<std::string, std::string> extra_headers = {});

  [[nodiscard]] common::Result<std::string>
  chat(const std::string &message, const std::string &model, double temperature) override;

  [[nodiscard]] common::Result<std::string>
  chat_with_system(const std::optional<std::string> &system_prompt, const std::string &message,
                   const std::string &model, double temperature) override;
  [[nodiscard]] common::Result<std::string>
  chat_with_system_tools(const std::optional<std::string> &system_prompt,
                         const std::string &message, const std::string &model,
                         double temperature,
                         const std::vector<tools::ToolSpec> &tools) override;
  [[nodiscard]] common::Result<std::string>
  chat_with_system_stream(const std::optional<std::string> &system_prompt,
                          const std::string &message, const std::string &model,
                          double temperature, const StreamChunkCallback &on_chunk) override;

  [[nodiscard]] common::Status warmup() override;
  [[nodiscard]] std::string name() const override;

private:
  [[nodiscard]] std::string build_body(const std::optional<std::string> &system_prompt,
                                       const std::string &message, const std::string &model,
                                       double temperature,
                                       const std::vector<tools::ToolSpec> &tools = {},
                                       bool stream = false) const;

  [[nodiscard]] common::Result<std::string> handle_response(const HttpResponse &response) const;
  [[nodiscard]] common::Status validate_response_status(const HttpResponse &response) const;
  [[nodiscard]] static bool is_sse_response(const HttpResponse &response);
  [[nodiscard]] static common::Result<std::string> parse_sse_response(const HttpResponse &response);

  std::string name_;
  std::string base_url_;
  std::string api_key_;
  std::shared_ptr<HttpClient> http_client_;
  bool require_api_key_ = true;
  std::unordered_map<std::string, std::string> extra_headers_;
};

} // namespace ghostclaw::providers
