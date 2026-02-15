#include "ghostclaw/observability/global.hpp"

#include <mutex>

namespace ghostclaw::observability {

namespace {

std::mutex g_observer_mutex;
std::unique_ptr<IObserver> g_observer;

} // namespace

void set_global_observer(std::unique_ptr<IObserver> observer) {
  std::lock_guard<std::mutex> lock(g_observer_mutex);
  g_observer = std::move(observer);
}

IObserver *get_global_observer() {
  std::lock_guard<std::mutex> lock(g_observer_mutex);
  return g_observer.get();
}

void record_event(const ObserverEvent &event) {
  if (auto *observer = get_global_observer(); observer != nullptr) {
    observer->record_event(event);
  }
}

void record_metric(const ObserverMetric &metric) {
  if (auto *observer = get_global_observer(); observer != nullptr) {
    observer->record_metric(metric);
  }
}

void record_agent_start(const std::string &provider, const std::string &model) {
  record_event(AgentStartEvent{.provider = provider, .model = model});
}

void record_agent_end(std::chrono::milliseconds duration, std::optional<std::uint64_t> tokens) {
  record_event(AgentEndEvent{.duration = duration, .tokens_used = tokens});
}

void record_tool_call(const std::string &tool, std::chrono::milliseconds duration,
                      const bool success) {
  record_event(ToolCallEvent{.tool = tool, .duration = duration, .success = success});
}

void record_channel_message(const std::string &channel, const std::string &direction) {
  record_event(ChannelMessageEvent{.channel = channel, .direction = direction});
}

void record_heartbeat_tick() { record_event(HeartbeatTickEvent{}); }

void record_error(const std::string &component, const std::string &message) {
  record_event(ErrorEvent{.component = component, .message = message});
}

} // namespace ghostclaw::observability
