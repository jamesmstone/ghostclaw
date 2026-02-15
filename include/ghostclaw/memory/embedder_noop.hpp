#pragma once

#include "ghostclaw/memory/embedder.hpp"

namespace ghostclaw::memory {

class NoopEmbedder final : public IEmbedder {
public:
  explicit NoopEmbedder(std::size_t dimensions);

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] common::Result<std::vector<float>> embed(std::string_view text) override;
  [[nodiscard]] common::Result<std::vector<std::vector<float>>>
  embed_batch(const std::vector<std::string> &texts) override;
  [[nodiscard]] std::size_t dimensions() const override;

private:
  std::size_t dimensions_;
};

} // namespace ghostclaw::memory
