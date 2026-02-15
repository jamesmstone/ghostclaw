#include "ghostclaw/observability/multi_observer.hpp"

namespace ghostclaw::observability {

void MultiObserver::add(std::unique_ptr<IObserver> observer) {
  if (observer != nullptr) {
    observers_.push_back(std::move(observer));
  }
}

void MultiObserver::record_event(const ObserverEvent &event) {
  for (auto &observer : observers_) {
    observer->record_event(event);
  }
}

void MultiObserver::record_metric(const ObserverMetric &metric) {
  for (auto &observer : observers_) {
    observer->record_metric(metric);
  }
}

void MultiObserver::flush() {
  for (auto &observer : observers_) {
    observer->flush();
  }
}

} // namespace ghostclaw::observability
