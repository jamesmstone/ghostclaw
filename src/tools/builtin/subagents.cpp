#include "ghostclaw/tools/builtin/sessions.hpp"

#include "sessions_internal.hpp"

#include <sstream>

namespace ghostclaw::tools {

namespace {

std::optional<std::string> resolve_target_subagent(const std::vector<std::string> &subagents,
                                                   const std::string &target) {
  const std::string trimmed = common::trim(target);
  if (trimmed.empty()) {
    if (subagents.empty()) {
      return std::nullopt;
    }
    return subagents.back();
  }

  for (const auto &id : subagents) {
    if (id == trimmed) {
      return id;
    }
  }

  for (const auto &id : subagents) {
    auto parsed = sessions::parse_session_key(id);
    if (parsed.ok() && parsed.value().peer_id == trimmed) {
      return id;
    }
  }
  return std::nullopt;
}

} // namespace

SubagentsTool::SubagentsTool(std::shared_ptr<sessions::SessionStore> store)
    : store_(std::move(store)) {}

std::string_view SubagentsTool::name() const { return "subagents"; }

std::string_view SubagentsTool::description() const {
  return "List, steer, or terminate spawned subagent sessions";
}

std::string SubagentsTool::parameters_schema() const {
  return R"({"type":"object","required":["action"],"properties":{"action":{"type":"string","enum":["list","steer","kill"]},"parent_session_id":{"type":"string"},"target":{"type":"string"},"message":{"type":"string"},"limit":{"type":"integer","minimum":1}}})";
}

common::Result<ToolResult> SubagentsTool::execute(const ToolArgs &args, const ToolContext &ctx) {
  using namespace builtin::sessions_internal;

  auto action = required_arg(args, "action");
  if (!action.ok()) {
    return common::Result<ToolResult>::failure(action.error());
  }
  const std::string action_name = common::to_lower(common::trim(action.value()));
  if (action_name != "list" && action_name != "steer" && action_name != "kill") {
    return common::Result<ToolResult>::failure("invalid action (expected list|steer|kill)");
  }

  auto handle = resolve_store(store_, ctx);
  if (handle.store == nullptr) {
    return common::Result<ToolResult>::failure("session store unavailable");
  }

  const std::string parent_session_id =
      optional_arg(args, "parent_session_id").value_or(default_parent_session_id(ctx));
  auto parent_state = handle.store->get_state(parent_session_id);
  if (!parent_state.ok()) {
    return common::Result<ToolResult>::failure("parent session not found: " + parent_session_id);
  }

  if (action_name == "list") {
    const std::size_t limit = parse_size_arg(args, "limit", 50, 500);
    std::vector<std::string> subagents = parent_state.value().subagents;
    if (subagents.size() > limit) {
      subagents.resize(limit);
    }

    std::ostringstream out;
    out << "{";
    out << "\"parent_session_id\":\"" << json_escape(parent_session_id) << "\",";
    out << "\"count\":" << subagents.size() << ",";
    out << "\"subagents\":[";
    for (std::size_t i = 0; i < subagents.size(); ++i) {
      if (i > 0) {
        out << ",";
      }
      out << "{";
      out << "\"session_id\":\"" << json_escape(subagents[i]) << "\"";
      auto state = handle.store->get_state(subagents[i]);
      if (state.ok()) {
        out << ",\"delivery_context\":\"" << json_escape(state.value().delivery_context) << "\"";
        out << ",\"updated_at\":\"" << json_escape(state.value().updated_at) << "\"";
        if (!state.value().model.empty()) {
          out << ",\"model\":\"" << json_escape(state.value().model) << "\"";
        }
      }
      out << "}";
    }
    out << "]";
    out << "}";

    ToolResult result;
    result.output = out.str();
    result.metadata["count"] = std::to_string(subagents.size());
    return common::Result<ToolResult>::success(std::move(result));
  }

  const auto target = resolve_target_subagent(
      parent_state.value().subagents, optional_arg(args, "target").value_or(""));
  if (!target.has_value()) {
    return common::Result<ToolResult>::failure("target subagent not found");
  }

  if (action_name == "steer") {
    auto message = required_arg(args, "message");
    if (!message.ok()) {
      return common::Result<ToolResult>::failure(message.error());
    }

    sessions::TranscriptEntry steer;
    steer.role = sessions::TranscriptRole::User;
    steer.content = message.value();
    steer.timestamp = memory::now_rfc3339();
    steer.metadata["source_tool"] = "subagents";
    steer.metadata["action"] = "steer";
    steer.metadata["parent_session_id"] = parent_session_id;
    auto appended = handle.store->append_transcript(*target, steer);
    if (!appended.ok()) {
      return common::Result<ToolResult>::failure(appended.error());
    }

    auto existing = handle.store->get_state(*target);
    if (existing.ok()) {
      auto updated = existing.value();
      updated.delivery_context = "subagent_steered";
      updated.updated_at = memory::now_rfc3339();
      (void)handle.store->upsert_state(updated);
    }

    ToolResult result;
    result.output = "{\"status\":\"steered\",\"target\":\"" + json_escape(*target) + "\"}";
    result.metadata["target"] = *target;
    result.metadata["action"] = "steer";
    return common::Result<ToolResult>::success(std::move(result));
  }

  auto existing = handle.store->get_state(*target);
  if (existing.ok()) {
    auto updated = existing.value();
    updated.delivery_context = "subagent_terminated";
    updated.updated_at = memory::now_rfc3339();
    auto upserted = handle.store->upsert_state(updated);
    if (!upserted.ok()) {
      return common::Result<ToolResult>::failure(upserted.error());
    }
  }

  sessions::TranscriptEntry terminated;
  terminated.role = sessions::TranscriptRole::System;
  terminated.content = "Subagent terminated by parent session";
  terminated.timestamp = memory::now_rfc3339();
  terminated.metadata["source_tool"] = "subagents";
  terminated.metadata["action"] = "kill";
  terminated.metadata["parent_session_id"] = parent_session_id;
  auto appended = handle.store->append_transcript(*target, terminated);
  if (!appended.ok()) {
    return common::Result<ToolResult>::failure(appended.error());
  }

  auto unregistered = handle.store->unregister_subagent(parent_session_id, *target);
  if (!unregistered.ok()) {
    return common::Result<ToolResult>::failure(unregistered.error());
  }

  ToolResult result;
  result.output = "{\"status\":\"killed\",\"target\":\"" + json_escape(*target) + "\"}";
  result.metadata["target"] = *target;
  result.metadata["action"] = "kill";
  return common::Result<ToolResult>::success(std::move(result));
}

bool SubagentsTool::is_safe() const { return true; }

std::string_view SubagentsTool::group() const { return "sessions"; }

} // namespace ghostclaw::tools
