#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ghostclaw::sessions {

class SessionSendPolicy {
public:
  SessionSendPolicy(std::size_t max_events, std::chrono::seconds window);

  [[nodiscard]] bool allow(const std::string &session_id);
  void clear(const std::string &session_id);
  [[nodiscard]] std::size_t queued_in_window(const std::string &session_id) const;

private:
  using Clock = std::chrono::steady_clock;
  void prune_locked(std::deque<Clock::time_point> &events, Clock::time_point now) const;

  std::size_t max_events_ = 0;
  std::chrono::seconds window_{0};
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::deque<Clock::time_point>> events_by_session_;
};

} // namespace ghostclaw::sessions
