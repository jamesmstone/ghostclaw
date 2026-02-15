#include "ghostclaw/observability/log_observer.hpp"

#include "ghostclaw/common/fs.hpp"

#include <iostream>
#include <type_traits>

namespace ghostclaw::observability {

namespace {

void log_line(const std::string &level, const std::string &message) {
  std::cerr << "[" << level << "] " << message << "\n";
}

} // namespace

void LogObserver::record_event(const ObserverEvent &event) {
  std::visit(
      [](auto &&evt) {
        using T = std::decay_t<decltype(evt)>;
        if constexpr (std::is_same_v<T, AgentStartEvent>) {
          log_line("INFO", "agent.start provider=" + evt.provider + " model=" + evt.model);
        } else if constexpr (std::is_same_v<T, AgentEndEvent>) {
          log_line("INFO", "agent.end duration_ms=" + std::to_string(evt.duration.count()));
        } else if constexpr (std::is_same_v<T, ToolCallEvent>) {
          log_line("INFO", "tool.call name=" + evt.tool +
                               " success=" + (evt.success ? std::string("true") : std::string("false")));
        } else if constexpr (std::is_same_v<T, ChannelMessageEvent>) {
          log_line("DEBUG", "channel.message channel=" + evt.channel + " direction=" + evt.direction);
        } else if constexpr (std::is_same_v<T, HeartbeatTickEvent>) {
          log_line("DEBUG", "heartbeat.tick");
        } else if constexpr (std::is_same_v<T, ErrorEvent>) {
          log_line("ERROR", evt.component + ": " + evt.message);
        }
      },
      event);
}

void LogObserver::record_metric(const ObserverMetric &metric) {
  std::visit(
      [](auto &&m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, RequestLatencyMetric>) {
          log_line("DEBUG", "metric.request_latency_ms=" + std::to_string(m.latency.count()));
        } else if constexpr (std::is_same_v<T, TokensUsedMetric>) {
          log_line("DEBUG", "metric.tokens_used=" + std::to_string(m.tokens));
        } else if constexpr (std::is_same_v<T, ActiveSessionsMetric>) {
          log_line("DEBUG", "metric.active_sessions=" + std::to_string(m.count));
        } else if constexpr (std::is_same_v<T, QueueDepthMetric>) {
          log_line("DEBUG", "metric.queue_depth=" + std::to_string(m.depth));
        }
      },
      metric);
}

} // namespace ghostclaw::observability
