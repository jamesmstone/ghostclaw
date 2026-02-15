#include "ghostclaw/doctor/diagnostics.hpp"

#include "ghostclaw/channels/channel_manager.hpp"
#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/config/config.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/providers/factory.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#endif

namespace ghostclaw::doctor {

namespace {

void add_check(DiagnosticsReport &report, DiagnosticCheck check) {
  switch (check.status) {
  case CheckStatus::Pass:
    ++report.passed;
    break;
  case CheckStatus::Fail:
    ++report.failed;
    break;
  case CheckStatus::Warn:
    ++report.warnings;
    break;
  }
  report.checks.push_back(std::move(check));
}

DiagnosticCheck check_config(const config::Config &config) {
  DiagnosticCheck check;
  check.name = "Config";
  auto validation = config::validate_config(config);
  if (!validation.ok()) {
    check.status = CheckStatus::Fail;
    check.message = validation.error();
    return check;
  }

  if (!validation.value().empty()) {
    check.status = CheckStatus::Warn;
    check.message = validation.value().front();
    return check;
  }

  check.status = CheckStatus::Pass;
  check.message = "valid";
  return check;
}

DiagnosticCheck check_api_key(const config::Config &config) {
  DiagnosticCheck check;
  check.name = "API Key";
  const bool has_cfg = config.api_key.has_value() && !common::trim(*config.api_key).empty();
  const char *env_key = std::getenv("GHOSTCLAW_API_KEY");
  const bool has_env = env_key != nullptr && *env_key != '\0';

  if (has_cfg || has_env) {
    check.status = CheckStatus::Pass;
    check.message = has_cfg ? "configured" : "from environment";
  } else {
    check.status = CheckStatus::Fail;
    check.message = "missing (set config.api_key or GHOSTCLAW_API_KEY)";
  }
  return check;
}

DiagnosticCheck check_provider(const config::Config &config) {
  DiagnosticCheck check;
  check.name = "Provider";

  const auto provider =
      providers::create_provider(config.default_provider, config.api_key, std::make_shared<providers::CurlHttpClient>());
  if (!provider.ok()) {
    check.status = CheckStatus::Fail;
    check.message = provider.error();
    return check;
  }

  const auto start = std::chrono::steady_clock::now();
  const auto warmup = provider.value()->warmup();
  const auto end = std::chrono::steady_clock::now();
  check.latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  if (!warmup.ok()) {
    check.status = CheckStatus::Fail;
    check.message = warmup.error();
    return check;
  }

  check.status = CheckStatus::Pass;
  check.message = provider.value()->name();
  return check;
}

DiagnosticCheck check_memory(const config::Config &config) {
  DiagnosticCheck check;
  check.name = "Memory";

  auto workspace = config::workspace_dir();
  if (!workspace.ok()) {
    check.status = CheckStatus::Fail;
    check.message = workspace.error();
    return check;
  }

  auto memory = memory::create_memory(config, workspace.value());
  if (memory == nullptr) {
    check.status = CheckStatus::Fail;
    check.message = "failed to create memory backend";
    return check;
  }

  if (!memory->health_check()) {
    check.status = CheckStatus::Fail;
    check.message = "health check failed";
    return check;
  }

  const auto stats = memory->stats();
  check.status = CheckStatus::Pass;
  check.message = std::string(memory->name()) + " entries=" + std::to_string(stats.total_entries);
  return check;
}

std::vector<DiagnosticCheck> check_channels(const config::Config &config) {
  std::vector<DiagnosticCheck> checks;
  auto manager = channels::create_channel_manager(config);
  for (const auto &name : manager->list_channels()) {
    DiagnosticCheck check;
    check.name = "Channel:" + name;
    auto *channel = manager->get_channel(name);
    if (channel == nullptr) {
      check.status = CheckStatus::Fail;
      check.message = "not available";
    } else if (channel->health_check()) {
      check.status = CheckStatus::Pass;
      check.message = "ok";
    } else {
      check.status = CheckStatus::Warn;
      check.message = "degraded";
    }
    checks.push_back(std::move(check));
  }
  return checks;
}

DiagnosticCheck check_daemon() {
  DiagnosticCheck check;
  check.name = "Daemon";

  auto cfg_dir = config::config_dir();
  if (!cfg_dir.ok()) {
    check.status = CheckStatus::Fail;
    check.message = cfg_dir.error();
    return check;
  }

  const auto pid_path = cfg_dir.value() / "daemon.pid";
  if (!std::filesystem::exists(pid_path)) {
    check.status = CheckStatus::Warn;
    check.message = "not running";
    return check;
  }

  std::ifstream in(pid_path);
  std::string pid_text;
  std::getline(in, pid_text);
  const std::string trimmed = common::trim(pid_text);
  if (trimmed.empty()) {
    check.status = CheckStatus::Warn;
    check.message = "stale pid file";
    return check;
  }

#ifdef _WIN32
  check.status = CheckStatus::Pass;
  check.message = "pid file present";
#else
  pid_t pid = 0;
  try {
    pid = static_cast<pid_t>(std::stoll(trimmed));
  } catch (...) {
    check.status = CheckStatus::Warn;
    check.message = "invalid pid file";
    return check;
  }

  if (pid > 0 && kill(pid, 0) == 0) {
    check.status = CheckStatus::Pass;
    check.message = "running (pid=" + std::to_string(pid) + ")";
  } else {
    check.status = CheckStatus::Warn;
    check.message = (errno == ESRCH) ? "not running (stale pid file)" : "unknown";
  }
#endif
  return check;
}

} // namespace

DiagnosticsReport run_diagnostics(const config::Config &config) {
  DiagnosticsReport report;

  add_check(report, check_config(config));
  add_check(report, check_api_key(config));
  add_check(report, check_provider(config));
  add_check(report, check_memory(config));

  for (auto &check : check_channels(config)) {
    add_check(report, std::move(check));
  }

  add_check(report, check_daemon());
  return report;
}

void print_diagnostics_report(const DiagnosticsReport &report) {
  auto status_prefix = [](CheckStatus status) -> const char * {
    switch (status) {
    case CheckStatus::Pass:
      return "[PASS]";
    case CheckStatus::Fail:
      return "[FAIL]";
    case CheckStatus::Warn:
      return "[WARN]";
    }
    return "[INFO]";
  };

  for (const auto &check : report.checks) {
    std::cout << status_prefix(check.status) << " " << check.name << ": " << check.message;
    if (check.latency.has_value()) {
      std::cout << " (" << check.latency->count() << "ms)";
    }
    std::cout << "\n";
  }

  std::cout << "Summary: " << report.passed << " passed, " << report.failed << " failed, "
            << report.warnings << " warnings\n";
}

} // namespace ghostclaw::doctor
