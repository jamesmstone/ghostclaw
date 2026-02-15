#include "ghostclaw/sessions/send_policy.hpp"

#include "ghostclaw/common/fs.hpp"

namespace ghostclaw::sessions {

SessionSendPolicy::SessionSendPolicy(const std::size_t max_events,
                                     const std::chrono::seconds window)
    : max_events_(max_events), window_(window) {}

void SessionSendPolicy::prune_locked(std::deque<Clock::time_point> &events,
                                     const Clock::time_point now) const {
  if (window_.count() <= 0) {
    events.clear();
    return;
  }
  const auto cutoff = now - window_;
  while (!events.empty() && events.front() < cutoff) {
    events.pop_front();
  }
}

bool SessionSendPolicy::allow(const std::string &session_id) {
  if (max_events_ == 0 || window_.count() <= 0) {
    return true;
  }
  const std::string key = common::trim(session_id).empty() ? "default" : common::trim(session_id);
  const auto now = Clock::now();

  std::lock_guard<std::mutex> lock(mutex_);
  auto &events = events_by_session_[key];
  prune_locked(events, now);
  if (events.size() >= max_events_) {
    return false;
  }
  events.push_back(now);
  return true;
}

void SessionSendPolicy::clear(const std::string &session_id) {
  const std::string key = common::trim(session_id).empty() ? "default" : common::trim(session_id);
  std::lock_guard<std::mutex> lock(mutex_);
  events_by_session_.erase(key);
}

std::size_t SessionSendPolicy::queued_in_window(const std::string &session_id) const {
  const std::string key = common::trim(session_id).empty() ? "default" : common::trim(session_id);
  const auto now = Clock::now();
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = events_by_session_.find(key);
  if (it == events_by_session_.end()) {
    return 0;
  }
  auto events = it->second;
  prune_locked(events, now);
  return events.size();
}

} // namespace ghostclaw::sessions
