#pragma once

#include "ghostclaw/observability/observer.hpp"

namespace ghostclaw::observability {

class LogObserver final : public IObserver {
public:
  void record_event(const ObserverEvent &event) override;
  void record_metric(const ObserverMetric &metric) override;
  [[nodiscard]] std::string_view name() const override { return "log"; }
};

} // namespace ghostclaw::observability
