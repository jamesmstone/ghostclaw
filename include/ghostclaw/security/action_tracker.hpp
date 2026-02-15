#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <memory>
#include <vector>

namespace ghostclaw::security {

class ActionTracker {
public:
  explicit ActionTracker(std::uint32_t max_per_hour);

  void record();
  void record_at(std::chrono::steady_clock::time_point now);

  [[nodiscard]] bool check();
  [[nodiscard]] bool check_at(std::chrono::steady_clock::time_point now);

  [[nodiscard]] std::size_t count();
  [[nodiscard]] std::size_t count_at(std::chrono::steady_clock::time_point now);

private:
  struct State {
    std::mutex mutex;
    std::vector<std::chrono::steady_clock::time_point> actions;
  };

  void prune_locked(std::chrono::steady_clock::time_point now);

  std::shared_ptr<State> state_;
  std::uint32_t max_per_hour_;
};

} // namespace ghostclaw::security
