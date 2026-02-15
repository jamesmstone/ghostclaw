#include "test_framework.hpp"

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/memory/chunker.hpp"
#include "ghostclaw/memory/embedder_local.hpp"
#include "ghostclaw/memory/embedder_noop.hpp"
#include "ghostclaw/memory/hybrid_ranker.hpp"
#include "ghostclaw/memory/markdown_store.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/memory/sqlite_store.hpp"
#include "ghostclaw/memory/vector_index.hpp"
#include "ghostclaw/memory/workspace_indexer.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <unordered_map>

namespace {

std::filesystem::path make_temp_dir() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto base = std::filesystem::temp_directory_path() /
                    ("ghostclaw-memory-test-" + std::to_string(rng()));
  std::filesystem::create_directories(base);
  return base;
}

void write_file(const std::filesystem::path &path, const std::string &content) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  out << content;
}

class CountingMemory final : public ghostclaw::memory::IMemory {
public:
  [[nodiscard]] std::string_view name() const override { return "counting"; }

  [[nodiscard]] ghostclaw::common::Status store(const std::string &key, const std::string &content,
                                                ghostclaw::memory::MemoryCategory) override {
    ++store_calls;
    entries[key] = content;
    return ghostclaw::common::Status::success();
  }

  [[nodiscard]] ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>
  recall(const std::string &, std::size_t) override {
    return ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>::success({});
  }

  [[nodiscard]] ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>
  get(const std::string &) override {
    return ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>::success(
        std::nullopt);
  }

  [[nodiscard]] ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>
  list(std::optional<ghostclaw::memory::MemoryCategory>) override {
    return ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>::success({});
  }

  [[nodiscard]] ghostclaw::common::Result<bool> forget(const std::string &) override {
    return ghostclaw::common::Result<bool>::success(false);
  }

  [[nodiscard]] ghostclaw::common::Result<std::size_t> count() override {
    return ghostclaw::common::Result<std::size_t>::success(entries.size());
  }

  [[nodiscard]] ghostclaw::common::Status reindex() override {
    return ghostclaw::common::Status::success();
  }

  [[nodiscard]] bool health_check() override { return true; }

  [[nodiscard]] ghostclaw::memory::MemoryStats stats() override {
    ghostclaw::memory::MemoryStats st;
    st.total_entries = entries.size();
    return st;
  }

  std::size_t store_calls = 0;
  std::unordered_map<std::string, std::string> entries;
};

class FailingEmbedder final : public ghostclaw::memory::IEmbedder {
public:
  explicit FailingEmbedder(std::size_t dimensions) : dimensions_(dimensions) {}

  [[nodiscard]] std::string_view name() const override { return "failing"; }

  [[nodiscard]] ghostclaw::common::Result<std::vector<float>>
  embed(std::string_view) override {
    return ghostclaw::common::Result<std::vector<float>>::failure("forced embedding failure");
  }

  [[nodiscard]] ghostclaw::common::Result<std::vector<std::vector<float>>>
  embed_batch(const std::vector<std::string> &) override {
    return ghostclaw::common::Result<std::vector<std::vector<float>>>::failure(
        "forced embedding failure");
  }

  [[nodiscard]] std::size_t dimensions() const override { return dimensions_; }

private:
  std::size_t dimensions_;
};

} // namespace

void register_memory_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace mem = ghostclaw::memory;
  namespace cfg = ghostclaw::config;

  tests.push_back({"memory_category_roundtrip", [] {
                     require(mem::category_from_string("core") == mem::MemoryCategory::Core,
                             "core parse failed");
                     require(mem::category_to_string(mem::MemoryCategory::Conversation) == "conversation",
                             "category string mismatch");
                   }});

  tests.push_back({"memory_now_rfc3339_format", [] {
                     const auto ts = mem::now_rfc3339();
                     require(ts.size() == 20, "timestamp size should match RFC3339 second precision");
                     require(ts.ends_with("Z"), "timestamp should be UTC");
                   }});

  tests.push_back({"memory_recency_score_decay", [] {
                     const double fresh = mem::recency_score(mem::now_rfc3339(), 14.0);
                     const double old = mem::recency_score("2000-01-01T00:00:00Z", 14.0);
                     require(fresh > old, "fresh score should be higher");
                   }});

  tests.push_back({"embedder_local_dimensions", [] {
                     mem::LocalEmbedder embedder;
                     auto vec = embedder.embed("hello world");
                     require(vec.ok(), vec.error());
                     require(vec.value().size() == 384, "local embedding should be 384 dims");
                   }});

  tests.push_back({"embedder_noop_zeros", [] {
                     mem::NoopEmbedder embedder(32);
                     auto vec = embedder.embed("ignored");
                     require(vec.ok(), vec.error());
                     require(vec.value().size() == 32, "noop dimensions mismatch");
                     for (float value : vec.value()) {
                       require(value == 0.0F, "noop embedding should be all zeros");
                     }
                   }});

  tests.push_back({"embedder_batch_size", [] {
                     mem::LocalEmbedder embedder;
                     auto batch = embedder.embed_batch({"a", "b", "c"});
                     require(batch.ok(), batch.error());
                     require(batch.value().size() == 3, "batch count mismatch");
                   }});

  tests.push_back({"vector_index_add_search", [] {
                     mem::VectorIndex index(3);
                     auto add1 = index.add("k1", {1.0F, 0.0F, 0.0F});
                     auto add2 = index.add("k2", {0.0F, 1.0F, 0.0F});
                     require(add1.ok() && add2.ok(), "vector add failed");

                     auto results = index.search({1.0F, 0.0F, 0.0F}, 2);
                     require(results.ok(), results.error());
                     require(!results.value().empty(), "search should return entries");
                     require(results.value().front().key == "k1", "nearest key mismatch");
                   }});

  tests.push_back({"vector_index_save_load", [] {
                     const auto dir = make_temp_dir();
                     const auto path = dir / "index.bin";

                     mem::VectorIndex a(2);
                     require(a.add("x", {0.2F, 0.8F}).ok(), "add failed");
                     require(a.save(path).ok(), "save failed");

                     mem::VectorIndex b(2);
                     require(b.load(path).ok(), "load failed");
                     auto results = b.search({0.2F, 0.8F}, 1);
                     require(results.ok(), results.error());
                     require(results.value().size() == 1, "loaded index result count mismatch");
                     require(results.value()[0].key == "x", "loaded key mismatch");
                   }});

  tests.push_back({"chunker_short_text_single_chunk", [] {
                     const auto chunks = mem::chunk_text("hello world", 512, 50);
                     require(chunks.size() == 1, "short text should produce one chunk");
                     require(chunks[0].content.find("hello") != std::string::npos, "content mismatch");
                   }});

  tests.push_back({"chunker_heading_preserved", [] {
                     const std::string text = "# Title\n\nParagraph one. Paragraph two.";
                     const auto chunks = mem::chunk_text(text, 40, 5);
                     require(!chunks.empty(), "expected chunks");
                     bool saw_heading = false;
                     for (const auto &chunk : chunks) {
                       if (chunk.heading.has_value() && chunk.heading->find("# Title") != std::string::npos) {
                         saw_heading = true;
                         break;
                       }
                     }
                     require(saw_heading, "chunk heading should be preserved");
                   }});

  tests.push_back({"hybrid_ranker_combines_scores", [] {
                     mem::MemoryEntry a;
                     a.key = "a";
                     a.content = "alpha";
                     a.updated_at = mem::now_rfc3339();
                     mem::MemoryEntry b;
                     b.key = "b";
                     b.content = "beta";
                     b.updated_at = "2000-01-01T00:00:00Z";

                     std::unordered_map<std::string, mem::MemoryEntry> entries;
                     entries["a"] = a;
                     entries["b"] = b;

                     const std::vector<mem::VectorSearchResult> vectors = {
                         {.key = "a", .distance = 0.1F, .score = 0.9F},
                         {.key = "b", .distance = 0.5F, .score = 0.3F},
                     };
                     const std::vector<std::pair<std::string, double>> keywords = {{"b", 0.9}, {"a", 0.2}};

                     mem::HybridRanker ranker(0.7, 0.3, 0.1);
                     auto ranked = ranker.rank(vectors, keywords, entries, 2);
                     require(ranked.size() == 2, "ranked size mismatch");
                     require(ranked[0].final_score >= ranked[1].final_score,
                             "results should be sorted by final score");
                   }});

  tests.push_back({"markdown_memory_store_recall", [] {
                     const auto ws = make_temp_dir();
                     mem::MarkdownMemory memory(ws);
                     auto s = memory.store("k", "hello markdown memory", mem::MemoryCategory::Core);
                     require(s.ok(), s.error());

                     auto results = memory.recall("markdown", 5);
                     require(results.ok(), results.error());
                     require(!results.value().empty(), "recall should find stored content");
                   }});

  tests.push_back({"markdown_memory_forget", [] {
                     const auto ws = make_temp_dir();
                     mem::MarkdownMemory memory(ws);
                     require(memory.store("k", "to-remove", mem::MemoryCategory::Core).ok(), "store failed");
                     auto removed = memory.forget("k");
                     require(removed.ok(), removed.error());
                     require(removed.value(), "forget should report removed");
                   }});

  tests.push_back({"workspace_indexer_incremental", [] {
                     const auto ws = make_temp_dir();
                     const auto file = ws / "notes.md";
                     write_file(file, "# Header\n\nOne paragraph.");

                     CountingMemory memory;
                     mem::WorkspaceIndexer indexer(memory, ws);

                     auto first = indexer.index_file(file);
                     require(first.ok(), first.error());
                     const auto first_calls = memory.store_calls;
                     require(first_calls > 0, "first index should store chunks");

                     auto second = indexer.index_file(file);
                     require(second.ok(), second.error());
                     require(memory.store_calls == first_calls, "unchanged file should not reindex");

                     std::this_thread::sleep_for(std::chrono::milliseconds(5));
                     write_file(file, "# Header\n\nChanged content.");
                     auto third = indexer.index_file(file);
                     require(third.ok(), third.error());
                     require(memory.store_calls > first_calls, "changed file should reindex");
                   }});

  tests.push_back({"sqlite_memory_store_get_forget", [] {
                     const auto ws = make_temp_dir();
                     cfg::MemoryConfig conf;
                     conf.embedding_dimensions = 16;
                     conf.embedding_cache_size = 32;
                     conf.vector_weight = 0.7;
                     conf.keyword_weight = 0.3;

                     mem::SqliteMemory memory(ws / "brain.db", std::make_unique<mem::NoopEmbedder>(16), conf);
                     auto st = memory.store("key1", "alpha beta gamma", mem::MemoryCategory::Core);
                     require(st.ok(), st.error());

                     auto got = memory.get("key1");
                     require(got.ok(), got.error());
                     require(got.value().has_value(), "stored key should be retrievable");

                     auto count = memory.count();
                     require(count.ok(), count.error());
                     require(count.value() == 1, "count should be 1");

                     auto forgotten = memory.forget("key1");
                     require(forgotten.ok(), forgotten.error());
                     require(forgotten.value(), "forget should return true");
                   }});

  tests.push_back({"sqlite_memory_recall_and_cache_hits", [] {
                     const auto ws = make_temp_dir();
                     cfg::MemoryConfig conf;
                     conf.embedding_dimensions = 8;
                     conf.embedding_cache_size = 64;
                     conf.vector_weight = 0.7;
                     conf.keyword_weight = 0.3;

                     mem::SqliteMemory memory(ws / "brain.db", std::make_unique<mem::NoopEmbedder>(8), conf);
                     require(memory.store("a", "rocket launch checklist", mem::MemoryCategory::Core).ok(),
                             "first store failed");
                     require(memory.store("b", "rocket launch checklist", mem::MemoryCategory::Core).ok(),
                             "second store failed");

                     auto recalled = memory.recall("rocket", 5);
                     require(recalled.ok(), recalled.error());
                     require(!recalled.value().empty(), "recall should return at least one entry");

                     const auto stats = memory.stats();
                     require(stats.cache_hits >= 1, "embedding cache should record hit on repeated text");
                   }});

  tests.push_back({"sqlite_memory_store_succeeds_on_embedding_failure", [] {
                     const auto ws = make_temp_dir();
                     cfg::MemoryConfig conf;
                     conf.embedding_dimensions = 8;
                     conf.embedding_cache_size = 64;
                     conf.vector_weight = 0.7;
                     conf.keyword_weight = 0.3;

                     mem::SqliteMemory memory(ws / "brain.db",
                                              std::make_unique<FailingEmbedder>(8), conf);
                     auto stored = memory.store("name", "My name is Dian", mem::MemoryCategory::Core);
                     require(stored.ok(), "store should succeed without embeddings");

                     auto got = memory.get("name");
                     require(got.ok(), got.error());
                     require(got.value().has_value(), "stored memory should exist");
                     require(got.value()->content == "My name is Dian", "stored content mismatch");
                   }});

  tests.push_back({"sqlite_memory_recall_falls_back_when_embedding_fails", [] {
                     const auto ws = make_temp_dir();
                     cfg::MemoryConfig conf;
                     conf.embedding_dimensions = 8;
                     conf.embedding_cache_size = 64;
                     conf.vector_weight = 0.7;
                     conf.keyword_weight = 0.3;

                     mem::SqliteMemory memory(ws / "brain.db",
                                              std::make_unique<FailingEmbedder>(8), conf);
                     require(memory.store("name", "My name is Dian", mem::MemoryCategory::Core).ok(),
                             "store should succeed");

                     auto recalled = memory.recall("Dian", 5);
                     require(recalled.ok(), recalled.error());
                     require(!recalled.value().empty(),
                             "recall should return keyword/recency fallback results");
                   }});

  tests.push_back({"create_memory_factory_backend_selection", [] {
                     const auto ws = make_temp_dir();
                     cfg::Config config;
                     config.memory.backend = "markdown";
                     auto markdown = mem::create_memory(config, ws);
                     require(markdown != nullptr, "markdown factory should return memory");
                     require(markdown->name() == "markdown", "expected markdown backend");

                     config.memory.backend = "sqlite";
                     config.memory.embedding_provider = "noop";
                     config.memory.embedding_dimensions = 8;
                     auto sqlite = mem::create_memory(config, ws);
                     require(sqlite != nullptr, "sqlite factory should return memory");
                     require(sqlite->name() == "sqlite", "expected sqlite backend");
                   }});
}
