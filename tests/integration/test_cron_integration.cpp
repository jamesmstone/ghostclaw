#include "test_framework.hpp"

#include "ghostclaw/agent/engine.hpp"
#include "ghostclaw/heartbeat/cron_store.hpp"
#include "ghostclaw/heartbeat/scheduler.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/tools/tool_registry.hpp"
#include "tests/helpers/test_helpers.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <thread>

namespace {

class NullMemory final : public ghostclaw::memory::IMemory {
public:
  [[nodiscard]] std::string_view name() const override { return "null"; }
  [[nodiscard]] ghostclaw::common::Status store(const std::string &, const std::string &,
                                                ghostclaw::memory::MemoryCategory) override {
    return ghostclaw::common::Status::success();
  }
  [[nodiscard]] ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>
  recall(const std::string &, std::size_t) override {
    return ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>::success({});
  }
  [[nodiscard]] ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>
  get(const std::string &) override {
    return ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>::success(std::nullopt);
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
};

} // namespace

void register_cron_integration_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace hb = ghostclaw::heartbeat;

  tests.push_back({"cron_integration_scheduler_runs_job", [] {
                     ghostclaw::testing::TempWorkspace workspace;
                     auto config = ghostclaw::testing::temp_config(workspace);
                     config.memory.auto_save = false;

                     auto provider = std::make_shared<ghostclaw::testing::MockProvider>();
                     provider->set_response("scheduled-ok");

                     ghostclaw::tools::ToolRegistry registry;
                     auto memory = std::make_unique<NullMemory>();
                     ghostclaw::agent::AgentEngine engine(config, provider, std::move(memory),
                                                          std::move(registry), workspace.path());

                     hb::CronStore store(workspace.path() / "cron" / "jobs.db");
                     hb::CronJob job;
                     job.id = "integration-job";
                     job.expression = "* * * * *";
                     job.command = "ping";
                     job.next_run = std::chrono::system_clock::now() - std::chrono::seconds(1);
                     require(store.add_job(job).ok(), "failed to add cron job");

                     hb::SchedulerConfig scheduler_config;
                     scheduler_config.poll_interval = std::chrono::milliseconds(100);
                     scheduler_config.max_retries = 0;

                     hb::Scheduler scheduler(store, engine, scheduler_config);
                     scheduler.start();
                     std::this_thread::sleep_for(std::chrono::milliseconds(350));
                     scheduler.stop();

                     auto listed = store.list_jobs();
                     require(listed.ok(), listed.error());
                     require(!listed.value().empty(), "job should remain stored");
                     require(listed.value().front().last_run.has_value(), "job should have executed");
                   }});
}
