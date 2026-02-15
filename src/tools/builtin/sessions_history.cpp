#include "ghostclaw/tools/builtin/sessions.hpp"

#include "sessions_internal.hpp"

#include <sstream>

namespace ghostclaw::tools {

SessionsHistoryTool::SessionsHistoryTool(std::shared_ptr<sessions::SessionStore> store)
    : store_(std::move(store)) {}

std::string_view SessionsHistoryTool::name() const { return "sessions_history"; }

std::string_view SessionsHistoryTool::description() const {
  return "Fetch transcript history for a session";
}

std::string SessionsHistoryTool::parameters_schema() const {
  return R"({"type":"object","required":["session_id"],"properties":{"session_id":{"type":"string"},"limit":{"type":"integer","minimum":1},"include_metadata":{"type":"boolean"},"max_chars":{"type":"integer","minimum":64}}})";
}

common::Result<ToolResult> SessionsHistoryTool::execute(const ToolArgs &args,
                                                        const ToolContext &ctx) {
  using namespace builtin::sessions_internal;

  auto session_id = required_arg(args, "session_id");
  if (!session_id.ok()) {
    return common::Result<ToolResult>::failure(session_id.error());
  }

  auto handle = resolve_store(store_, ctx);
  if (handle.store == nullptr) {
    return common::Result<ToolResult>::failure("session store unavailable");
  }

  const std::size_t limit = parse_size_arg(args, "limit", 50, 500);
  const bool include_metadata = parse_bool_arg(args, "include_metadata", true);
  const std::size_t max_chars = parse_size_arg(args, "max_chars", 4000, 32 * 1024);

  auto loaded = handle.store->load_transcript(session_id.value(), limit);
  if (!loaded.ok()) {
    return common::Result<ToolResult>::failure(loaded.error());
  }

  std::size_t truncated_entries = 0;
  std::ostringstream json;
  json << "{";
  json << "\"session_id\":\"" << json_escape(session_id.value()) << "\",";
  json << "\"count\":" << loaded.value().size() << ",";
  json << "\"entries\":[";
  for (std::size_t i = 0; i < loaded.value().size(); ++i) {
    if (i > 0) {
      json << ",";
    }
    auto entry = loaded.value()[i];
    if (entry.content.size() > max_chars) {
      entry.content.resize(max_chars);
      entry.content += "\n...(truncated)...";
      ++truncated_entries;
    }
    json << transcript_entry_json(entry, include_metadata);
  }
  json << "],";
  json << "\"truncated_entries\":" << truncated_entries;
  json << "}";

  ToolResult result;
  result.output = json.str();
  result.metadata["session_id"] = session_id.value();
  result.metadata["count"] = std::to_string(loaded.value().size());
  if (truncated_entries > 0) {
    result.truncated = true;
    result.metadata["truncated_entries"] = std::to_string(truncated_entries);
  }
  return common::Result<ToolResult>::success(std::move(result));
}

bool SessionsHistoryTool::is_safe() const { return true; }

std::string_view SessionsHistoryTool::group() const { return "sessions"; }

} // namespace ghostclaw::tools
