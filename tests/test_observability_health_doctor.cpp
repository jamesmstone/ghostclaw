#include "test_framework.hpp"

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/doctor/diagnostics.hpp"
#include "ghostclaw/health/health.hpp"
#include "ghostclaw/observability/factory.hpp"
#include "ghostclaw/observability/global.hpp"
#include "ghostclaw/observability/multi_observer.hpp"
#include "ghostclaw/observability/noop_observer.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace {

struct CounterState {
  int events = 0;
  int metrics = 0;
};

class CountingObserver final : public ghostclaw::observability::IObserver {
public:
  explicit CountingObserver(CounterState *state) : state_(state) {}

  void record_event(const ghostclaw::observability::ObserverEvent &) override { ++state_->events; }
  void record_metric(const ghostclaw::observability::ObserverMetric &) override {
    ++state_->metrics;
  }
  [[nodiscard]] std::string_view name() const override { return "counting"; }

private:
  CounterState *state_ = nullptr;
};

} // namespace

void register_observability_health_doctor_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace ob = ghostclaw::observability;
  namespace hl = ghostclaw::health;
  namespace dr = ghostclaw::doctor;

  tests.push_back({"observability_global_noop", [] {
                     ob::set_global_observer(std::make_unique<ob::NoopObserver>());
                     require(ob::get_global_observer() != nullptr, "observer should be set");
                     require(ob::get_global_observer()->name() == "noop", "expected noop observer");

                     ob::record_agent_start("openai", "gpt");
                     ob::record_tool_call("shell", std::chrono::milliseconds(5), true);
                     ob::record_metric(ob::TokensUsedMetric{.tokens = 42});

                     // Reset to prevent dangling references during static destruction
                     ob::set_global_observer(nullptr);
                   }});

  tests.push_back({"observability_multi_forwards_to_children", [] {
                     CounterState one;
                     CounterState two;
                     auto multi = std::make_unique<ob::MultiObserver>();
                     multi->add(std::make_unique<CountingObserver>(&one));
                     multi->add(std::make_unique<CountingObserver>(&two));

                     ob::set_global_observer(std::move(multi));
                     ob::record_event(ob::ErrorEvent{.component = "unit", .message = "boom"});
                     ob::record_metric(ob::QueueDepthMetric{.depth = 3});

                     require(one.events == 1 && two.events == 1, "event should be forwarded");
                     require(one.metrics == 1 && two.metrics == 1, "metric should be forwarded");

                     // Reset before local CounterState variables go out of scope
                     ob::set_global_observer(nullptr);
                   }});

  tests.push_back({"observability_factory_selects_backend", [] {
                     ghostclaw::config::Config config;
                     config.observability.backend = "none";
                     auto none = ob::create_observer(config);
                     require(none->name() == "noop", "none backend should map to noop");

                     config.observability.backend = "log";
                     auto log = ob::create_observer(config);
                     require(log->name() == "log", "log backend should map to log observer");

                     config.observability.backend = "log,noop";
                     auto multi = ob::create_observer(config);
                     require(multi->name() == "multi", "comma backend should map to multi observer");
                   }});

  tests.push_back({"health_tracks_component_state", [] {
                     hl::clear();
                     hl::mark_component_starting("gateway");
                     hl::mark_component_ok("gateway");
                     hl::bump_component_restart("gateway");
                     hl::mark_component_error("scheduler", "failed");

                     auto gateway = hl::get_component("gateway");
                     require(gateway.has_value(), "gateway should exist");
                     require(gateway->status == "ok", "gateway status should be ok");
                     require(gateway->restart_count == 1, "gateway restart count mismatch");
                     require(gateway->last_ok.has_value(), "gateway should track last_ok");

                     const auto json = hl::snapshot_json();
                     require(json.find("\"components\"") != std::string::npos,
                             "snapshot json should include components");
                     require(json.find("scheduler") != std::string::npos,
                             "snapshot json should include scheduler");
                   }});

  tests.push_back({"doctor_runs_diagnostics_report", [] {
                     ghostclaw::config::Config config;
                     config.default_provider = "custom:http://127.0.0.1:1";
                     config.api_key = "dummy";
                     config.observability.backend = "none";

                     const auto report = dr::run_diagnostics(config);
                     require(!report.checks.empty(), "doctor should return checks");
                     require(report.passed + report.failed + report.warnings ==
                                 static_cast<int>(report.checks.size()),
                             "summary counts should match checks");
                   }});
}
