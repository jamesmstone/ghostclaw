#pragma once

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/memory/embedder.hpp"
#include "ghostclaw/memory/hybrid_ranker.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/memory/vector_index.hpp"

#include <mutex>
#include <sqlite3.h>

namespace ghostclaw::memory {

class SqliteMemory final : public IMemory {
public:
  SqliteMemory(std::filesystem::path db_path, std::unique_ptr<IEmbedder> embedder,
               config::MemoryConfig config);
  ~SqliteMemory() override;

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] common::Status store(const std::string &key, const std::string &content,
                                     MemoryCategory category) override;
  [[nodiscard]] common::Result<std::vector<MemoryEntry>>
  recall(const std::string &query, std::size_t limit) override;
  [[nodiscard]] common::Result<std::optional<MemoryEntry>> get(const std::string &key) override;
  [[nodiscard]] common::Result<std::vector<MemoryEntry>>
  list(std::optional<MemoryCategory> category) override;
  [[nodiscard]] common::Result<bool> forget(const std::string &key) override;
  [[nodiscard]] common::Result<std::size_t> count() override;
  [[nodiscard]] common::Status reindex() override;
  [[nodiscard]] bool health_check() override;
  [[nodiscard]] MemoryStats stats() override;

private:
  [[nodiscard]] common::Status init_schema();
  [[nodiscard]] common::Result<std::vector<float>> embedding_for_text(const std::string &text);
  [[nodiscard]] common::Status cache_embedding(const std::string &text,
                                               const std::vector<float> &embedding);
  [[nodiscard]] common::Result<std::optional<std::vector<float>>>
  cached_embedding(const std::string &text);
  [[nodiscard]] common::Result<std::unordered_map<std::string, MemoryEntry>>
  load_entries_by_keys(const std::vector<std::string> &keys);
  [[nodiscard]] common::Result<MemoryEntry> row_to_entry(sqlite3_stmt *stmt) const;

  std::filesystem::path db_path_;
  sqlite3 *db_ = nullptr;
  std::mutex mutex_;
  std::unique_ptr<IEmbedder> embedder_;
  VectorIndex vector_index_;
  config::MemoryConfig config_;
  MemoryStats stats_;
};

} // namespace ghostclaw::memory
