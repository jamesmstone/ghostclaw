#pragma once

#include "ghostclaw/common/result.hpp"

#include <string>

namespace ghostclaw::sessions {

struct SessionKeyParts {
  std::string agent_id;
  std::string channel_id;
  std::string peer_id;
};

[[nodiscard]] common::Result<std::string> make_session_key(const SessionKeyParts &parts);
[[nodiscard]] common::Result<SessionKeyParts> parse_session_key(const std::string &key);
[[nodiscard]] bool is_session_key(const std::string &value);

} // namespace ghostclaw::sessions
