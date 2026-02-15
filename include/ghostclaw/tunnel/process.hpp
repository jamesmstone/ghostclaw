#pragma once

#include <mutex>
#include <optional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

namespace ghostclaw::tunnel {

struct TunnelProcess {
#ifdef _WIN32
  HANDLE process_handle = nullptr;
  DWORD process_id = 0;
#else
  pid_t pid = 0;
#endif
  std::string public_url;

  [[nodiscard]] bool is_running() const;
  void terminate();
};

class SharedProcess {
public:
  void set(TunnelProcess process);
  void clear();
  [[nodiscard]] std::optional<std::string> get_url() const;
  [[nodiscard]] bool is_running() const;
  void terminate();

private:
  mutable std::mutex mutex_;
  std::optional<TunnelProcess> process_;
};

} // namespace ghostclaw::tunnel
