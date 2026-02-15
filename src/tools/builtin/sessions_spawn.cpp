#include "ghostclaw/tools/builtin/sessions.hpp"

#include "sessions_internal.hpp"

#include <sstream>

namespace ghostclaw::tools {

SessionsSpawnTool::SessionsSpawnTool(std::shared_ptr<sessions::SessionStore> store)
    : store_(std::move(store)) {}

std::string_view SessionsSpawnTool::name() const { return "sessions_spawn"; }

std::string_view SessionsSpawnTool::description() const {
  return "Spawn a sub-agent session for an isolated task";
}

std::string SessionsSpawnTool::parameters_schema() const {
  return R"({"type":"object","required":["task"],"properties":{"task":{"type":"string"},"parent_session_id":{"type":"string"},"label":{"type":"string"},"model":{"type":"string"},"thinking_level":{"type":"string"}}})";
}

common::Result<ToolResult> SessionsSpawnTool::execute(const ToolArgs &args, const ToolContext &ctx) {
  using namespace builtin::sessions_internal;

  auto task = required_arg(args, "task");
  if (!task.ok()) {
    return common::Result<ToolResult>::failure(task.error());
  }

  auto handle = resolve_store(store_, ctx);
  if (handle.store == nullptr) {
    return common::Result<ToolResult>::failure("session store unavailable");
  }

  const std::string parent_session_id =
      optional_arg(args, "parent_session_id").value_or(default_parent_session_id(ctx));

  auto parent_ensure = ensure_session_exists(handle.store, parent_session_id, ctx);
  if (!parent_ensure.ok()) {
    return common::Result<ToolResult>::failure(parent_ensure.error());
  }

  std::string agent_id = common::trim(ctx.agent_id).empty() ? "ghostclaw" : common::trim(ctx.agent_id);
  auto parsed_parent = sessions::parse_session_key(parent_session_id);
  if (parsed_parent.ok() && !parsed_parent.value().agent_id.empty()) {
    agent_id = parsed_parent.value().agent_id;
  }

  const std::string child_session_id = child_session_key_for_spawn(agent_id);

  sessions::SessionState child;
  child.session_id = child_session_id;
  child.agent_id = agent_id;
  child.channel_id = "subagent";
  child.peer_id = "subagent";
  auto parsed_child = sessions::parse_session_key(child_session_id);
  if (parsed_child.ok()) {
    child.peer_id = parsed_child.value().peer_id;
  }
  child.model = optional_arg(args, "model").value_or("");
  child.thinking_level = optional_arg(args, "thinking_level").value_or("standard");
  child.delivery_context = "subagent_spawned";
  child.updated_at = memory::now_rfc3339();

  auto upsert_child = handle.store->upsert_state(child);
  if (!upsert_child.ok()) {
    return common::Result<ToolResult>::failure(upsert_child.error());
  }

  auto reg = handle.store->register_subagent(parent_session_id, child_session_id);
  if (!reg.ok()) {
    return common::Result<ToolResult>::failure(reg.error());
  }

  sessions::TranscriptEntry child_entry;
  child_entry.role = sessions::TranscriptRole::System;
  child_entry.timestamp = memory::now_rfc3339();
  child_entry.content = "Subagent task: " + task.value();
  child_entry.metadata["source_tool"] = "sessions_spawn";
  child_entry.metadata["parent_session_id"] = parent_session_id;
  if (const auto label = optional_arg(args, "label"); label.has_value()) {
    child_entry.metadata["label"] = *label;
  }
  auto child_append = handle.store->append_transcript(child_session_id, child_entry);
  if (!child_append.ok()) {
    return common::Result<ToolResult>::failure(child_append.error());
  }

  sessions::TranscriptEntry parent_entry;
  parent_entry.role = sessions::TranscriptRole::System;
  parent_entry.timestamp = memory::now_rfc3339();
  parent_entry.content = "Spawned subagent " + child_session_id;
  parent_entry.metadata["source_tool"] = "sessions_spawn";
  parent_entry.metadata["task"] = task.value();
  auto parent_append = handle.store->append_transcript(parent_session_id, parent_entry);
  if (!parent_append.ok()) {
    return common::Result<ToolResult>::failure(parent_append.error());
  }

  std::ostringstream out;
  out << "{";
  out << "\"status\":\"accepted\",";
  out << "\"parent_session_id\":\"" << json_escape(parent_session_id) << "\",";
  out << "\"child_session_id\":\"" << json_escape(child_session_id) << "\"";
  out << "}";

  ToolResult result;
  result.output = out.str();
  result.metadata["parent_session_id"] = parent_session_id;
  result.metadata["child_session_id"] = child_session_id;
  result.metadata["run_status"] = "accepted";
  return common::Result<ToolResult>::success(std::move(result));
}

bool SessionsSpawnTool::is_safe() const { return true; }

std::string_view SessionsSpawnTool::group() const { return "sessions"; }

} // namespace ghostclaw::tools
