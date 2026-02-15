#include "ghostclaw/channels/supervisor.hpp"

#include <algorithm>
#include <iostream>
#include <system_error>
#include <thread>

namespace ghostclaw::channels {

ChannelSupervisor::ChannelSupervisor(IChannel &channel, MessageCallback callback,
                                     SupervisorConfig config)
    : channel_(channel), callback_(std::move(callback)), config_(config) {}

ChannelSupervisor::~ChannelSupervisor() { stop(); }

void ChannelSupervisor::start() {
  if (running_) {
    return;
  }
  stop_requested_ = false;
  running_ = true;
  thread_ = std::thread([this]() { run_loop(); });
}

void ChannelSupervisor::stop() {
  stop_requested_ = true;
  channel_.stop();
  if (thread_.joinable()) {
    try {
      thread_.join();
    } catch (const std::system_error &err) {
      std::cerr << "[channels] supervisor join failed: " << err.what() << "\n";
    }
  }
  running_ = false;
}

bool ChannelSupervisor::is_running() const { return running_; }

void ChannelSupervisor::run_loop() {
  auto backoff = config_.initial_backoff;
  channel_.on_message(callback_);

  while (!stop_requested_) {
    auto start_status = channel_.start();
    if (stop_requested_) {
      break;
    }

    if (!start_status.ok()) {
      std::this_thread::sleep_for(backoff);
      backoff = std::min(backoff * 2, config_.max_backoff);
      continue;
    }

    backoff = config_.initial_backoff;
    while (!stop_requested_ && channel_.health_check()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    channel_.stop();
  }

  running_ = false;
}

} // namespace ghostclaw::channels
