#include "ghostclaw/memory/memory.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/config/config.hpp"
#include "ghostclaw/memory/markdown_store.hpp"
#include "ghostclaw/memory/sqlite_store.hpp"
#include "ghostclaw/memory/embedder.hpp"

#include <cmath>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ghostclaw::memory {

std::string category_to_string(const MemoryCategory category) {
  switch (category) {
  case MemoryCategory::Core:
    return "core";
  case MemoryCategory::Daily:
    return "daily";
  case MemoryCategory::Conversation:
    return "conversation";
  case MemoryCategory::Custom:
    return "custom";
  }
  return "core";
}

MemoryCategory category_from_string(std::string_view value) {
  const std::string v = common::to_lower(std::string(value));
  if (v == "core") {
    return MemoryCategory::Core;
  }
  if (v == "daily") {
    return MemoryCategory::Daily;
  }
  if (v == "conversation") {
    return MemoryCategory::Conversation;
  }
  return MemoryCategory::Custom;
}

std::string now_rfc3339() {
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif

  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

double recency_score(const std::string &updated_at, const double half_life_days) {
  std::tm tm{};
  std::istringstream in(updated_at);
  in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  if (in.fail()) {
    return 0.0;
  }

#ifdef _WIN32
  const std::time_t updated_time = _mkgmtime(&tm);
#else
  const std::time_t updated_time = timegm(&tm);
#endif

  const auto updated = std::chrono::system_clock::from_time_t(updated_time);
  const auto now = std::chrono::system_clock::now();
  const auto age = now - updated;
  const double days =
      std::chrono::duration_cast<std::chrono::duration<double>>(age).count() / 86400.0;
  if (half_life_days <= 0.0) {
    return 0.0;
  }
  return std::exp(-days / half_life_days);
}

std::unique_ptr<IMemory> create_memory(const config::Config &config,
                                       const std::filesystem::path &workspace) {
  const std::string backend = common::to_lower(config.memory.backend);
  if (backend == "sqlite") {
    auto embedder = create_embedder(config);
    return std::make_unique<SqliteMemory>(workspace / "memory" / "brain.db", std::move(embedder),
                                          config.memory);
  }

  return std::make_unique<MarkdownMemory>(workspace);
}

} // namespace ghostclaw::memory
