#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace ghostclaw::observability {

struct AgentStartEvent {
  std::string provider;
  std::string model;
};

struct AgentEndEvent {
  std::chrono::milliseconds duration{0};
  std::optional<std::uint64_t> tokens_used;
};

struct ToolCallEvent {
  std::string tool;
  std::chrono::milliseconds duration{0};
  bool success = false;
};

struct ChannelMessageEvent {
  std::string channel;
  std::string direction;
};

struct HeartbeatTickEvent {};

struct ErrorEvent {
  std::string component;
  std::string message;
};

using ObserverEvent =
    std::variant<AgentStartEvent, AgentEndEvent, ToolCallEvent, ChannelMessageEvent,
                 HeartbeatTickEvent, ErrorEvent>;

struct RequestLatencyMetric {
  std::chrono::milliseconds latency{0};
};

struct TokensUsedMetric {
  std::uint64_t tokens = 0;
};

struct ActiveSessionsMetric {
  std::uint64_t count = 0;
};

struct QueueDepthMetric {
  std::uint64_t depth = 0;
};

using ObserverMetric =
    std::variant<RequestLatencyMetric, TokensUsedMetric, ActiveSessionsMetric, QueueDepthMetric>;

class IObserver {
public:
  virtual ~IObserver() = default;

  virtual void record_event(const ObserverEvent &event) = 0;
  virtual void record_metric(const ObserverMetric &metric) = 0;
  virtual void flush() {}
  [[nodiscard]] virtual std::string_view name() const = 0;
};

} // namespace ghostclaw::observability
