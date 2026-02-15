#pragma once

#include "ghostclaw/common/result.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ghostclaw::agent {

struct SessionEntry {
  std::string role;
  std::string content;
  std::string timestamp;
};

class Session {
public:
  Session(std::string id, std::filesystem::path sessions_dir);

  [[nodiscard]] common::Status append(const SessionEntry &entry);
  [[nodiscard]] common::Result<std::vector<SessionEntry>> load_history(std::size_t limit = 0) const;
  [[nodiscard]] common::Status compact(std::size_t keep_recent = 10);

  [[nodiscard]] const std::string &id() const;

private:
  std::string id_;
  std::filesystem::path file_path_;
};

} // namespace ghostclaw::agent
