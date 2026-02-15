#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace ghostclaw::agent {

enum class QueueMode { Steer, Followup, Collect };

struct QueuedMessage {
  std::string content;
  std::string sender;
  std::string channel;
  std::chrono::steady_clock::time_point received_at;
};

class MessageQueue {
public:
  explicit MessageQueue(QueueMode mode);

  void push(QueuedMessage message);
  [[nodiscard]] std::optional<QueuedMessage> pop();
  [[nodiscard]] std::vector<QueuedMessage> pop_all();
  [[nodiscard]] bool empty() const;

private:
  QueueMode mode_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<QueuedMessage> queue_;
};

} // namespace ghostclaw::agent
