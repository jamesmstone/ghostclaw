#pragma once

#include "ghostclaw/observability/observer.hpp"

namespace ghostclaw::observability {

class NoopObserver final : public IObserver {
public:
  void record_event(const ObserverEvent &) override {}
  void record_metric(const ObserverMetric &) override {}
  [[nodiscard]] std::string_view name() const override { return "noop"; }
};

} // namespace ghostclaw::observability
