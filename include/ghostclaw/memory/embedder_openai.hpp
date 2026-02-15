#pragma once

#include "ghostclaw/memory/embedder.hpp"
#include "ghostclaw/providers/traits.hpp"

namespace ghostclaw::memory {

class OpenAiEmbedder final : public IEmbedder {
public:
  OpenAiEmbedder(std::string api_key, std::string model, std::size_t dimensions,
                 std::shared_ptr<providers::HttpClient> http_client =
                     std::make_shared<providers::CurlHttpClient>());

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] common::Result<std::vector<float>> embed(std::string_view text) override;
  [[nodiscard]] common::Result<std::vector<std::vector<float>>>
  embed_batch(const std::vector<std::string> &texts) override;
  [[nodiscard]] std::size_t dimensions() const override;

private:
  std::string api_key_;
  std::string model_;
  std::size_t dimensions_;
  std::shared_ptr<providers::HttpClient> http_client_;
};

} // namespace ghostclaw::memory
