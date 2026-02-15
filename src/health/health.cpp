#include "ghostclaw/health/health.hpp"

#include "ghostclaw/memory/memory.hpp"

#include <mutex>
#include <sstream>

namespace ghostclaw::health {

namespace {

std::mutex g_mutex;
std::unordered_map<std::string, ComponentStatus> g_components;

ComponentStatus &ensure_component(const std::string &name) { return g_components[name]; }

std::string now() { return memory::now_rfc3339(); }

} // namespace

void mark_component_starting(const std::string &name) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto &component = ensure_component(name);
  component.status = "starting";
  component.updated_at = now();
  component.last_error.reset();
}

void mark_component_ok(const std::string &name) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto &component = ensure_component(name);
  component.status = "ok";
  component.updated_at = now();
  component.last_ok = component.updated_at;
  component.last_error.reset();
}

void mark_component_error(const std::string &name, const std::string &error) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto &component = ensure_component(name);
  component.status = "error";
  component.updated_at = now();
  component.last_error = error;
}

void bump_component_restart(const std::string &name) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto &component = ensure_component(name);
  ++component.restart_count;
  component.updated_at = now();
}

void reset_component(const std::string &name) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_components.erase(name);
}

std::optional<ComponentStatus> get_component(const std::string &name) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto it = g_components.find(name);
  if (it == g_components.end()) {
    return std::nullopt;
  }
  return it->second;
}

HealthSnapshot snapshot() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return HealthSnapshot{.components = g_components};
}

std::string snapshot_json() {
  const auto snap = snapshot();
  std::ostringstream json;
  json << "{\"components\":{";
  bool first = true;
  for (const auto &[name, status] : snap.components) {
    if (!first) {
      json << ",";
    }
    first = false;
    json << "\"" << name << "\":{";
    json << "\"status\":\"" << status.status << "\",";
    json << "\"restart_count\":" << status.restart_count;
    if (!status.updated_at.empty()) {
      json << ",\"updated_at\":\"" << status.updated_at << "\"";
    }
    if (status.last_ok.has_value()) {
      json << ",\"last_ok\":\"" << *status.last_ok << "\"";
    }
    if (status.last_error.has_value()) {
      json << ",\"last_error\":\"" << *status.last_error << "\"";
    }
    json << "}";
  }
  json << "}}";
  return json.str();
}

void clear() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_components.clear();
}

} // namespace ghostclaw::health
