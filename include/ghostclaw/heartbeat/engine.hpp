#pragma once

#include "ghostclaw/agent/engine.hpp"
#include "ghostclaw/common/result.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

namespace ghostclaw::heartbeat {

struct HeartbeatTask {
  std::string title;
  std::string description;
};

struct HeartbeatConfig {
  bool enabled = false;
  std::chrono::minutes interval{60};
  std::filesystem::path tasks_file;
};

class HeartbeatEngine {
public:
  HeartbeatEngine(agent::AgentEngine &agent, HeartbeatConfig config);
  ~HeartbeatEngine();

  void start();
  void stop();
  [[nodiscard]] bool is_running() const;

  [[nodiscard]] static std::vector<HeartbeatTask>
  parse_heartbeat_file(const std::filesystem::path &path);

private:
  void run_loop();

  agent::AgentEngine &agent_;
  HeartbeatConfig config_;
  std::thread thread_;
  std::atomic<bool> running_{false};
};

} // namespace ghostclaw::heartbeat
