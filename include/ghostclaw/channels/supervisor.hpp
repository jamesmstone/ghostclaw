#pragma once

#include "ghostclaw/channels/channel.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace ghostclaw::channels {

struct SupervisorConfig {
  std::chrono::milliseconds initial_backoff{2000};
  std::chrono::milliseconds max_backoff{60000};
};

class ChannelSupervisor {
public:
  ChannelSupervisor(IChannel &channel, MessageCallback callback,
                    SupervisorConfig config = {});
  ~ChannelSupervisor();

  void start();
  void stop();
  [[nodiscard]] bool is_running() const;

private:
  void run_loop();

  IChannel &channel_;
  MessageCallback callback_;
  SupervisorConfig config_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
};

} // namespace ghostclaw::channels
