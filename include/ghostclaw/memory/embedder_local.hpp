#pragma once

#include "ghostclaw/memory/embedder.hpp"

namespace ghostclaw::memory {

class LocalEmbedder final : public IEmbedder {
public:
  LocalEmbedder();

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] common::Result<std::vector<float>> embed(std::string_view text) override;
  [[nodiscard]] common::Result<std::vector<std::vector<float>>>
  embed_batch(const std::vector<std::string> &texts) override;
  [[nodiscard]] std::size_t dimensions() const override;

private:
  static constexpr std::size_t kDimensions = 384;
};

} // namespace ghostclaw::memory
