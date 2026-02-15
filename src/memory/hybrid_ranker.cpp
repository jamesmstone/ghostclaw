#include "ghostclaw/memory/hybrid_ranker.hpp"

#include <algorithm>
#include <unordered_map>

namespace ghostclaw::memory {

HybridRanker::HybridRanker(const double vector_weight, const double keyword_weight,
                           const double recency_weight)
    : vector_weight_(vector_weight), keyword_weight_(keyword_weight),
      recency_weight_(recency_weight) {}

std::vector<RankedResult> HybridRanker::rank(
    const std::vector<VectorSearchResult> &vector_results,
    const std::vector<std::pair<std::string, double>> &keyword_results,
    const std::unordered_map<std::string, MemoryEntry> &entries, const std::size_t limit) const {
  std::unordered_map<std::string, double> vector_by_key;
  std::unordered_map<std::string, double> keyword_by_key;

  for (const auto &result : vector_results) {
    vector_by_key[result.key] = result.score;
  }
  for (const auto &[key, score] : keyword_results) {
    keyword_by_key[key] = score;
  }

  std::vector<RankedResult> ranked;
  ranked.reserve(entries.size());

  for (const auto &[key, entry] : entries) {
    const double vec = vector_by_key.contains(key) ? vector_by_key.at(key) : 0.0;
    const double kw = keyword_by_key.contains(key) ? keyword_by_key.at(key) : 0.0;
    const double rec = recency_score(entry.updated_at, 14.0);

    RankedResult result;
    result.entry = entry;
    result.vector_score = vec;
    result.keyword_score = kw;
    result.recency = rec;
    result.final_score = vector_weight_ * vec + keyword_weight_ * kw + recency_weight_ * rec;
    result.entry.score = result.final_score;
    ranked.push_back(std::move(result));
  }

  std::sort(ranked.begin(), ranked.end(),
            [](const auto &lhs, const auto &rhs) { return lhs.final_score > rhs.final_score; });

  if (ranked.size() > limit) {
    ranked.resize(limit);
  }
  return ranked;
}

} // namespace ghostclaw::memory
