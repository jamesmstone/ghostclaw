#include "ghostclaw/tunnel/process.hpp"

#ifdef _WIN32
#include <tlhelp32.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ghostclaw::tunnel {

bool TunnelProcess::is_running() const {
#ifdef _WIN32
  if (process_handle == nullptr) {
    return false;
  }
  DWORD code = 0;
  if (GetExitCodeProcess(process_handle, &code) == 0) {
    return false;
  }
  return code == STILL_ACTIVE;
#else
  if (pid <= 0) {
    return false;
  }
  if (kill(pid, 0) == 0) {
    return true;
  }
  return errno == EPERM;
#endif
}

void TunnelProcess::terminate() {
#ifdef _WIN32
  if (process_handle != nullptr) {
    TerminateProcess(process_handle, 1);
    CloseHandle(process_handle);
    process_handle = nullptr;
  }
  process_id = 0;
#else
  if (pid <= 0) {
    return;
  }
  kill(pid, SIGTERM);
  for (int i = 0; i < 20; ++i) {
    int status = 0;
    const pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid) {
      pid = 0;
      return;
    }
    usleep(50 * 1000);
  }
  kill(pid, SIGKILL);
  int status = 0;
  (void)waitpid(pid, &status, 0);
  pid = 0;
#endif
}

void SharedProcess::set(TunnelProcess process) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (process_.has_value()) {
    process_->terminate();
  }
  process_ = std::move(process);
}

void SharedProcess::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  process_.reset();
}

std::optional<std::string> SharedProcess::get_url() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!process_.has_value() || process_->public_url.empty()) {
    return std::nullopt;
  }
  return process_->public_url;
}

bool SharedProcess::is_running() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return process_.has_value() && process_->is_running();
}

void SharedProcess::terminate() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (process_.has_value()) {
    process_->terminate();
    process_.reset();
  }
}

} // namespace ghostclaw::tunnel
