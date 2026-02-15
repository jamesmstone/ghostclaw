#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ghostclaw::memory {

class IEmbedder {
public:
  virtual ~IEmbedder() = default;

  [[nodiscard]] virtual std::string_view name() const = 0;
  [[nodiscard]] virtual common::Result<std::vector<float>> embed(std::string_view text) = 0;
  [[nodiscard]] virtual common::Result<std::vector<std::vector<float>>>
  embed_batch(const std::vector<std::string> &texts) = 0;
  [[nodiscard]] virtual std::size_t dimensions() const = 0;
};

[[nodiscard]] std::unique_ptr<IEmbedder> create_embedder(const config::Config &config);

} // namespace ghostclaw::memory
