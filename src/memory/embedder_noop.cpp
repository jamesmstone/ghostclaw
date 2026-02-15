#include "ghostclaw/memory/embedder_noop.hpp"

namespace ghostclaw::memory {

NoopEmbedder::NoopEmbedder(const std::size_t dimensions) : dimensions_(dimensions) {}

std::string_view NoopEmbedder::name() const { return "noop"; }

common::Result<std::vector<float>> NoopEmbedder::embed(std::string_view) {
  return common::Result<std::vector<float>>::success(std::vector<float>(dimensions_, 0.0F));
}

common::Result<std::vector<std::vector<float>>>
NoopEmbedder::embed_batch(const std::vector<std::string> &texts) {
  std::vector<std::vector<float>> out;
  out.reserve(texts.size());
  for (std::size_t i = 0; i < texts.size(); ++i) {
    out.emplace_back(dimensions_, 0.0F);
  }
  return common::Result<std::vector<std::vector<float>>>::success(std::move(out));
}

std::size_t NoopEmbedder::dimensions() const { return dimensions_; }

} // namespace ghostclaw::memory
