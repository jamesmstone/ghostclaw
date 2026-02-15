#pragma once

#include "ghostclaw/observability/observer.hpp"

#include <memory>

namespace ghostclaw::observability {

void set_global_observer(std::unique_ptr<IObserver> observer);
IObserver *get_global_observer();

void record_event(const ObserverEvent &event);
void record_metric(const ObserverMetric &metric);

void record_agent_start(const std::string &provider, const std::string &model);
void record_agent_end(std::chrono::milliseconds duration,
                      std::optional<std::uint64_t> tokens = std::nullopt);
void record_tool_call(const std::string &tool, std::chrono::milliseconds duration, bool success);
void record_channel_message(const std::string &channel, const std::string &direction);
void record_heartbeat_tick();
void record_error(const std::string &component, const std::string &message);

} // namespace ghostclaw::observability
