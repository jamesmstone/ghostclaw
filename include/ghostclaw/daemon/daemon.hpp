#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace ghostclaw::daemon {

struct DaemonOptions {
  std::string host = "127.0.0.1";
  std::uint16_t port = 8080;
};

class Daemon {
public:
  explicit Daemon(const config::Config &config);
  ~Daemon();

  [[nodiscard]] common::Status start(const DaemonOptions &options);
  void stop();
  [[nodiscard]] bool is_running() const;

private:
  const config::Config &config_;
  std::atomic<bool> running_{false};
  std::vector<std::thread> component_threads_;
};

} // namespace ghostclaw::daemon
