#include "ghostclaw/sessions/store.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/sessions/session_key.hpp"

#include <algorithm>
#include <fstream>

namespace ghostclaw::sessions {

namespace {

std::string sanitize_session_filename(const std::string &session_id) {
  std::string out;
  out.reserve(session_id.size());
  for (const char ch : session_id) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
        ch == '-' || ch == '_') {
      out.push_back(ch);
    } else {
      out.push_back('_');
    }
  }
  if (out.empty()) {
    return "default";
  }
  return out;
}

std::string now_timestamp() { return memory::now_rfc3339(); }

} // namespace

SessionStore::SessionStore(std::filesystem::path root_dir) : root_dir_(std::move(root_dir)) {
  std::error_code ec;
  std::filesystem::create_directories(root_dir_, ec);
  transcript_dir_ = root_dir_ / "transcripts";
  std::filesystem::create_directories(transcript_dir_, ec);
  state_index_path_ = root_dir_ / "states.jsonl";
  (void)load_state_index();
}

common::Status SessionStore::load_state_index() {
  std::lock_guard<std::mutex> lock(mutex_);
  states_.clear();

  std::ifstream in(state_index_path_);
  if (!in) {
    return common::Status::success();
  }

  std::string line;
  while (std::getline(in, line)) {
    auto parsed = parse_session_state_jsonl(line);
    if (!parsed.ok()) {
      continue;
    }
    states_[parsed.value().session_id] = parsed.value();
  }
  return common::Status::success();
}

common::Status SessionStore::persist_state_index() const {
  std::filesystem::path tmp = state_index_path_;
  tmp += ".tmp";
  std::ofstream out(tmp, std::ios::trunc);
  if (!out) {
    return common::Status::error("failed to open temporary session index");
  }

  std::vector<SessionState> ordered;
  ordered.reserve(states_.size());
  for (const auto &[id, state] : states_) {
    (void)id;
    ordered.push_back(state);
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const SessionState &a, const SessionState &b) { return a.updated_at > b.updated_at; });

  for (const auto &state : ordered) {
    out << encode_session_state_jsonl(state) << "\n";
  }
  out.close();
  if (!out) {
    return common::Status::error("failed writing temporary session index");
  }

  std::error_code ec;
  std::filesystem::rename(tmp, state_index_path_, ec);
  if (ec) {
    return common::Status::error("failed replacing session index: " + ec.message());
  }
  return common::Status::success();
}

std::filesystem::path SessionStore::transcript_path(const std::string &session_id) const {
  return transcript_dir_ / (sanitize_session_filename(session_id) + ".jsonl");
}

common::Result<SessionState> SessionStore::normalize_state(const SessionState &state) const {
  SessionState normalized = state;
  if (normalized.session_id.empty()) {
    return common::Result<SessionState>::failure("session_id is required");
  }

  auto parsed = parse_session_key(normalized.session_id);
  if (parsed.ok()) {
    if (normalized.agent_id.empty()) {
      normalized.agent_id = parsed.value().agent_id;
    }
    if (normalized.channel_id.empty()) {
      normalized.channel_id = parsed.value().channel_id;
    }
    if (normalized.peer_id.empty()) {
      normalized.peer_id = parsed.value().peer_id;
    }
  } else {
    if (normalized.agent_id.empty()) {
      normalized.agent_id = "ghostclaw";
    }
    if (normalized.channel_id.empty()) {
      normalized.channel_id = "unknown";
    }
    if (normalized.peer_id.empty()) {
      normalized.peer_id = normalized.session_id;
    }
    auto key = make_session_key({.agent_id = normalized.agent_id,
                                 .channel_id = normalized.channel_id,
                                 .peer_id = normalized.peer_id});
    if (!key.ok()) {
      return common::Result<SessionState>::failure("invalid session state");
    }
    normalized.session_id = key.value();
  }

  if (normalized.updated_at.empty()) {
    normalized.updated_at = now_timestamp();
  }
  if (normalized.thinking_level.empty()) {
    normalized.thinking_level = "standard";
  }
  if (normalized.delivery_context.empty()) {
    normalized.delivery_context = "default";
  }
  return common::Result<SessionState>::success(std::move(normalized));
}

common::Status SessionStore::upsert_state(const SessionState &state) {
  auto normalized = normalize_state(state);
  if (!normalized.ok()) {
    return common::Status::error(normalized.error());
  }

  std::lock_guard<std::mutex> lock(mutex_);
  SessionState merged = normalized.value();
  const auto existing_it = states_.find(merged.session_id);
  if (existing_it != states_.end()) {
    const SessionState &existing = existing_it->second;
    if (state.agent_id.empty()) {
      merged.agent_id = existing.agent_id;
    }
    if (state.channel_id.empty()) {
      merged.channel_id = existing.channel_id;
    }
    if (state.peer_id.empty()) {
      merged.peer_id = existing.peer_id;
    }
    if (state.model.empty()) {
      merged.model = existing.model;
    }
    if (state.thinking_level.empty()) {
      merged.thinking_level = existing.thinking_level;
    }
    if (state.group_id.empty()) {
      merged.group_id = existing.group_id;
    }
    if (state.delivery_context.empty()) {
      merged.delivery_context = existing.delivery_context;
    }
    if (state.subagents.empty()) {
      merged.subagents = existing.subagents;
    }
  }
  if (state.updated_at.empty()) {
    merged.updated_at = now_timestamp();
  }
  states_[merged.session_id] = std::move(merged);
  return persist_state_index();
}

common::Result<SessionState> SessionStore::get_state(const std::string &session_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = states_.find(session_id);
  if (it == states_.end()) {
    return common::Result<SessionState>::failure("session not found");
  }
  return common::Result<SessionState>::success(it->second);
}

common::Result<std::vector<SessionState>> SessionStore::list_states() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<SessionState> out;
  out.reserve(states_.size());
  for (const auto &[session_id, state] : states_) {
    (void)session_id;
    out.push_back(state);
  }
  std::sort(out.begin(), out.end(),
            [](const SessionState &a, const SessionState &b) { return a.updated_at > b.updated_at; });
  return common::Result<std::vector<SessionState>>::success(std::move(out));
}

common::Result<std::vector<SessionState>>
SessionStore::list_states_by_group(const std::string &group_id) const {
  const std::string normalized_group = common::trim(group_id);
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<SessionState> out;
  out.reserve(states_.size());
  for (const auto &[session_id, state] : states_) {
    (void)session_id;
    if (common::trim(state.group_id) == normalized_group) {
      out.push_back(state);
    }
  }
  std::sort(out.begin(), out.end(),
            [](const SessionState &a, const SessionState &b) { return a.updated_at > b.updated_at; });
  return common::Result<std::vector<SessionState>>::success(std::move(out));
}

common::Status SessionStore::set_group(const std::string &session_id, const std::string &group_id) {
  if (common::trim(session_id).empty()) {
    return common::Status::error("session_id is required");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = states_.find(session_id);
  if (it == states_.end()) {
    SessionState state;
    state.session_id = session_id;
    auto normalized = normalize_state(state);
    if (!normalized.ok()) {
      return common::Status::error(normalized.error());
    }
    it = states_.insert_or_assign(normalized.value().session_id, normalized.value()).first;
  }
  it->second.group_id = common::trim(group_id);
  it->second.updated_at = now_timestamp();
  return persist_state_index();
}

common::Status SessionStore::append_transcript(const std::string &session_id,
                                               const TranscriptEntry &entry) {
  if (session_id.empty()) {
    return common::Status::error("session_id is required");
  }

  TranscriptEntry normalized_entry = entry;
  if (normalized_entry.timestamp.empty()) {
    normalized_entry.timestamp = now_timestamp();
  }

  const auto path = transcript_path(session_id);
  std::ofstream out(path, std::ios::app);
  if (!out) {
    return common::Status::error("failed opening transcript file");
  }
  out << encode_transcript_entry_jsonl(normalized_entry) << "\n";
  if (!out) {
    return common::Status::error("failed appending transcript");
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = states_.find(session_id);
  if (it != states_.end()) {
    it->second.updated_at = normalized_entry.timestamp;
  } else {
    SessionState state;
    state.session_id = session_id;
    state.updated_at = normalized_entry.timestamp;
    auto normalized_state = normalize_state(state);
    if (normalized_state.ok()) {
      states_[normalized_state.value().session_id] = normalized_state.value();
    }
  }
  return persist_state_index();
}

common::Result<std::vector<TranscriptEntry>>
SessionStore::load_transcript(const std::string &session_id, const std::size_t limit) const {
  if (session_id.empty()) {
    return common::Result<std::vector<TranscriptEntry>>::failure("session_id is required");
  }

  std::ifstream in(transcript_path(session_id));
  if (!in) {
    return common::Result<std::vector<TranscriptEntry>>::success({});
  }

  std::vector<TranscriptEntry> entries;
  std::string line;
  while (std::getline(in, line)) {
    auto parsed = parse_transcript_entry_jsonl(line);
    if (!parsed.ok()) {
      continue;
    }
    entries.push_back(parsed.value());
  }

  if (limit > 0 && entries.size() > limit) {
    entries.erase(entries.begin(), entries.end() - static_cast<long>(limit));
  }
  return common::Result<std::vector<TranscriptEntry>>::success(std::move(entries));
}

common::Status SessionStore::register_subagent(const std::string &session_id,
                                               const std::string &subagent_id) {
  if (session_id.empty() || subagent_id.empty()) {
    return common::Status::error("session_id and subagent_id are required");
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = states_.find(session_id);
  if (it == states_.end()) {
    SessionState state;
    state.session_id = session_id;
    auto normalized = normalize_state(state);
    if (!normalized.ok()) {
      return common::Status::error(normalized.error());
    }
    it = states_.insert_or_assign(normalized.value().session_id, normalized.value()).first;
  }

  auto &subagents = it->second.subagents;
  if (std::find(subagents.begin(), subagents.end(), subagent_id) == subagents.end()) {
    subagents.push_back(subagent_id);
    it->second.updated_at = now_timestamp();
  }
  return persist_state_index();
}

common::Status SessionStore::unregister_subagent(const std::string &session_id,
                                                 const std::string &subagent_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = states_.find(session_id);
  if (it == states_.end()) {
    return common::Status::success();
  }
  auto &subagents = it->second.subagents;
  subagents.erase(std::remove(subagents.begin(), subagents.end(), subagent_id), subagents.end());
  it->second.updated_at = now_timestamp();
  return persist_state_index();
}

} // namespace ghostclaw::sessions
