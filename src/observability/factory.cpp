#include "ghostclaw/observability/factory.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/observability/log_observer.hpp"
#include "ghostclaw/observability/multi_observer.hpp"
#include "ghostclaw/observability/noop_observer.hpp"

#include <sstream>

namespace ghostclaw::observability {

std::unique_ptr<IObserver> create_observer(const config::Config &config) {
  const std::string backend = common::to_lower(common::trim(config.observability.backend));
  if (backend.empty() || backend == "none" || backend == "noop") {
    return std::make_unique<NoopObserver>();
  }

  if (backend == "log") {
    return std::make_unique<LogObserver>();
  }

  if (backend.find(',') != std::string::npos) {
    auto multi = std::make_unique<MultiObserver>();
    std::stringstream stream(backend);
    std::string part;
    while (std::getline(stream, part, ',')) {
      const std::string p = common::to_lower(common::trim(part));
      if (p == "log") {
        multi->add(std::make_unique<LogObserver>());
      } else if (p == "noop" || p == "none") {
        multi->add(std::make_unique<NoopObserver>());
      }
    }
    return multi;
  }

  return std::make_unique<LogObserver>();
}

} // namespace ghostclaw::observability
