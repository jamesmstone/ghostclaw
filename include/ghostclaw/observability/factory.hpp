#pragma once

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/observability/observer.hpp"

#include <memory>

namespace ghostclaw::observability {

[[nodiscard]] std::unique_ptr<IObserver> create_observer(const config::Config &config);

} // namespace ghostclaw::observability
