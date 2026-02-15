#include "ghostclaw/heartbeat/engine.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/health/health.hpp"
#include "ghostclaw/observability/global.hpp"

#include <algorithm>
#include <fstream>

namespace ghostclaw::heartbeat {

HeartbeatEngine::HeartbeatEngine(agent::AgentEngine &agent, HeartbeatConfig config)
    : agent_(agent), config_(std::move(config)) {}

HeartbeatEngine::~HeartbeatEngine() { stop(); }

void HeartbeatEngine::start() {
  if (running_ || !config_.enabled) {
    return;
  }
  running_ = true;
  thread_ = std::thread([this]() { run_loop(); });
}

void HeartbeatEngine::stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool HeartbeatEngine::is_running() const { return running_; }

std::vector<HeartbeatTask>
HeartbeatEngine::parse_heartbeat_file(const std::filesystem::path &path) {
  std::vector<HeartbeatTask> tasks;
  if (!std::filesystem::exists(path)) {
    return tasks;
  }

  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    const std::string trimmed = common::trim(line);
    if (trimmed.empty()) {
      continue;
    }
    if (common::starts_with(trimmed, "- ") || common::starts_with(trimmed, "* ")) {
      HeartbeatTask task;
      task.title = trimmed.substr(2);
      task.description = task.title;
      tasks.push_back(std::move(task));
    }
  }
  return tasks;
}

void HeartbeatEngine::run_loop() {
  while (running_) {
    observability::record_heartbeat_tick();
    health::mark_component_ok("heartbeat");
    const auto tasks = parse_heartbeat_file(config_.tasks_file);
    for (const auto &task : tasks) {
      if (!running_) {
        break;
      }
      (void)agent_.run(task.description);
    }
    const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(config_.interval);
    const auto steps = std::max<long long>(1, wait_ms.count() / 100);
    for (long long i = 0; i < steps && running_; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

} // namespace ghostclaw::heartbeat
