#include "ghostclaw/daemon/pid_file.hpp"

#include <fstream>

#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#endif

namespace ghostclaw::daemon {

PidFile::PidFile(std::filesystem::path path) : path_(std::move(path)) {}

PidFile::~PidFile() { release(); }

common::Status PidFile::acquire() {
  if (acquired_) {
    return common::Status::success();
  }

  std::error_code ec;
  std::filesystem::create_directories(path_.parent_path(), ec);
  if (ec) {
    return common::Status::error("failed to create pid directory: " + ec.message());
  }

  if (std::filesystem::exists(path_)) {
    std::ifstream in(path_);
    int existing_pid = 0;
    in >> existing_pid;
    if (existing_pid > 0 && is_process_running(existing_pid)) {
      return common::Status::error("daemon already running with pid " +
                                   std::to_string(existing_pid));
    }
    std::filesystem::remove(path_, ec);
  }

  int pid = 0;
#ifdef _WIN32
  pid = static_cast<int>(GetCurrentProcessId());
#else
  pid = static_cast<int>(getpid());
#endif

  std::ofstream out(path_, std::ios::trunc);
  if (!out) {
    return common::Status::error("failed to write pid file");
  }
  out << pid << "\n";
  acquired_ = true;
  return common::Status::success();
}

void PidFile::release() {
  if (!acquired_) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove(path_, ec);
  acquired_ = false;
}

bool PidFile::is_process_running(const int pid) {
  if (pid <= 0) {
    return false;
  }
#ifdef _WIN32
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
  if (process != nullptr) {
    CloseHandle(process);
    return true;
  }
  return false;
#else
  return kill(pid, 0) == 0;
#endif
}

} // namespace ghostclaw::daemon
