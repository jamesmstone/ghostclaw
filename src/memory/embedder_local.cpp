#include "ghostclaw/memory/embedder_local.hpp"

#include <cmath>
#include <functional>

namespace ghostclaw::memory {

namespace {

float hash_to_unit(const std::size_t hash) {
  constexpr double max = static_cast<double>(std::numeric_limits<std::size_t>::max());
  const double normalized = static_cast<double>(hash) / max;
  return static_cast<float>(normalized * 2.0 - 1.0);
}

void normalize(std::vector<float> &values) {
  double norm = 0.0;
  for (float v : values) {
    norm += static_cast<double>(v) * static_cast<double>(v);
  }
  norm = std::sqrt(norm);
  if (norm < 1e-9) {
    return;
  }
  for (float &v : values) {
    v = static_cast<float>(static_cast<double>(v) / norm);
  }
}

} // namespace

LocalEmbedder::LocalEmbedder() = default;

std::string_view LocalEmbedder::name() const { return "local"; }

common::Result<std::vector<float>> LocalEmbedder::embed(const std::string_view text) {
  std::vector<float> values(kDimensions, 0.0F);

  std::hash<std::string_view> hasher;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const auto hash = hasher(text.substr(i));
    const std::size_t idx = hash % kDimensions;
    values[idx] += hash_to_unit(hash);
  }

  normalize(values);
  return common::Result<std::vector<float>>::success(std::move(values));
}

common::Result<std::vector<std::vector<float>>>
LocalEmbedder::embed_batch(const std::vector<std::string> &texts) {
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

std::size_t LocalEmbedder::dimensions() const { return kDimensions; }

} // namespace ghostclaw::memory
