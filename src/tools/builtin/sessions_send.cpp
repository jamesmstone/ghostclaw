#include "ghostclaw/tools/builtin/sessions.hpp"

#include "sessions_internal.hpp"

#include <sstream>

namespace ghostclaw::tools {

SessionsSendTool::SessionsSendTool(std::shared_ptr<sessions::SessionStore> store)
    : store_(std::move(store)) {}

std::string_view SessionsSendTool::name() const { return "sessions_send"; }

std::string_view SessionsSendTool::description() const {
  return "Send a message to another session transcript";
}

std::string SessionsSendTool::parameters_schema() const {
  return R"({"type":"object","required":["session_id","message"],"properties":{"session_id":{"type":"string"},"message":{"type":"string"},"role":{"type":"string"},"model":{"type":"string"}}})";
}

common::Result<ToolResult> SessionsSendTool::execute(const ToolArgs &args, const ToolContext &ctx) {
  using namespace builtin::sessions_internal;

  auto session_id = required_arg(args, "session_id");
  if (!session_id.ok()) {
    return common::Result<ToolResult>::failure(session_id.error());
  }
  auto message = required_arg(args, "message");
  if (!message.ok()) {
    return common::Result<ToolResult>::failure(message.error());
  }

  auto handle = resolve_store(store_, ctx);
  if (handle.store == nullptr) {
    return common::Result<ToolResult>::failure("session store unavailable");
  }

  auto ensure = ensure_session_exists(handle.store, session_id.value(), ctx);
  if (!ensure.ok()) {
    return common::Result<ToolResult>::failure(ensure.error());
  }

  sessions::TranscriptEntry entry;
  entry.role = sessions::TranscriptRole::User;
  if (const auto role = optional_arg(args, "role"); role.has_value()) {
    entry.role = sessions::role_from_string(*role);
  }
  entry.content = message.value();
  entry.timestamp = memory::now_rfc3339();
  if (const auto model = optional_arg(args, "model"); model.has_value()) {
    entry.model = model;
  }

  entry.metadata["source_tool"] = "sessions_send";
  if (!common::trim(ctx.agent_id).empty()) {
    entry.metadata["source_agent_id"] = common::trim(ctx.agent_id);
  }
  if (!common::trim(ctx.session_id).empty()) {
    entry.metadata["source_session_id"] = common::trim(ctx.session_id);
  }

  if (!common::trim(ctx.session_id).empty() && common::trim(ctx.session_id) != session_id.value()) {
    sessions::InputProvenance provenance;
    provenance.kind = "inter_session";
    provenance.source_session_id = common::trim(ctx.session_id);
    provenance.source_tool = "sessions_send";
    entry.input_provenance = std::move(provenance);
  }

  auto appended = handle.store->append_transcript(session_id.value(), entry);
  if (!appended.ok()) {
    return common::Result<ToolResult>::failure(appended.error());
  }

  std::ostringstream out;
  out << "{";
  out << "\"status\":\"accepted\",";
  out << "\"session_id\":\"" << json_escape(session_id.value()) << "\",";
  out << "\"role\":\"" << json_escape(sessions::role_to_string(entry.role)) << "\"";
  out << "}";

  ToolResult result;
  result.output = out.str();
  result.metadata["session_id"] = session_id.value();
  result.metadata["role"] = sessions::role_to_string(entry.role);
  result.metadata["queued"] = "1";
  return common::Result<ToolResult>::success(std::move(result));
}

bool SessionsSendTool::is_safe() const { return true; }

std::string_view SessionsSendTool::group() const { return "sessions"; }

} // namespace ghostclaw::tools
