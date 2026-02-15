#include "ghostclaw/tools/builtin/sessions.hpp"

#include "sessions_internal.hpp"

#include <sstream>

namespace ghostclaw::tools {

SessionsListTool::SessionsListTool(std::shared_ptr<sessions::SessionStore> store)
    : store_(std::move(store)) {}

std::string_view SessionsListTool::name() const { return "sessions_list"; }

std::string_view SessionsListTool::description() const {
  return "List active sessions with optional group filter";
}

std::string SessionsListTool::parameters_schema() const {
  return R"({"type":"object","properties":{"group_id":{"type":"string"},"limit":{"type":"integer","minimum":1},"include_subagents":{"type":"boolean"}}})";
}

common::Result<ToolResult> SessionsListTool::execute(const ToolArgs &args, const ToolContext &ctx) {
  using namespace builtin::sessions_internal;

  auto handle = resolve_store(store_, ctx);
  if (handle.store == nullptr) {
    return common::Result<ToolResult>::failure("session store unavailable");
  }

  std::vector<sessions::SessionState> sessions_out;
  if (const auto group_id = optional_arg(args, "group_id"); group_id.has_value()) {
    auto listed = handle.store->list_states_by_group(*group_id);
    if (!listed.ok()) {
      return common::Result<ToolResult>::failure(listed.error());
    }
    sessions_out = std::move(listed.value());
  } else {
    auto listed = handle.store->list_states();
    if (!listed.ok()) {
      return common::Result<ToolResult>::failure(listed.error());
    }
    sessions_out = std::move(listed.value());
  }

  const std::size_t limit = parse_size_arg(args, "limit", 100, 1000);
  if (sessions_out.size() > limit) {
    sessions_out.resize(limit);
  }

  const bool include_subagents = parse_bool_arg(args, "include_subagents", true);

  std::ostringstream json;
  json << "{";
  json << "\"count\":" << sessions_out.size() << ",";
  json << "\"sessions\":[";
  for (std::size_t i = 0; i < sessions_out.size(); ++i) {
    if (i > 0) {
      json << ",";
    }
    if (include_subagents) {
      json << session_state_json(sessions_out[i]);
    } else {
      auto copy = sessions_out[i];
      copy.subagents.clear();
      json << session_state_json(copy);
    }
  }
  json << "]";
  json << "}";

  ToolResult result;
  result.output = json.str();
  result.metadata["count"] = std::to_string(sessions_out.size());
  result.metadata["group"] = optional_arg(args, "group_id").value_or("");
  return common::Result<ToolResult>::success(std::move(result));
}

bool SessionsListTool::is_safe() const { return true; }

std::string_view SessionsListTool::group() const { return "sessions"; }

} // namespace ghostclaw::tools
