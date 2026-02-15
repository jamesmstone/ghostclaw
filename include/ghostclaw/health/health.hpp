#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace ghostclaw::health {

struct ComponentStatus {
  std::string status = "unknown";
  std::size_t restart_count = 0;
  std::optional<std::string> last_error;
  std::string updated_at;
  std::optional<std::string> last_ok;
};

struct HealthSnapshot {
  std::unordered_map<std::string, ComponentStatus> components;
};

void mark_component_starting(const std::string &name);
void mark_component_ok(const std::string &name);
void mark_component_error(const std::string &name, const std::string &error);
void bump_component_restart(const std::string &name);
void reset_component(const std::string &name);

[[nodiscard]] std::optional<ComponentStatus> get_component(const std::string &name);
[[nodiscard]] HealthSnapshot snapshot();
[[nodiscard]] std::string snapshot_json();
void clear();

} // namespace ghostclaw::health
