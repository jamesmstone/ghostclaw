#pragma once

#include "ghostclaw/common/result.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ghostclaw::sessions {

enum class TranscriptRole {
  System,
  User,
  Assistant,
  Tool,
};

[[nodiscard]] std::string role_to_string(TranscriptRole role);
[[nodiscard]] TranscriptRole role_from_string(std::string_view value);

struct InputProvenance {
  std::string kind;
  std::optional<std::string> source_session_id;
  std::optional<std::string> source_channel;
  std::optional<std::string> source_tool;
  std::optional<std::string> source_message_id;
};

struct TranscriptEntry {
  TranscriptRole role = TranscriptRole::User;
  std::string content;
  std::string timestamp;
  std::optional<std::string> model;
  std::optional<InputProvenance> input_provenance;
  std::unordered_map<std::string, std::string> metadata;
};

[[nodiscard]] std::string encode_transcript_entry_jsonl(const TranscriptEntry &entry);
[[nodiscard]] common::Result<TranscriptEntry> parse_transcript_entry_jsonl(const std::string &line);

} // namespace ghostclaw::sessions
