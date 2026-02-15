#pragma once

#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/memory/vector_index.hpp"

#include <unordered_map>

namespace ghostclaw::memory {

struct RankedResult {
  MemoryEntry entry;
  double vector_score = 0.0;
  double keyword_score = 0.0;
  double recency = 0.0;
  double final_score = 0.0;
};

class HybridRanker {
public:
  HybridRanker(double vector_weight, double keyword_weight, double recency_weight);

  [[nodiscard]] std::vector<RankedResult>
  rank(const std::vector<VectorSearchResult> &vector_results,
       const std::vector<std::pair<std::string, double>> &keyword_results,
       const std::unordered_map<std::string, MemoryEntry> &entries, std::size_t limit) const;

private:
  double vector_weight_;
  double keyword_weight_;
  double recency_weight_;
};

} // namespace ghostclaw::memory
