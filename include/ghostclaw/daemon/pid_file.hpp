#pragma once

#include "ghostclaw/common/result.hpp"

#include <filesystem>

namespace ghostclaw::daemon {

class PidFile {
public:
  explicit PidFile(std::filesystem::path path);
  ~PidFile();

  [[nodiscard]] common::Status acquire();
  void release();

  [[nodiscard]] static bool is_process_running(int pid);

private:
  std::filesystem::path path_;
  bool acquired_ = false;
};

} // namespace ghostclaw::daemon
