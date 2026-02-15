#include "ghostclaw/security/action_tracker.hpp"

#include <algorithm>

namespace ghostclaw::security {

ActionTracker::ActionTracker(const std::uint32_t max_per_hour)
    : state_(std::make_shared<State>()), max_per_hour_(max_per_hour) {}

void ActionTracker::prune_locked(const std::chrono::steady_clock::time_point now) {
  const auto cutoff = now - std::chrono::hours(1);
  state_->actions.erase(
      std::remove_if(state_->actions.begin(), state_->actions.end(),
                     [cutoff](const auto &t) { return t < cutoff; }),
      state_->actions.end());
}

void ActionTracker::record() { record_at(std::chrono::steady_clock::now()); }

void ActionTracker::record_at(const std::chrono::steady_clock::time_point now) {
  std::lock_guard<std::mutex> lock(state_->mutex);
  prune_locked(now);
  state_->actions.push_back(now);
}

bool ActionTracker::check() { return check_at(std::chrono::steady_clock::now()); }

bool ActionTracker::check_at(const std::chrono::steady_clock::time_point now) {
  std::lock_guard<std::mutex> lock(state_->mutex);
  prune_locked(now);
  return state_->actions.size() < static_cast<std::size_t>(max_per_hour_);
}

std::size_t ActionTracker::count() { return count_at(std::chrono::steady_clock::now()); }

std::size_t ActionTracker::count_at(const std::chrono::steady_clock::time_point now) {
  std::lock_guard<std::mutex> lock(state_->mutex);
  prune_locked(now);
  return state_->actions.size();
}

} // namespace ghostclaw::security
