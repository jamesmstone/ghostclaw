#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ghostclaw::memory {

enum class MemoryCategory {
  Core,
  Daily,
  Conversation,
  Custom,
};

[[nodiscard]] std::string category_to_string(MemoryCategory category);
[[nodiscard]] MemoryCategory category_from_string(std::string_view value);

struct MemoryEntry {
  std::string key;
  std::string content;
  MemoryCategory category = MemoryCategory::Core;
  std::string created_at;
  std::string updated_at;
  std::optional<double> score;
  std::optional<std::string> source_file;
  std::optional<std::string> heading;
};

struct MemoryStats {
  std::size_t total_entries = 0;
  std::size_t total_vectors = 0;
  std::size_t cache_size = 0;
  std::size_t cache_hits = 0;
  std::size_t cache_misses = 0;
};

class IMemory {
public:
  virtual ~IMemory() = default;

  [[nodiscard]] virtual std::string_view name() const = 0;
  [[nodiscard]] virtual common::Status store(const std::string &key, const std::string &content,
                                             MemoryCategory category) = 0;
  [[nodiscard]] virtual common::Result<std::vector<MemoryEntry>>
  recall(const std::string &query, std::size_t limit) = 0;
  [[nodiscard]] virtual common::Result<std::optional<MemoryEntry>> get(const std::string &key) = 0;
  [[nodiscard]] virtual common::Result<std::vector<MemoryEntry>>
  list(std::optional<MemoryCategory> category) = 0;
  [[nodiscard]] virtual common::Result<bool> forget(const std::string &key) = 0;
  [[nodiscard]] virtual common::Result<std::size_t> count() = 0;
  [[nodiscard]] virtual common::Status reindex() = 0;
  [[nodiscard]] virtual bool health_check() = 0;
  [[nodiscard]] virtual MemoryStats stats() = 0;
};

[[nodiscard]] std::unique_ptr<IMemory> create_memory(const config::Config &config,
                                                     const std::filesystem::path &workspace);

[[nodiscard]] std::string now_rfc3339();
[[nodiscard]] double recency_score(const std::string &updated_at, double half_life_days);

} // namespace ghostclaw::memory
