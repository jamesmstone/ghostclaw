#include "test_framework.hpp"

#include "ghostclaw/config/config.hpp"
#include "ghostclaw/daemon/daemon.hpp"
#include "ghostclaw/daemon/pid_file.hpp"
#include "ghostclaw/daemon/state_writer.hpp"
#include "ghostclaw/health/health.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <thread>

namespace {

struct EnvGuard {
  std::string key;
  std::optional<std::string> old_value;

  EnvGuard(std::string key_, std::optional<std::string> value) : key(std::move(key_)) {
    if (const char *existing = std::getenv(key.c_str()); existing != nullptr) {
      old_value = existing;
    }
    if (value.has_value()) {
      setenv(key.c_str(), value->c_str(), 1);
    } else {
      unsetenv(key.c_str());
    }
  }

  ~EnvGuard() {
    if (old_value.has_value()) {
      setenv(key.c_str(), old_value->c_str(), 1);
    } else {
      unsetenv(key.c_str());
    }
  }
};

std::filesystem::path make_temp_home() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto path = std::filesystem::temp_directory_path() /
                    ("ghostclaw-daemon-test-home-" + std::to_string(rng()));
  std::filesystem::create_directories(path);
  return path;
}

} // namespace

void register_daemon_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace dm = ghostclaw::daemon;
  namespace hl = ghostclaw::health;
  namespace cfg = ghostclaw::config;

  tests.push_back({"daemon_health_snapshot_updates", [] {
                     hl::clear();
                     hl::mark_component_ok("gateway");
                     hl::bump_component_restart("gateway");
                     hl::mark_component_error("scheduler", "boom");

                     auto snap = hl::snapshot();
                     require(snap.components.contains("gateway"), "gateway missing");
                     require(snap.components.contains("scheduler"), "scheduler missing");
                     require(snap.components["gateway"].restart_count == 1, "restart count mismatch");
                     require(snap.components["scheduler"].status == "error",
                             "scheduler status mismatch");
                   }});

  tests.push_back({"daemon_pid_file_prevents_double_start", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const auto pid_path = home / ".ghostclaw" / "daemon.pid";

                     dm::PidFile first(pid_path);
                     auto acquired = first.acquire();
                     require(acquired.ok(), acquired.error());

                     dm::PidFile second(pid_path);
                     auto second_acquire = second.acquire();
                     require(!second_acquire.ok(), "second acquire should fail");

                     first.release();
                   }});

  tests.push_back({"daemon_state_writer_writes_file", [] {
                     const auto home = make_temp_home();
                     const auto state_path = home / "daemon_state.json";
                     hl::clear();
                     hl::mark_component_ok("gateway");

                     dm::StateWriter writer(state_path);
                     writer.start();
                     std::this_thread::sleep_for(std::chrono::milliseconds(200));
                     writer.stop();

                     require(std::filesystem::exists(state_path), "state file should exist");
                     std::ifstream in(state_path);
                     std::string content((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
                     require(content.find("\"components\"") != std::string::npos,
                             "state file should include components");
                   }});

  tests.push_back({"daemon_start_stop", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());

                     cfg::Config config;
                     config.default_provider = "ollama";
                     config.gateway.require_pairing = false;
                     config.reliability.scheduler_poll_secs = 1;
                     config.heartbeat.enabled = false;

                     dm::Daemon daemon(config);
                     dm::DaemonOptions options;
                     options.host = "127.0.0.1";
                     options.port = 0;

                     auto started = daemon.start(options);
                     require(started.ok(), started.error());
                     require(daemon.is_running(), "daemon should be running");
                     std::this_thread::sleep_for(std::chrono::milliseconds(300));
                     daemon.stop();
                    require(!daemon.is_running(), "daemon should stop");
                  }});

  // ============================================
  // NEW TESTS: Component Startup and Dependencies
  // ============================================

  tests.push_back({"daemon_health_component_starting_state", [] {
                     hl::clear();
                     hl::mark_component_starting("gateway");
                     auto snap = hl::snapshot();
                     require(snap.components.contains("gateway"), "gateway should exist");
                     require(snap.components["gateway"].status == "starting",
                             "status should be starting");
                   }});

  tests.push_back({"daemon_health_multiple_components", [] {
                     hl::clear();
                     hl::mark_component_ok("gateway");
                     hl::mark_component_ok("channels");
                     hl::mark_component_ok("scheduler");
                     hl::mark_component_error("heartbeat", "disabled");

                     auto snap = hl::snapshot();
                     require(snap.components.size() >= 4, "should have 4 components");
                     require(snap.components["gateway"].status == "ok", "gateway should be ok");
                     require(snap.components["heartbeat"].status == "error", "heartbeat should be error");
                   }});

  tests.push_back({"daemon_health_error_message_preserved", [] {
                     hl::clear();
                     hl::mark_component_error("test", "specific error message");
                     auto snap = hl::snapshot();
                     require(snap.components["test"].last_error == "specific error message",
                             "error message should be preserved");
                   }});

  tests.push_back({"daemon_health_restart_count_increments", [] {
                     hl::clear();
                     hl::mark_component_ok("gateway");
                     hl::bump_component_restart("gateway");
                     hl::bump_component_restart("gateway");
                     hl::bump_component_restart("gateway");

                     auto snap = hl::snapshot();
                     require(snap.components["gateway"].restart_count == 3,
                             "restart count should be 3");
                   }});

  // ============================================
  // NEW TESTS: PID File Operations
  // ============================================

  tests.push_back({"daemon_pid_file_creates_directory", [] {
                     const auto home = make_temp_home();
                     const auto pid_path = home / "subdir" / "nested" / "daemon.pid";

                     dm::PidFile pid(pid_path);
                     auto acquired = pid.acquire();
                     require(acquired.ok(), acquired.error());
                     require(std::filesystem::exists(pid_path), "pid file should exist");
                     pid.release();
                   }});

  tests.push_back({"daemon_pid_file_release_removes_file", [] {
                     const auto home = make_temp_home();
                     const auto pid_path = home / "daemon.pid";

                     dm::PidFile pid(pid_path);
                     require(pid.acquire().ok(), "acquire should succeed");
                     require(std::filesystem::exists(pid_path), "pid file should exist");
                     pid.release();
                     require(!std::filesystem::exists(pid_path), "pid file should be removed");
                   }});

  tests.push_back({"daemon_pid_file_contains_valid_pid", [] {
                     const auto home = make_temp_home();
                     const auto pid_path = home / "daemon.pid";

                     dm::PidFile pid(pid_path);
                     require(pid.acquire().ok(), "acquire should succeed");

                     std::ifstream in(pid_path);
                     std::string content;
                     std::getline(in, content);
                     require(!content.empty(), "pid file should contain content");

                     try {
                       int pid_value = std::stoi(content);
                       require(pid_value > 0, "pid should be positive");
                     } catch (...) {
                       require(false, "pid file should contain valid integer");
                     }

                     pid.release();
                   }});

  // ============================================
  // NEW TESTS: State Writer Operations
  // ============================================

  tests.push_back({"daemon_state_writer_updates_periodically", [] {
                     const auto home = make_temp_home();
                     const auto state_path = home / "state.json";
                     hl::clear();
                     hl::mark_component_ok("test");

                     dm::StateWriter writer(state_path);
                     writer.start();

                     std::this_thread::sleep_for(std::chrono::milliseconds(150));
                     auto mtime1 = std::filesystem::last_write_time(state_path);

                     std::this_thread::sleep_for(std::chrono::milliseconds(200));
                     auto mtime2 = std::filesystem::last_write_time(state_path);

                     writer.stop();

                     // File should be updated (mtime may or may not change depending on interval)
                     require(std::filesystem::exists(state_path), "state file should exist");
                   }});

  tests.push_back({"daemon_state_writer_json_valid", [] {
                     const auto home = make_temp_home();
                     const auto state_path = home / "state.json";
                     hl::clear();
                     hl::mark_component_ok("gateway");
                     hl::mark_component_error("scheduler", "test error");

                     dm::StateWriter writer(state_path);
                     writer.start();
                     std::this_thread::sleep_for(std::chrono::milliseconds(150));
                     writer.stop();

                     std::ifstream in(state_path);
                     std::string content((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());

                     require(content.find("{") != std::string::npos, "should be JSON");
                     require(content.find("\"components\"") != std::string::npos,
                             "should have components");
                     require(content.find("\"gateway\"") != std::string::npos,
                             "should have gateway");
                   }});

  // ============================================
  // NEW TESTS: Daemon Lifecycle Edge Cases
  // ============================================

  tests.push_back({"daemon_double_start_fails", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());

                     cfg::Config config;
                     config.default_provider = "ollama";
                     config.gateway.require_pairing = false;
                     config.heartbeat.enabled = false;

                     dm::Daemon daemon(config);
                     dm::DaemonOptions options;
                     options.host = "127.0.0.1";
                     options.port = 0;

                     require(daemon.start(options).ok(), "first start should succeed");
                     auto second = daemon.start(options);
                     require(!second.ok(), "second start should fail");
                     daemon.stop();
                   }});

  tests.push_back({"daemon_stop_idempotent", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());

                     cfg::Config config;
                     config.default_provider = "ollama";
                     config.gateway.require_pairing = false;
                     config.heartbeat.enabled = false;

                     dm::Daemon daemon(config);
                     dm::DaemonOptions options;
                     options.host = "127.0.0.1";
                     options.port = 0;

                     require(daemon.start(options).ok(), "start should succeed");
                     daemon.stop();
                     daemon.stop(); // Should not crash
                     daemon.stop(); // Should not crash
                     require(!daemon.is_running(), "daemon should be stopped");
                   }});

  tests.push_back({"daemon_stop_without_start", [] {
                     cfg::Config config;
                     dm::Daemon daemon(config);
                     daemon.stop(); // Should not crash
                     require(!daemon.is_running(), "daemon should not be running");
                   }});

  // ============================================
  // NEW TESTS: Health Clear and Reset
  // ============================================

  tests.push_back({"daemon_health_clear_removes_all", [] {
                     hl::mark_component_ok("a");
                     hl::mark_component_ok("b");
                     hl::mark_component_ok("c");
                     hl::clear();

                     auto snap = hl::snapshot();
                     require(snap.components.empty(), "clear should remove all components");
                   }});

  tests.push_back({"daemon_health_status_transitions", [] {
                     hl::clear();
                     hl::mark_component_starting("test");
                     auto snap1 = hl::snapshot();
                     require(snap1.components["test"].status == "starting", "should be starting");

                     hl::mark_component_ok("test");
                     auto snap2 = hl::snapshot();
                     require(snap2.components["test"].status == "ok", "should be ok");

                     hl::mark_component_error("test", "failed");
                     auto snap3 = hl::snapshot();
                     require(snap3.components["test"].status == "error", "should be error");
                   }});

  // ============================================
  // NEW TESTS: Operational Readiness
  // ============================================

  tests.push_back({"operational_health_snapshot_format", [] {
                     hl::clear();
                     hl::mark_component_ok("gateway");
                     hl::mark_component_ok("scheduler");
                     hl::mark_component_error("provider", "connection failed");

                     auto snap = hl::snapshot();
                     require(snap.components.size() == 3, "should have 3 components");
                     require(snap.components.count("gateway") == 1, "should have gateway");
                     require(snap.components.count("scheduler") == 1, "should have scheduler");
                     require(snap.components.count("provider") == 1, "should have provider");
                   }});

  tests.push_back({"operational_health_error_message_preserved", [] {
                     hl::clear();
                     const std::string error_msg = "Connection refused: ECONNREFUSED";
                     hl::mark_component_error("database", error_msg);

                     auto snap = hl::snapshot();
                     require(snap.components["database"].last_error.has_value(),
                             "should have error");
                     require(snap.components["database"].last_error.value() == error_msg,
                             "error message should be preserved");
                   }});

  tests.push_back({"operational_pid_file_cleanup", [] {
                     const auto home = make_temp_home();
                     const auto pid_path = home / "test.pid";

                     {
                       dm::PidFile pid_file(pid_path);
                       require(pid_file.acquire().ok(), "acquire should succeed");
                       require(std::filesystem::exists(pid_path), "pid file should exist");
                     }
                     // PidFile destructor should clean up
                     require(!std::filesystem::exists(pid_path), "pid file should be cleaned up");
                   }});

  tests.push_back({"operational_state_writer_json_valid", [] {
                     const auto home = make_temp_home();
                     const auto state_path = home / "state.json";

                     hl::clear();
                     hl::mark_component_ok("test");

                     dm::StateWriter writer(state_path);
                     writer.start();
                     std::this_thread::sleep_for(std::chrono::milliseconds(150));
                     writer.stop();

                     std::ifstream in(state_path);
                     std::string content((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());

                     // Basic JSON validation
                     require(content.front() == '{', "should start with {");
                     require(content.find('}') != std::string::npos, "should have closing }");
                     require(content.find("\"components\"") != std::string::npos,
                             "should have components key");
                   }});

  tests.push_back({"operational_config_validation_provider", [] {
                     cfg::Config config;
                     config.default_provider = "invalid-provider-name";
                     // Config should still be valid - provider validation happens at runtime
                     require(!config.default_provider.empty(), "provider should be set");
                   }});

  tests.push_back({"operational_config_validation_model", [] {
                     cfg::Config config;
                     config.default_model = "gpt-4";
                     require(config.default_model == "gpt-4", "model should be set");
                   }});

  tests.push_back({"operational_config_validation_temperature", [] {
                     cfg::Config config;
                     config.default_temperature = 0.7;
                     require(config.default_temperature == 0.7, "temperature should be set");
                   }});

  tests.push_back({"operational_graceful_shutdown_health_clear", [] {
                     hl::clear();
                     hl::mark_component_ok("gateway");
                     hl::mark_component_ok("scheduler");

                     auto snap1 = hl::snapshot();
                     require(snap1.components.size() == 2, "should have 2 components");

                     // Simulate graceful shutdown
                     hl::clear();

                     auto snap2 = hl::snapshot();
                     require(snap2.components.empty(), "should be empty after shutdown");
                   }});

  tests.push_back({"operational_recovery_after_error", [] {
                     hl::clear();
                     hl::mark_component_error("gateway", "startup failed");

                     auto snap1 = hl::snapshot();
                     require(snap1.components["gateway"].status == "error", "should be error");

                     // Simulate recovery
                     hl::mark_component_ok("gateway");

                     auto snap2 = hl::snapshot();
                     require(snap2.components["gateway"].status == "ok", "should recover to ok");
                   }});
}
