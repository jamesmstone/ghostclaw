#include "ghostclaw/memory/vector_index.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace ghostclaw::memory {

float cosine_similarity(const std::vector<float> &a, const std::vector<float> &b) {
  if (a.empty() || b.empty() || a.size() != b.size()) {
    return 0.0F;
  }

  double dot = 0.0;
  double norm_a = 0.0;
  double norm_b = 0.0;

  for (std::size_t i = 0; i < a.size(); ++i) {
    dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
    norm_a += static_cast<double>(a[i]) * static_cast<double>(a[i]);
    norm_b += static_cast<double>(b[i]) * static_cast<double>(b[i]);
  }

  if (norm_a < 1e-9 || norm_b < 1e-9) {
    return 0.0F;
  }

  return static_cast<float>(dot / (std::sqrt(norm_a) * std::sqrt(norm_b)));
}

VectorIndex::VectorIndex(const std::size_t dimensions, const std::size_t max_elements)
    : dimensions_(dimensions), max_elements_(max_elements) {}

common::Status VectorIndex::add(const std::string &key, const std::vector<float> &embedding) {
  if (embedding.size() != dimensions_) {
    return common::Status::error("embedding dimensions mismatch");
  }
  if (!contains(key) && vectors_.size() >= max_elements_) {
    return common::Status::error("vector index full");
  }
  vectors_[key] = embedding;
  return common::Status::success();
}

common::Status VectorIndex::remove(const std::string &key) {
  vectors_.erase(key);
  return common::Status::success();
}

common::Result<std::vector<VectorSearchResult>>
VectorIndex::search(const std::vector<float> &query, const std::size_t limit) const {
  if (query.size() != dimensions_) {
    return common::Result<std::vector<VectorSearchResult>>::failure("query dimensions mismatch");
  }

  std::vector<VectorSearchResult> results;
  results.reserve(vectors_.size());

  for (const auto &[key, embedding] : vectors_) {
    const float similarity = cosine_similarity(query, embedding);
    results.push_back(VectorSearchResult{
        .key = key,
        .distance = 1.0F - similarity,
        .score = std::clamp((similarity + 1.0F) / 2.0F, 0.0F, 1.0F),
    });
  }

  std::sort(results.begin(), results.end(), [](const auto &lhs, const auto &rhs) {
    return lhs.score > rhs.score;
  });

  if (results.size() > limit) {
    results.resize(limit);
  }

  return common::Result<std::vector<VectorSearchResult>>::success(std::move(results));
}

std::size_t VectorIndex::size() const { return vectors_.size(); }

bool VectorIndex::contains(const std::string &key) const { return vectors_.contains(key); }

common::Status VectorIndex::save(const std::filesystem::path &path) const {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return common::Status::error("failed to open vector index for write");
  }

  const std::uint64_t dims = dimensions_;
  const std::uint64_t count = vectors_.size();
  out.write(reinterpret_cast<const char *>(&dims), sizeof(dims));
  out.write(reinterpret_cast<const char *>(&count), sizeof(count));

  for (const auto &[key, embedding] : vectors_) {
    const std::uint64_t key_size = key.size();
    out.write(reinterpret_cast<const char *>(&key_size), sizeof(key_size));
    out.write(key.data(), static_cast<std::streamsize>(key.size()));
    out.write(reinterpret_cast<const char *>(embedding.data()),
              static_cast<std::streamsize>(embedding.size() * sizeof(float)));
  }

  return out ? common::Status::success() : common::Status::error("failed to write vector index");
}

common::Status VectorIndex::load(const std::filesystem::path &path) {
  if (!std::filesystem::exists(path)) {
    return common::Status::success();
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return common::Status::error("failed to open vector index");
  }

  std::uint64_t dims = 0;
  std::uint64_t count = 0;
  in.read(reinterpret_cast<char *>(&dims), sizeof(dims));
  in.read(reinterpret_cast<char *>(&count), sizeof(count));
  if (!in) {
    return common::Status::error("failed to read vector index header");
  }

  if (dims != dimensions_) {
    return common::Status::error("vector index dimensions mismatch");
  }

  vectors_.clear();
  for (std::uint64_t i = 0; i < count; ++i) {
    std::uint64_t key_size = 0;
    in.read(reinterpret_cast<char *>(&key_size), sizeof(key_size));
    if (!in) {
      return common::Status::error("failed to read key size");
    }

    std::string key(key_size, '\0');
    in.read(key.data(), static_cast<std::streamsize>(key_size));
    std::vector<float> embedding(dimensions_);
    in.read(reinterpret_cast<char *>(embedding.data()),
            static_cast<std::streamsize>(embedding.size() * sizeof(float)));
    if (!in) {
      return common::Status::error("failed to read vector payload");
    }

    vectors_[std::move(key)] = std::move(embedding);
  }

  return common::Status::success();
}

} // namespace ghostclaw::memory
