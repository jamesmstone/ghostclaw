#include "ghostclaw/agent/message_queue.hpp"

namespace ghostclaw::agent {

MessageQueue::MessageQueue(const QueueMode mode) : mode_(mode) {}

void MessageQueue::push(QueuedMessage message) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push(std::move(message));
  cv_.notify_one();
}

std::optional<QueuedMessage> MessageQueue::pop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) {
    return std::nullopt;
  }

  auto value = queue_.front();
  queue_.pop();
  return value;
}

std::vector<QueuedMessage> MessageQueue::pop_all() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<QueuedMessage> out;

  if (mode_ == QueueMode::Collect) {
    while (!queue_.empty()) {
      out.push_back(queue_.front());
      queue_.pop();
    }
    return out;
  }

  if (!queue_.empty()) {
    out.push_back(queue_.front());
    queue_.pop();
  }
  return out;
}

bool MessageQueue::empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.empty();
}

} // namespace ghostclaw::agent
