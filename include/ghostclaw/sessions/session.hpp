#pragma once

#include "ghostclaw/common/result.hpp"

#include <string>
#include <vector>

namespace ghostclaw::sessions {

struct SessionState {
  std::string session_id;
  std::string agent_id;
  std::string channel_id;
  std::string peer_id;
  std::string model;
  std::string thinking_level;
  std::string group_id;
  std::string delivery_context;
  std::string updated_at;
  std::vector<std::string> subagents;
};

[[nodiscard]] std::string encode_session_state_jsonl(const SessionState &state);
[[nodiscard]] common::Result<SessionState> parse_session_state_jsonl(const std::string &line);

} // namespace ghostclaw::sessions
