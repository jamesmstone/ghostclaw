#include "ghostclaw/sessions/session_key.hpp"

#include "ghostclaw/common/fs.hpp"

namespace ghostclaw::sessions {

namespace {

bool is_valid_component(const std::string &value) {
  return !value.empty() && value.find(':') == std::string::npos;
}

} // namespace

common::Result<std::string> make_session_key(const SessionKeyParts &parts) {
  if (!is_valid_component(parts.agent_id)) {
    return common::Result<std::string>::failure("invalid agent_id");
  }
  if (!is_valid_component(parts.channel_id)) {
    return common::Result<std::string>::failure("invalid channel_id");
  }
  if (!is_valid_component(parts.peer_id)) {
    return common::Result<std::string>::failure("invalid peer_id");
  }
  const std::string key = "agent:" + parts.agent_id + ":channel:" + parts.channel_id +
                          ":peer:" + parts.peer_id;
  return common::Result<std::string>::success(key);
}

bool is_session_key(const std::string &value) { return parse_session_key(value).ok(); }

common::Result<SessionKeyParts> parse_session_key(const std::string &key) {
  const std::string prefix = "agent:";
  if (key.rfind(prefix, 0) != 0) {
    return common::Result<SessionKeyParts>::failure("missing agent prefix");
  }
  const auto channel_marker = key.find(":channel:", prefix.size());
  if (channel_marker == std::string::npos) {
    return common::Result<SessionKeyParts>::failure("missing channel marker");
  }
  const auto peer_marker = key.find(":peer:", channel_marker + 9);
  if (peer_marker == std::string::npos) {
    return common::Result<SessionKeyParts>::failure("missing peer marker");
  }

  SessionKeyParts parts;
  parts.agent_id = key.substr(prefix.size(), channel_marker - prefix.size());
  parts.channel_id = key.substr(channel_marker + 9, peer_marker - (channel_marker + 9));
  parts.peer_id = key.substr(peer_marker + 6);
  if (!is_valid_component(parts.agent_id) || !is_valid_component(parts.channel_id) ||
      !is_valid_component(parts.peer_id)) {
    return common::Result<SessionKeyParts>::failure("invalid session key components");
  }
  return common::Result<SessionKeyParts>::success(std::move(parts));
}

} // namespace ghostclaw::sessions
