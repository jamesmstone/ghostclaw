#pragma once

#include "ghostclaw/memory/memory.hpp"

#include <filesystem>
#include <unordered_map>

namespace ghostclaw::memory {

class WorkspaceIndexer {
public:
  WorkspaceIndexer(IMemory &memory, std::filesystem::path workspace);

  [[nodiscard]] common::Status index_file(const std::filesystem::path &path);
  [[nodiscard]] common::Status index_workspace();
  [[nodiscard]] common::Status watch_for_changes();

private:
  IMemory &memory_;
  std::filesystem::path workspace_;
  std::unordered_map<std::string, std::filesystem::file_time_type> file_mtimes_;
};

} // namespace ghostclaw::memory
