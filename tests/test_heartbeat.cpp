#include "test_framework.hpp"

#include "ghostclaw/agent/engine.hpp"
#include "ghostclaw/heartbeat/cron.hpp"
#include "ghostclaw/heartbeat/cron_store.hpp"
#include "ghostclaw/heartbeat/engine.hpp"
#include "ghostclaw/heartbeat/scheduler.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/tools/tool_registry.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <thread>

namespace {

std::filesystem::path make_temp_dir() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto base = std::filesystem::temp_directory_path() /
                    ("ghostclaw-heartbeat-test-" + std::to_string(rng()));
  std::filesystem::create_directories(base);
  return base;
}

class FakeMemory final : public ghostclaw::memory::IMemory {
public:
  [[nodiscard]] std::string_view name() const override { return "fake"; }
  [[nodiscard]] ghostclaw::common::Status store(const std::string &, const std::string &,
                                                ghostclaw::memory::MemoryCategory) override {
    ++store_calls;
    return ghostclaw::common::Status::success();
  }
  [[nodiscard]] ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>
  recall(const std::string &, std::size_t) override {
    return ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>::success({});
  }
  [[nodiscard]] ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>
  get(const std::string &) override {
    return ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>::success(
        std::nullopt);
  }
  [[nodiscard]] ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>
  list(std::optional<ghostclaw::memory::MemoryCategory>) override {
    return ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>::success({});
  }
  [[nodiscard]] ghostclaw::common::Result<bool> forget(const std::string &) override {
    return ghostclaw::common::Result<bool>::success(false);
  }
  [[nodiscard]] ghostclaw::common::Result<std::size_t> count() override {
    return ghostclaw::common::Result<std::size_t>::success(0);
  }
  [[nodiscard]] ghostclaw::common::Status reindex() override {
    return ghostclaw::common::Status::success();
  }
  [[nodiscard]] bool health_check() override { return true; }
  [[nodiscard]] ghostclaw::memory::MemoryStats stats() override { return {}; }

  std::size_t store_calls = 0;
};

class CountingProvider final : public ghostclaw::providers::Provider {
public:
  [[nodiscard]] ghostclaw::common::Result<std::string>
  chat(const std::string &, const std::string &, double) override {
    ++calls;
    return ghostclaw::common::Result<std::string>::success("ok");
  }
  [[nodiscard]] ghostclaw::common::Result<std::string>
  chat_with_system(const std::optional<std::string> &, const std::string &, const std::string &,
                   double) override {
    ++calls;
    return ghostclaw::common::Result<std::string>::success("ok");
  }
  [[nodiscard]] ghostclaw::common::Status warmup() override {
    return ghostclaw::common::Status::success();
  }
  [[nodiscard]] std::string name() const override { return "counting"; }

  std::size_t calls = 0;
};

std::shared_ptr<ghostclaw::agent::AgentEngine>
make_engine(const ghostclaw::config::Config &config, const std::filesystem::path &workspace,
            std::shared_ptr<CountingProvider> provider, FakeMemory **memory_out = nullptr) {
  auto memory = std::make_unique<FakeMemory>();
  auto *raw_memory = memory.get();
  ghostclaw::tools::ToolRegistry registry;
  auto engine = std::make_shared<ghostclaw::agent::AgentEngine>(config, provider, std::move(memory),
                                                                 std::move(registry), workspace);
  if (memory_out != nullptr) {
    *memory_out = raw_memory;
  }
  return engine;
}

} // namespace

void register_heartbeat_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace hb = ghostclaw::heartbeat;

  tests.push_back({"heartbeat_cron_parse_and_next", [] {
                     auto expr = hb::CronExpression::parse("*/5 * * * *");
                     require(expr.ok(), expr.error());
                     auto next = expr.value().next_occurrence(std::chrono::system_clock::now());
                     require(next > std::chrono::system_clock::now(), "next occurrence should be future");
                   }});

  tests.push_back({"heartbeat_cron_store_add_list_remove", [] {
                     const auto dir = make_temp_dir();
                     hb::CronStore store(dir / "jobs.db");

                     hb::CronJob job;
                     job.id = "job1";
                     job.expression = "* * * * *";
                     job.command = "echo hi";
                     job.next_run = std::chrono::system_clock::now();
                     require(store.add_job(job).ok(), "add should succeed");

                     auto listed = store.list_jobs();
                     require(listed.ok(), listed.error());
                     require(listed.value().size() == 1, "list should return one job");

                     auto removed = store.remove_job("job1");
                     require(removed.ok(), removed.error());
                     require(removed.value(), "remove should return true");
                   }});

  tests.push_back({"heartbeat_scheduler_executes_due_jobs", [] {
                     const auto dir = make_temp_dir();
                     ghostclaw::config::Config config;
                     config.memory.auto_save = false;
                     auto provider = std::make_shared<CountingProvider>();
                     auto engine = make_engine(config, dir, provider);

                     hb::CronStore store(dir / "jobs.db");
                     hb::CronJob job;
                     job.id = "due-job";
                     job.expression = "* * * * *";
                     job.command = "run scheduled";
                     job.next_run = std::chrono::system_clock::now() - std::chrono::seconds(1);
                     require(store.add_job(job).ok(), "failed to add due job");

                     hb::SchedulerConfig scheduler_config;
                     scheduler_config.poll_interval = std::chrono::milliseconds(100);
                     scheduler_config.max_retries = 0;

                     hb::Scheduler scheduler(store, *engine, scheduler_config);
                     scheduler.start();
                     std::this_thread::sleep_for(std::chrono::milliseconds(350));
                     scheduler.stop();

                     require(provider->calls >= 1, "scheduler should execute due job");

                     auto listed = store.list_jobs();
                     require(listed.ok(), listed.error());
                     require(!listed.value().empty(), "job should still exist");
                     require(listed.value()[0].last_run.has_value(),
                             "job should record last_run after execution");
                   }});

  tests.push_back({"heartbeat_scheduler_dispatches_channel_message_payload", [] {
                     const auto dir = make_temp_dir();
                     ghostclaw::config::Config config;
                     config.memory.auto_save = false;
                     auto provider = std::make_shared<CountingProvider>();
                     auto engine = make_engine(config, dir, provider);

                     hb::CronStore store(dir / "jobs.db");
                     hb::CronJob job;
                     job.id = "dispatch-job";
                     job.expression = "* * * * *";
                     job.command =
                         R"({"kind":"channel_message","channel":"cli","to":"test-user","text":"scheduled ping","id":"dispatch-job"})";
                     job.next_run = std::chrono::system_clock::now() - std::chrono::seconds(1);
                     require(store.add_job(job).ok(), "failed to add due dispatch job");

                     hb::SchedulerConfig scheduler_config;
                     scheduler_config.poll_interval = std::chrono::milliseconds(100);
                     scheduler_config.max_retries = 0;

                     hb::Scheduler scheduler(store, *engine, scheduler_config, &config);
                     scheduler.start();
                     std::this_thread::sleep_for(std::chrono::milliseconds(350));
                     scheduler.stop();

                     require(provider->calls == 0, "dispatch payload should bypass agent.run");
                     auto listed = store.list_jobs();
                     require(listed.ok(), listed.error());
                     require(!listed.value().empty(), "job should still exist");
                     require(listed.value()[0].last_status.has_value(),
                             "job should record dispatch status");
                     require(listed.value()[0].last_status.value() == "ok",
                             "dispatch status should be ok");
                   }});

  tests.push_back({"heartbeat_engine_parses_markdown_tasks", [] {
                     const auto dir = make_temp_dir();
                     const auto file = dir / "HEARTBEAT.md";
                     std::ofstream out(file);
                     out << "# Heartbeat\n\n- Check inbox\n* Summarize changes\n";
                     out.close();

                     auto tasks = hb::HeartbeatEngine::parse_heartbeat_file(file);
                     require(tasks.size() == 2, "expected two parsed tasks");
                    require(tasks[0].description.find("Check inbox") != std::string::npos,
                            "first task mismatch");
                  }});

  // ============================================
  // NEW TESTS: Cron Expression Edge Cases
  // ============================================

  tests.push_back({"heartbeat_cron_every_minute", [] {
                     auto expr = hb::CronExpression::parse("* * * * *");
                     require(expr.ok(), expr.error());
                     auto now = std::chrono::system_clock::now();
                     auto next = expr.value().next_occurrence(now);
                     auto diff = std::chrono::duration_cast<std::chrono::seconds>(next - now);
                     require(diff.count() <= 60, "next occurrence should be within 60 seconds");
                   }});

  tests.push_back({"heartbeat_cron_hourly", [] {
                     auto expr = hb::CronExpression::parse("0 * * * *");
                     require(expr.ok(), expr.error());
                     auto next = expr.value().next_occurrence(std::chrono::system_clock::now());
                     require(next > std::chrono::system_clock::now(), "next should be in future");
                   }});

  tests.push_back({"heartbeat_cron_daily_at_midnight", [] {
                     auto expr = hb::CronExpression::parse("0 0 * * *");
                     require(expr.ok(), expr.error());
                     auto next = expr.value().next_occurrence(std::chrono::system_clock::now());
                     require(next > std::chrono::system_clock::now(), "next should be in future");
                   }});

  tests.push_back({"heartbeat_cron_invalid_expression", [] {
                     auto invalid1 = hb::CronExpression::parse("invalid");
                     require(!invalid1.ok(), "invalid expression should fail");

                     auto invalid2 = hb::CronExpression::parse("* * *");
                     require(!invalid2.ok(), "incomplete expression should fail");

                     auto invalid3 = hb::CronExpression::parse("60 * * * *");
                     require(!invalid3.ok(), "out of range minute should fail");
                   }});

  tests.push_back({"heartbeat_cron_specific_day_of_week", [] {
                     auto expr = hb::CronExpression::parse("0 9 * * 1"); // Monday at 9am
                     require(expr.ok(), expr.error());
                     auto next = expr.value().next_occurrence(std::chrono::system_clock::now());
                     require(next > std::chrono::system_clock::now(), "next should be in future");
                   }});

  // ============================================
  // NEW TESTS: Cron Store Operations
  // ============================================

  tests.push_back({"heartbeat_cron_store_update_after_run", [] {
                     const auto dir = make_temp_dir();
                     hb::CronStore store(dir / "jobs.db");

                     hb::CronJob job;
                     job.id = "update-test";
                     job.expression = "* * * * *";
                     job.command = "original";
                     job.next_run = std::chrono::system_clock::now() - std::chrono::seconds(10);
                     require(store.add_job(job).ok(), "add should succeed");

                     auto new_next = std::chrono::system_clock::now() + std::chrono::hours(1);
                     require(store.update_after_run("update-test", "success", new_next).ok(),
                             "update_after_run should succeed");

                     auto listed = store.list_jobs();
                     require(listed.ok(), listed.error());
                     require(listed.value().size() == 1, "should have one job");
                   }});

  tests.push_back({"heartbeat_cron_store_get_due_jobs", [] {
                     const auto dir = make_temp_dir();
                     hb::CronStore store(dir / "jobs.db");

                     hb::CronJob due_job;
                     due_job.id = "due-job";
                     due_job.expression = "* * * * *";
                     due_job.command = "due command";
                     due_job.next_run = std::chrono::system_clock::now() - std::chrono::seconds(10);
                     require(store.add_job(due_job).ok(), "add due job should succeed");

                     hb::CronJob future_job;
                     future_job.id = "future-job";
                     future_job.expression = "* * * * *";
                     future_job.command = "future command";
                     future_job.next_run = std::chrono::system_clock::now() + std::chrono::hours(1);
                     require(store.add_job(future_job).ok(), "add future job should succeed");

                     auto due = store.get_due_jobs();
                     require(due.ok(), due.error());
                     require(due.value().size() == 1, "should have one due job");
                     require(due.value()[0].id == "due-job", "due job id should match");
                   }});

  tests.push_back({"heartbeat_cron_store_list_returns_all", [] {
                     const auto dir = make_temp_dir();
                     hb::CronStore store(dir / "jobs.db");

                     auto listed = store.list_jobs();
                     require(listed.ok(), listed.error());
                     require(listed.value().empty(), "empty store should return empty list");
                   }});

  tests.push_back({"heartbeat_cron_store_remove_nonexistent", [] {
                     const auto dir = make_temp_dir();
                     hb::CronStore store(dir / "jobs.db");

                     auto removed = store.remove_job("nonexistent");
                     require(removed.ok(), removed.error());
                     require(!removed.value(), "remove nonexistent should return false");
                   }});

  tests.push_back({"heartbeat_cron_store_multiple_jobs", [] {
                     const auto dir = make_temp_dir();
                     hb::CronStore store(dir / "jobs.db");

                     for (int i = 0; i < 5; ++i) {
                       hb::CronJob job;
                       job.id = "job-" + std::to_string(i);
                       job.expression = "* * * * *";
                       job.command = "cmd-" + std::to_string(i);
                       job.next_run = std::chrono::system_clock::now();
                       require(store.add_job(job).ok(), "add should succeed");
                     }

                     auto listed = store.list_jobs();
                     require(listed.ok(), listed.error());
                     require(listed.value().size() == 5, "should have 5 jobs");
                   }});

  // ============================================
  // NEW TESTS: Scheduler Behavior
  // ============================================

  tests.push_back({"heartbeat_scheduler_skips_future_jobs", [] {
                     const auto dir = make_temp_dir();
                     ghostclaw::config::Config config;
                     config.memory.auto_save = false;
                     auto provider = std::make_shared<CountingProvider>();
                     auto engine = make_engine(config, dir, provider);

                     hb::CronStore store(dir / "jobs.db");
                     hb::CronJob job;
                     job.id = "future-job";
                     job.expression = "* * * * *";
                     job.command = "should not run";
                     job.next_run = std::chrono::system_clock::now() + std::chrono::hours(1);
                     require(store.add_job(job).ok(), "add should succeed");

                     hb::SchedulerConfig scheduler_config;
                     scheduler_config.poll_interval = std::chrono::milliseconds(50);

                     hb::Scheduler scheduler(store, *engine, scheduler_config);
                     scheduler.start();
                     std::this_thread::sleep_for(std::chrono::milliseconds(200));
                     scheduler.stop();

                     require(provider->calls == 0, "future job should not be executed");
                   }});

  tests.push_back({"heartbeat_scheduler_start_stop_idempotent", [] {
                     const auto dir = make_temp_dir();
                     ghostclaw::config::Config config;
                     config.memory.auto_save = false;
                     auto provider = std::make_shared<CountingProvider>();
                     auto engine = make_engine(config, dir, provider);

                     hb::CronStore store(dir / "jobs.db");
                     hb::SchedulerConfig scheduler_config;
                     scheduler_config.poll_interval = std::chrono::milliseconds(50);

                     hb::Scheduler scheduler(store, *engine, scheduler_config);
                     scheduler.start();
                     scheduler.start(); // Should not crash
                     scheduler.stop();
                     scheduler.stop(); // Should not crash
                   }});

  // ============================================
  // NEW TESTS: Heartbeat Engine
  // ============================================

  tests.push_back({"heartbeat_engine_empty_file", [] {
                     const auto dir = make_temp_dir();
                     const auto file = dir / "EMPTY.md";
                     std::ofstream out(file);
                     out << "";
                     out.close();

                     auto tasks = hb::HeartbeatEngine::parse_heartbeat_file(file);
                     require(tasks.empty(), "empty file should return no tasks");
                   }});

  tests.push_back({"heartbeat_engine_no_tasks", [] {
                     const auto dir = make_temp_dir();
                     const auto file = dir / "NOTASKS.md";
                     std::ofstream out(file);
                     out << "# Heartbeat\n\nNo tasks here, just text.\n";
                     out.close();

                     auto tasks = hb::HeartbeatEngine::parse_heartbeat_file(file);
                     require(tasks.empty(), "file without list items should return no tasks");
                   }});

  tests.push_back({"heartbeat_engine_mixed_content", [] {
                     const auto dir = make_temp_dir();
                     const auto file = dir / "MIXED.md";
                     std::ofstream out(file);
                     out << "# Heartbeat\n\n";
                     out << "Some intro text.\n\n";
                     out << "- Task one\n";
                     out << "More text.\n";
                     out << "* Task two\n";
                     out << "## Section\n";
                     out << "- Task three\n";
                     out.close();

                     auto tasks = hb::HeartbeatEngine::parse_heartbeat_file(file);
                     require(tasks.size() == 3, "should find 3 tasks");
                   }});
}
