#include "ghostclaw/memory/sqlite_store.hpp"

#include "ghostclaw/common/fs.hpp"

#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace ghostclaw::memory {

namespace {

std::string sha256_hex(const std::string &text) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(text.data()), text.size(), digest);

  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const unsigned char c : digest) {
    stream << std::setw(2) << static_cast<int>(c);
  }
  return stream.str();
}

std::vector<unsigned char> vector_to_blob(const std::vector<float> &values) {
  std::vector<unsigned char> blob(values.size() * sizeof(float));
  std::memcpy(blob.data(), values.data(), blob.size());
  return blob;
}

std::vector<float> blob_to_vector(const void *blob, const int bytes) {
  if (blob == nullptr || bytes <= 0 || (bytes % static_cast<int>(sizeof(float)) != 0)) {
    return {};
  }

  const std::size_t length = static_cast<std::size_t>(bytes) / sizeof(float);
  std::vector<float> values(length);
  std::memcpy(values.data(), blob, static_cast<std::size_t>(bytes));
  return values;
}

common::Status exec_sql(sqlite3 *db, const std::string &sql) {
  char *err = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    const std::string msg = err == nullptr ? "sqlite error" : err;
    if (err != nullptr) {
      sqlite3_free(err);
    }
    return common::Status::error(msg);
  }
  return common::Status::success();
}

} // namespace

SqliteMemory::SqliteMemory(std::filesystem::path db_path, std::unique_ptr<IEmbedder> embedder,
                           config::MemoryConfig config)
    : db_path_(std::move(db_path)), embedder_(std::move(embedder)),
      vector_index_(embedder_->dimensions()), config_(std::move(config)) {
  std::error_code ec;
  std::filesystem::create_directories(db_path_.parent_path(), ec);

  if (sqlite3_open(db_path_.string().c_str(), &db_) != SQLITE_OK) {
    db_ = nullptr;
    return;
  }

  (void)init_schema();
  (void)reindex();
}

SqliteMemory::~SqliteMemory() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
  }
}

std::string_view SqliteMemory::name() const { return "sqlite"; }

common::Status SqliteMemory::init_schema() {
  if (db_ == nullptr) {
    return common::Status::error("database is not initialized");
  }

  auto status = exec_sql(db_, "PRAGMA journal_mode=WAL;");
  if (!status.ok()) {
    return status;
  }

  status = exec_sql(db_, R"(
CREATE TABLE IF NOT EXISTS memories (
  key TEXT PRIMARY KEY,
  content TEXT NOT NULL,
  category TEXT NOT NULL DEFAULT 'core',
  embedding BLOB,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
)");
  if (!status.ok()) {
    return status;
  }

  status = exec_sql(db_, R"(
CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5(
  key, content, category,
  content=memories, content_rowid=rowid
);
)");
  if (!status.ok()) {
    return status;
  }

  status = exec_sql(db_, R"(
CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN
  INSERT INTO memories_fts(rowid, key, content, category)
  VALUES (new.rowid, new.key, new.content, new.category);
END;
)");
  if (!status.ok()) {
    return status;
  }

  status = exec_sql(db_, R"(
CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN
  INSERT INTO memories_fts(memories_fts, rowid, key, content, category)
  VALUES ('delete', old.rowid, old.key, old.content, old.category);
END;
)");
  if (!status.ok()) {
    return status;
  }

  status = exec_sql(db_, R"(
CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories BEGIN
  INSERT INTO memories_fts(memories_fts, rowid, key, content, category)
  VALUES ('delete', old.rowid, old.key, old.content, old.category);
  INSERT INTO memories_fts(rowid, key, content, category)
  VALUES (new.rowid, new.key, new.content, new.category);
END;
)");
  if (!status.ok()) {
    return status;
  }

  status = exec_sql(db_, R"(
CREATE TABLE IF NOT EXISTS embedding_cache (
  text_hash TEXT PRIMARY KEY,
  embedding BLOB NOT NULL,
  created_at TEXT NOT NULL
);
)");
  return status;
}

common::Result<std::optional<std::vector<float>>> SqliteMemory::cached_embedding(const std::string &text) {
  const std::string hash = sha256_hex(text);

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT embedding FROM embedding_cache WHERE text_hash = ?1";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return common::Result<std::optional<std::vector<float>>>::failure(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);

  const int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    const void *blob = sqlite3_column_blob(stmt, 0);
    const int bytes = sqlite3_column_bytes(stmt, 0);
    auto embedding = blob_to_vector(blob, bytes);
    sqlite3_finalize(stmt);
    ++stats_.cache_hits;
    return common::Result<std::optional<std::vector<float>>>::success(std::move(embedding));
  }

  sqlite3_finalize(stmt);
  ++stats_.cache_misses;
  return common::Result<std::optional<std::vector<float>>>::success(std::nullopt);
}

common::Status SqliteMemory::cache_embedding(const std::string &text,
                                             const std::vector<float> &embedding) {
  const std::string hash = sha256_hex(text);
  const auto blob = vector_to_blob(embedding);

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "INSERT OR REPLACE INTO embedding_cache(text_hash, embedding, created_at) VALUES(?1, ?2, ?3)";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return common::Status::error(sqlite3_errmsg(db_));
  }

  sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_blob(stmt, 2, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
  const std::string now = now_rfc3339();
  sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return common::Status::error(sqlite3_errmsg(db_));
  }

  sqlite3_stmt *count_stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM embedding_cache", -1, &count_stmt, nullptr) ==
      SQLITE_OK) {
    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
      stats_.cache_size = static_cast<std::size_t>(sqlite3_column_int64(count_stmt, 0));
    }
    sqlite3_finalize(count_stmt);
  }

  if (stats_.cache_size > config_.embedding_cache_size) {
    const std::size_t overflow = stats_.cache_size - config_.embedding_cache_size;
    std::ostringstream trim_sql;
    trim_sql << "DELETE FROM embedding_cache WHERE text_hash IN ("
             << "SELECT text_hash FROM embedding_cache ORDER BY created_at ASC LIMIT " << overflow
             << ")";
    auto trim_status = exec_sql(db_, trim_sql.str());
    if (!trim_status.ok()) {
      return trim_status;
    }
    stats_.cache_size = config_.embedding_cache_size;
  }

  return common::Status::success();
}

common::Result<std::vector<float>> SqliteMemory::embedding_for_text(const std::string &text) {
  auto cached = cached_embedding(text);
  if (!cached.ok()) {
    return common::Result<std::vector<float>>::failure(cached.error());
  }
  if (cached.value().has_value()) {
    return common::Result<std::vector<float>>::success(std::move(*cached.value()));
  }

  auto embedded = embedder_->embed(text);
  if (!embedded.ok()) {
    return embedded;
  }

  auto cache_status = cache_embedding(text, embedded.value());
  if (!cache_status.ok()) {
    return common::Result<std::vector<float>>::failure(cache_status.error());
  }

  return embedded;
}

common::Status SqliteMemory::store(const std::string &key, const std::string &content,
                                   const MemoryCategory category) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == nullptr) {
    return common::Status::error("database not initialized");
  }

  std::optional<std::vector<float>> embedding;
  auto embedded = embedding_for_text(content);
  if (embedded.ok()) {
    embedding = std::move(embedded.value());
  }

  std::string created_at = now_rfc3339();
  std::string updated_at = created_at;

  sqlite3_stmt *lookup = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT created_at FROM memories WHERE key = ?1", -1, &lookup, nullptr) ==
      SQLITE_OK) {
    sqlite3_bind_text(lookup, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(lookup) == SQLITE_ROW) {
      created_at = reinterpret_cast<const char *>(sqlite3_column_text(lookup, 0));
    }
    sqlite3_finalize(lookup);
  }

  sqlite3_stmt *stmt = nullptr;
  const char *sql = R"(
INSERT INTO memories(key, content, category, embedding, created_at, updated_at)
VALUES(?1, ?2, ?3, ?4, ?5, ?6)
ON CONFLICT(key) DO UPDATE SET
  content=excluded.content,
  category=excluded.category,
  embedding=excluded.embedding,
  updated_at=excluded.updated_at
)";

  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return common::Status::error(sqlite3_errmsg(db_));
  }

  const auto category_value = category_to_string(category);
  sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, category_value.c_str(), -1, SQLITE_TRANSIENT);
  if (embedding.has_value()) {
    const auto blob = vector_to_blob(*embedding);
    sqlite3_bind_blob(stmt, 4, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(stmt, 4);
  }
  sqlite3_bind_text(stmt, 5, created_at.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, updated_at.c_str(), -1, SQLITE_TRANSIENT);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return common::Status::error(sqlite3_errmsg(db_));
  }

  if (embedding.has_value()) {
    auto index_status = vector_index_.add(key, *embedding);
    if (!index_status.ok()) {
      return index_status;
    }
  } else {
    (void)vector_index_.remove(key);
  }

  return common::Status::success();
}

common::Result<MemoryEntry> SqliteMemory::row_to_entry(sqlite3_stmt *stmt) const {
  MemoryEntry entry;
  entry.key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  entry.content = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  entry.category = category_from_string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)));
  entry.created_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  entry.updated_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  return common::Result<MemoryEntry>::success(std::move(entry));
}

common::Result<std::unordered_map<std::string, MemoryEntry>>
SqliteMemory::load_entries_by_keys(const std::vector<std::string> &keys) {
  std::unordered_map<std::string, MemoryEntry> map;

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT key, content, category, created_at, updated_at FROM memories WHERE key = ?1";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return common::Result<std::unordered_map<std::string, MemoryEntry>>::failure(sqlite3_errmsg(db_));
  }

  for (const auto &key : keys) {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      auto row = row_to_entry(stmt);
      if (row.ok()) {
        map[row.value().key] = std::move(row.value());
      }
    }
  }

  sqlite3_finalize(stmt);
  return common::Result<std::unordered_map<std::string, MemoryEntry>>::success(std::move(map));
}

common::Result<std::vector<MemoryEntry>> SqliteMemory::recall(const std::string &query,
                                                              const std::size_t limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == nullptr) {
    return common::Result<std::vector<MemoryEntry>>::failure("database not initialized");
  }

  if (query.empty()) {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(
            db_,
            "SELECT key, content, category, created_at, updated_at FROM memories ORDER BY updated_at DESC LIMIT ?1",
            -1, &stmt, nullptr) != SQLITE_OK) {
      return common::Result<std::vector<MemoryEntry>>::failure(sqlite3_errmsg(db_));
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(limit));
    std::vector<MemoryEntry> entries;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      auto row = row_to_entry(stmt);
      if (row.ok()) {
        entries.push_back(std::move(row.value()));
      }
    }
    sqlite3_finalize(stmt);
    return common::Result<std::vector<MemoryEntry>>::success(std::move(entries));
  }

  std::vector<VectorSearchResult> vector_results;
  auto query_embedding = embedding_for_text(query);
  if (query_embedding.ok()) {
    auto searched = vector_index_.search(query_embedding.value(), limit * 3);
    if (searched.ok()) {
      vector_results = std::move(searched.value());
    }
  }

  std::vector<std::pair<std::string, double>> keyword_results;
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT key, bm25(memories_fts) FROM memories_fts WHERE memories_fts MATCH ?1 LIMIT ?2",
                         -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(limit * 3));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const std::string key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
      const double bm25 = sqlite3_column_double(stmt, 1);
      keyword_results.emplace_back(key, 1.0 / (1.0 + std::max(0.0, bm25)));
    }
    sqlite3_finalize(stmt);
  }

  if (keyword_results.empty()) {
    sqlite3_stmt *like_stmt = nullptr;
    if (sqlite3_prepare_v2(
            db_,
            "SELECT key FROM memories WHERE content LIKE ?1 OR key LIKE ?1 ORDER BY updated_at DESC LIMIT ?2",
            -1, &like_stmt, nullptr) == SQLITE_OK) {
      const std::string like_pattern = "%" + query + "%";
      sqlite3_bind_text(like_stmt, 1, like_pattern.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(like_stmt, 2, static_cast<sqlite3_int64>(limit * 3));
      std::size_t ordinal = 0;
      while (sqlite3_step(like_stmt) == SQLITE_ROW) {
        const std::string key = reinterpret_cast<const char *>(sqlite3_column_text(like_stmt, 0));
        const double score = 1.0 / (1.0 + static_cast<double>(ordinal));
        keyword_results.emplace_back(key, score);
        ++ordinal;
      }
      sqlite3_finalize(like_stmt);
    }
  }

  std::vector<std::string> keys;
  for (const auto &item : vector_results) {
    keys.push_back(item.key);
  }
  for (const auto &[key, _] : keyword_results) {
    keys.push_back(key);
  }

  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

  auto entries_by_key = load_entries_by_keys(keys);
  if (!entries_by_key.ok()) {
    return common::Result<std::vector<MemoryEntry>>::failure(entries_by_key.error());
  }

  HybridRanker ranker(config_.vector_weight, config_.keyword_weight, 0.1);
  auto ranked = ranker.rank(vector_results, keyword_results, entries_by_key.value(), limit);

  std::vector<MemoryEntry> out;
  out.reserve(ranked.size());
  for (auto &entry : ranked) {
    out.push_back(std::move(entry.entry));
  }

  return common::Result<std::vector<MemoryEntry>>::success(std::move(out));
}

common::Result<std::optional<MemoryEntry>> SqliteMemory::get(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT key, content, category, created_at, updated_at FROM memories WHERE key = ?1",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    return common::Result<std::optional<MemoryEntry>>::failure(sqlite3_errmsg(db_));
  }

  sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    auto row = row_to_entry(stmt);
    sqlite3_finalize(stmt);
    if (!row.ok()) {
      return common::Result<std::optional<MemoryEntry>>::failure(row.error());
    }
    return common::Result<std::optional<MemoryEntry>>::success(std::move(row.value()));
  }

  sqlite3_finalize(stmt);
  return common::Result<std::optional<MemoryEntry>>::success(std::nullopt);
}

common::Result<std::vector<MemoryEntry>>
SqliteMemory::list(const std::optional<MemoryCategory> category) {
  std::lock_guard<std::mutex> lock(mutex_);
  sqlite3_stmt *stmt = nullptr;
  const char *all_sql =
      "SELECT key, content, category, created_at, updated_at FROM memories ORDER BY updated_at DESC";
  const char *filter_sql =
      "SELECT key, content, category, created_at, updated_at FROM memories WHERE category = ?1 ORDER BY updated_at DESC";

  if (sqlite3_prepare_v2(db_, category.has_value() ? filter_sql : all_sql, -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return common::Result<std::vector<MemoryEntry>>::failure(sqlite3_errmsg(db_));
  }

  std::string category_value;
  if (category.has_value()) {
    category_value = category_to_string(*category);
    sqlite3_bind_text(stmt, 1, category_value.c_str(), -1, SQLITE_TRANSIENT);
  }

  std::vector<MemoryEntry> entries;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto row = row_to_entry(stmt);
    if (row.ok()) {
      entries.push_back(std::move(row.value()));
    }
  }

  sqlite3_finalize(stmt);
  return common::Result<std::vector<MemoryEntry>>::success(std::move(entries));
}

common::Result<bool> SqliteMemory::forget(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM memories WHERE key = ?1", -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return common::Result<bool>::failure(sqlite3_errmsg(db_));
  }

  sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return common::Result<bool>::failure(sqlite3_errmsg(db_));
  }

  const bool removed = sqlite3_changes(db_) > 0;
  if (removed) {
    (void)vector_index_.remove(key);
  }

  return common::Result<bool>::success(removed);
}

common::Result<std::size_t> SqliteMemory::count() {
  std::lock_guard<std::mutex> lock(mutex_);
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM memories", -1, &stmt, nullptr) != SQLITE_OK) {
    return common::Result<std::size_t>::failure(sqlite3_errmsg(db_));
  }

  std::size_t value = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    value = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
  }
  sqlite3_finalize(stmt);
  return common::Result<std::size_t>::success(value);
}

common::Status SqliteMemory::reindex() {
  std::lock_guard<std::mutex> lock(mutex_);
  vector_index_ = VectorIndex(embedder_->dimensions());

  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT key, embedding FROM memories", -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return common::Status::error(sqlite3_errmsg(db_));
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const std::string key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    const void *blob = sqlite3_column_blob(stmt, 1);
    const int bytes = sqlite3_column_bytes(stmt, 1);
    auto vector = blob_to_vector(blob, bytes);
    if (vector.size() == embedder_->dimensions()) {
      auto status = vector_index_.add(key, vector);
      if (!status.ok()) {
        sqlite3_finalize(stmt);
        return status;
      }
    }
  }

  sqlite3_finalize(stmt);
  return common::Status::success();
}

bool SqliteMemory::health_check() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == nullptr) {
    return false;
  }

  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "SELECT 1", -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  const bool ok = sqlite3_step(stmt) == SQLITE_ROW;
  sqlite3_finalize(stmt);
  return ok;
}

MemoryStats SqliteMemory::stats() {
  auto counted = count();
  if (counted.ok()) {
    stats_.total_entries = counted.value();
  }
  stats_.total_vectors = vector_index_.size();
  return stats_;
}

} // namespace ghostclaw::memory
