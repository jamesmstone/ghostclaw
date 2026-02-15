#include "ghostclaw/sessions/session.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"

#include <sstream>

namespace ghostclaw::sessions {

namespace {

std::string json_escape(const std::string &value) { return common::json_escape(value); }

std::string find_json_string_field(const std::string &json, const std::string &field) {
  return common::json_get_string(json, field);
}

std::vector<std::string> find_json_string_array_field(const std::string &json,
                                                      const std::string &field) {
  return common::json_get_string_array(json, field);
}

} // namespace

std::string encode_session_state_jsonl(const SessionState &state) {
  std::ostringstream out;
  out << "{";
  out << "\"session_id\":\"" << json_escape(state.session_id) << "\",";
  out << "\"agent_id\":\"" << json_escape(state.agent_id) << "\",";
  out << "\"channel_id\":\"" << json_escape(state.channel_id) << "\",";
  out << "\"peer_id\":\"" << json_escape(state.peer_id) << "\",";
  out << "\"model\":\"" << json_escape(state.model) << "\",";
  out << "\"thinking_level\":\"" << json_escape(state.thinking_level) << "\",";
  out << "\"group_id\":\"" << json_escape(state.group_id) << "\",";
  out << "\"delivery_context\":\"" << json_escape(state.delivery_context) << "\",";
  out << "\"updated_at\":\"" << json_escape(state.updated_at) << "\",";
  out << "\"subagents\":[";
  for (std::size_t i = 0; i < state.subagents.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << "\"" << json_escape(state.subagents[i]) << "\"";
  }
  out << "]";
  out << "}";
  return out.str();
}

common::Result<SessionState> parse_session_state_jsonl(const std::string &line) {
  if (common::trim(line).empty()) {
    return common::Result<SessionState>::failure("empty session state line");
  }
  SessionState state;
  state.session_id = find_json_string_field(line, "session_id");
  if (state.session_id.empty()) {
    return common::Result<SessionState>::failure("session_id missing");
  }
  state.agent_id = find_json_string_field(line, "agent_id");
  state.channel_id = find_json_string_field(line, "channel_id");
  state.peer_id = find_json_string_field(line, "peer_id");
  state.model = find_json_string_field(line, "model");
  state.thinking_level = find_json_string_field(line, "thinking_level");
  state.group_id = find_json_string_field(line, "group_id");
  state.delivery_context = find_json_string_field(line, "delivery_context");
  state.updated_at = find_json_string_field(line, "updated_at");
  state.subagents = find_json_string_array_field(line, "subagents");
  return common::Result<SessionState>::success(std::move(state));
}

} // namespace ghostclaw::sessions
