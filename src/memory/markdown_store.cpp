#include "ghostclaw/memory/markdown_store.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/config/config.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace ghostclaw::memory {

namespace {

std::string escape_field(std::string value) {
  for (char &ch : value) {
    if (ch == '\n') {
      ch = '\t';
    }
  }
  return value;
}

std::string unescape_field(std::string value) {
  for (char &ch : value) {
    if (ch == '\t') {
      ch = '\n';
    }
  }
  return value;
}

std::vector<std::string> split_fields(const std::string &line) {
  std::vector<std::string> parts;
  std::stringstream stream(line);
  std::string item;
  while (std::getline(stream, item, '\t')) {
    parts.push_back(item);
  }
  return parts;
}

} // namespace

MarkdownMemory::MarkdownMemory(std::filesystem::path workspace) : workspace_(std::move(workspace)) {
  std::error_code ec;
  std::filesystem::create_directories(workspace_ / "memory", ec);
}

std::string_view MarkdownMemory::name() const { return "markdown"; }

std::filesystem::path MarkdownMemory::path_for_category(const MemoryCategory category) const {
  if (category == MemoryCategory::Core) {
    return workspace_ / "MEMORY.md";
  }

  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream filename;
  filename << std::put_time(&tm, "%Y-%m-%d") << ".md";
  return workspace_ / "memory" / filename.str();
}

common::Status MarkdownMemory::append_entry(const std::filesystem::path &path,
                                            const MemoryEntry &entry) const {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    return common::Status::error("failed to create memory directory");
  }

  std::ofstream out(path, std::ios::app);
  if (!out) {
    return common::Status::error("failed to open memory file");
  }

  out << escape_field(entry.key) << '\t' << category_to_string(entry.category) << '\t'
      << escape_field(entry.created_at) << '\t' << escape_field(entry.updated_at) << '\t'
      << escape_field(entry.content) << '\n';

  return out ? common::Status::success() : common::Status::error("failed to write memory entry");
}

common::Status MarkdownMemory::store(const std::string &key, const std::string &content,
                                     const MemoryCategory category) {
  MemoryEntry entry;
  entry.key = key;
  entry.content = content;
  entry.category = category;
  entry.created_at = now_rfc3339();
  entry.updated_at = entry.created_at;
  return append_entry(path_for_category(category), entry);
}

common::Result<std::vector<MemoryEntry>> MarkdownMemory::load_all() const {
  std::vector<MemoryEntry> entries;

  auto load_file = [&entries](const std::filesystem::path &path) {
    if (!std::filesystem::exists(path)) {
      return;
    }

    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
      if (common::trim(line).empty()) {
        continue;
      }

      auto fields = split_fields(line);
      if (fields.size() < 5) {
        continue;
      }

      MemoryEntry entry;
      entry.key = unescape_field(fields[0]);
      entry.category = category_from_string(fields[1]);
      entry.created_at = unescape_field(fields[2]);
      entry.updated_at = unescape_field(fields[3]);
      entry.content = unescape_field(fields[4]);
      entries.push_back(std::move(entry));
    }
  };

  load_file(workspace_ / "MEMORY.md");

  const auto memory_dir = workspace_ / "memory";
  if (std::filesystem::exists(memory_dir)) {
    for (const auto &entry : std::filesystem::directory_iterator(memory_dir)) {
      if (entry.is_regular_file()) {
        load_file(entry.path());
      }
    }
  }

  return common::Result<std::vector<MemoryEntry>>::success(std::move(entries));
}

common::Result<std::vector<MemoryEntry>> MarkdownMemory::recall(const std::string &query,
                                                                const std::size_t limit) {
  auto all = load_all();
  if (!all.ok()) {
    return all;
  }

  std::vector<MemoryEntry> filtered;
  const std::string needle = common::to_lower(query);
  for (auto &entry : all.value()) {
    const std::string haystack = common::to_lower(entry.content + " " + entry.key);
    if (query.empty() || haystack.find(needle) != std::string::npos) {
      entry.score = query.empty() ? 1.0 : 0.5;
      filtered.push_back(std::move(entry));
    }
  }

  std::sort(filtered.begin(), filtered.end(),
            [](const auto &lhs, const auto &rhs) { return lhs.updated_at > rhs.updated_at; });

  if (filtered.size() > limit) {
    filtered.resize(limit);
  }

  return common::Result<std::vector<MemoryEntry>>::success(std::move(filtered));
}

common::Result<std::optional<MemoryEntry>> MarkdownMemory::get(const std::string &key) {
  auto all = load_all();
  if (!all.ok()) {
    return common::Result<std::optional<MemoryEntry>>::failure(all.error());
  }

  for (auto &entry : all.value()) {
    if (entry.key == key) {
      return common::Result<std::optional<MemoryEntry>>::success(std::move(entry));
    }
  }

  return common::Result<std::optional<MemoryEntry>>::success(std::nullopt);
}

common::Result<std::vector<MemoryEntry>>
MarkdownMemory::list(const std::optional<MemoryCategory> category) {
  auto all = load_all();
  if (!all.ok()) {
    return all;
  }

  if (category.has_value()) {
    std::vector<MemoryEntry> filtered;
    for (auto &entry : all.value()) {
      if (entry.category == *category) {
        filtered.push_back(std::move(entry));
      }
    }
    return common::Result<std::vector<MemoryEntry>>::success(std::move(filtered));
  }

  return all;
}

common::Result<bool> MarkdownMemory::forget(const std::string &key) {
  auto all = load_all();
  if (!all.ok()) {
    return common::Result<bool>::failure(all.error());
  }

  const auto before = all.value().size();
  std::erase_if(all.value(), [&key](const auto &entry) { return entry.key == key; });
  const bool removed = all.value().size() != before;

  std::ofstream overwrite_core(workspace_ / "MEMORY.md", std::ios::trunc);
  overwrite_core.close();

  std::error_code ec;
  const auto memory_dir = workspace_ / "memory";
  if (std::filesystem::exists(memory_dir)) {
    for (const auto &entry : std::filesystem::directory_iterator(memory_dir)) {
      if (entry.is_regular_file()) {
        std::ofstream clear(entry.path(), std::ios::trunc);
      }
    }
  }

  for (const auto &entry : all.value()) {
    auto status = append_entry(path_for_category(entry.category), entry);
    if (!status.ok()) {
      return common::Result<bool>::failure(status.error());
    }
  }

  return common::Result<bool>::success(removed);
}

common::Result<std::size_t> MarkdownMemory::count() {
  auto all = load_all();
  if (!all.ok()) {
    return common::Result<std::size_t>::failure(all.error());
  }
  return common::Result<std::size_t>::success(all.value().size());
}

common::Status MarkdownMemory::reindex() { return common::Status::success(); }

bool MarkdownMemory::health_check() {
  std::error_code ec;
  std::filesystem::create_directories(workspace_, ec);
  return !ec;
}

MemoryStats MarkdownMemory::stats() {
  MemoryStats stat;
  auto counted = count();
  if (counted.ok()) {
    stat.total_entries = counted.value();
  }
  return stat;
}

} // namespace ghostclaw::memory
