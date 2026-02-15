#pragma once

#include "ghostclaw/common/result.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ghostclaw::memory {

struct VectorSearchResult {
  std::string key;
  float distance = 0.0F;
  float score = 0.0F;
};

class VectorIndex {
public:
  explicit VectorIndex(std::size_t dimensions, std::size_t max_elements = 100000);

  [[nodiscard]] common::Status load(const std::filesystem::path &path);
  [[nodiscard]] common::Status save(const std::filesystem::path &path) const;

  [[nodiscard]] common::Status add(const std::string &key, const std::vector<float> &embedding);
  [[nodiscard]] common::Status remove(const std::string &key);
  [[nodiscard]] common::Result<std::vector<VectorSearchResult>>
  search(const std::vector<float> &query, std::size_t limit) const;

  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool contains(const std::string &key) const;

private:
  std::size_t dimensions_;
  std::size_t max_elements_;
  std::unordered_map<std::string, std::vector<float>> vectors_;
};

[[nodiscard]] float cosine_similarity(const std::vector<float> &a, const std::vector<float> &b);

} // namespace ghostclaw::memory
