#include "ghostclaw/memory/workspace_indexer.hpp"

#include "ghostclaw/memory/chunker.hpp"

#include <fstream>
#include <sstream>

namespace ghostclaw::memory {

WorkspaceIndexer::WorkspaceIndexer(IMemory &memory, std::filesystem::path workspace)
    : memory_(memory), workspace_(std::move(workspace)) {}

common::Status WorkspaceIndexer::index_file(const std::filesystem::path &path) {
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return common::Status::error("failed to stat file");
  }

  const std::string path_key = path.string();
  const auto it = file_mtimes_.find(path_key);
  if (it != file_mtimes_.end() && it->second == mtime) {
    return common::Status::success();
  }

  std::ifstream in(path);
  if (!in) {
    return common::Status::error("failed to read file");
  }

  std::stringstream buffer;
  buffer << in.rdbuf();

  const auto chunks = chunk_text(buffer.str(), 512, 50);
  std::size_t idx = 0;
  for (const auto &chunk : chunks) {
    const std::string key = "workspace:" + path.filename().string() + ":" + std::to_string(idx++);
    auto status = memory_.store(key, chunk.content, MemoryCategory::Core);
    if (!status.ok()) {
      return status;
    }
  }

  file_mtimes_[path_key] = mtime;
  return common::Status::success();
}

common::Status WorkspaceIndexer::index_workspace() {
  if (!std::filesystem::exists(workspace_)) {
    return common::Status::error("workspace missing");
  }

  for (const auto &entry : std::filesystem::recursive_directory_iterator(workspace_)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    const auto ext = entry.path().extension().string();
    if (ext != ".md" && ext != ".txt") {
      continue;
    }

    auto status = index_file(entry.path());
    if (!status.ok()) {
      return status;
    }
  }

  return common::Status::success();
}

common::Status WorkspaceIndexer::watch_for_changes() {
  // Lightweight fallback: run a single incremental pass when invoked.
  return index_workspace();
}

} // namespace ghostclaw::memory
