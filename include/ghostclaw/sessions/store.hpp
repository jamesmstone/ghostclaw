#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/sessions/session.hpp"
#include "ghostclaw/sessions/transcript.hpp"

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ghostclaw::sessions {

class SessionStore {
public:
  explicit SessionStore(std::filesystem::path root_dir);

  [[nodiscard]] common::Status upsert_state(const SessionState &state);
  [[nodiscard]] common::Result<SessionState> get_state(const std::string &session_id) const;
  [[nodiscard]] common::Result<std::vector<SessionState>> list_states() const;
  [[nodiscard]] common::Result<std::vector<SessionState>>
  list_states_by_group(const std::string &group_id) const;
  [[nodiscard]] common::Status set_group(const std::string &session_id,
                                         const std::string &group_id);

  [[nodiscard]] common::Status append_transcript(const std::string &session_id,
                                                 const TranscriptEntry &entry);
  [[nodiscard]] common::Result<std::vector<TranscriptEntry>>
  load_transcript(const std::string &session_id, std::size_t limit = 0) const;

  [[nodiscard]] common::Status register_subagent(const std::string &session_id,
                                                 const std::string &subagent_id);
  [[nodiscard]] common::Status unregister_subagent(const std::string &session_id,
                                                   const std::string &subagent_id);

private:
  [[nodiscard]] common::Status load_state_index();
  [[nodiscard]] common::Status persist_state_index() const;
  [[nodiscard]] std::filesystem::path transcript_path(const std::string &session_id) const;
  [[nodiscard]] common::Result<SessionState> normalize_state(const SessionState &state) const;

  std::filesystem::path root_dir_;
  std::filesystem::path state_index_path_;
  std::filesystem::path transcript_dir_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, SessionState> states_;
};

} // namespace ghostclaw::sessions
