#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

namespace ghostclaw::daemon {

class StateWriter {
public:
  explicit StateWriter(std::filesystem::path state_file);
  ~StateWriter();

  void start();
  void stop();
  [[nodiscard]] bool is_running() const;

private:
  void write_loop();
  void write_state() const;

  std::filesystem::path state_file_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::chrono::steady_clock::time_point started_at_{};
};

} // namespace ghostclaw::daemon
