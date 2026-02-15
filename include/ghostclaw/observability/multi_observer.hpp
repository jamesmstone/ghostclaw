#pragma once

#include "ghostclaw/observability/observer.hpp"

#include <memory>
#include <vector>

namespace ghostclaw::observability {

class MultiObserver final : public IObserver {
public:
  void add(std::unique_ptr<IObserver> observer);

  void record_event(const ObserverEvent &event) override;
  void record_metric(const ObserverMetric &metric) override;
  void flush() override;
  [[nodiscard]] std::string_view name() const override { return "multi"; }

private:
  std::vector<std::unique_ptr<IObserver>> observers_;
};

} // namespace ghostclaw::observability
