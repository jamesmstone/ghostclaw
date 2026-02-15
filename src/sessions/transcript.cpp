#include "ghostclaw/sessions/transcript.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"

#include <sstream>

namespace ghostclaw::sessions {

namespace {

std::string json_escape(const std::string &value) { return common::json_escape(value); }

std::string find_json_string_field(const std::string &json, const std::string &field) {
  return common::json_get_string(json, field);
}

std::string find_json_object_field(const std::string &json, const std::string &field) {
  return common::json_get_object(json, field);
}

std::unordered_map<std::string, std::string>
find_json_string_map_field(const std::string &json, const std::string &field) {
  const std::string obj = common::json_get_object(json, field);
  if (obj.empty()) {
    return {};
  }
  return common::json_parse_flat(obj);
}

} // namespace

std::string role_to_string(const TranscriptRole role) {
  switch (role) {
  case TranscriptRole::System:
    return "system";
  case TranscriptRole::User:
    return "user";
  case TranscriptRole::Assistant:
    return "assistant";
  case TranscriptRole::Tool:
    return "tool";
  }
  return "user";
}

TranscriptRole role_from_string(std::string_view value) {
  const std::string normalized = common::to_lower(common::trim(std::string(value)));
  if (normalized == "system") {
    return TranscriptRole::System;
  }
  if (normalized == "assistant") {
    return TranscriptRole::Assistant;
  }
  if (normalized == "tool") {
    return TranscriptRole::Tool;
  }
  return TranscriptRole::User;
}

std::string encode_transcript_entry_jsonl(const TranscriptEntry &entry) {
  std::ostringstream out;
  out << "{";
  out << "\"role\":\"" << json_escape(role_to_string(entry.role)) << "\",";
  out << "\"content\":\"" << json_escape(entry.content) << "\",";
  out << "\"timestamp\":\"" << json_escape(entry.timestamp) << "\"";
  if (entry.model.has_value()) {
    out << ",\"model\":\"" << json_escape(*entry.model) << "\"";
  }
  if (entry.input_provenance.has_value() &&
      !common::trim(entry.input_provenance->kind).empty()) {
    out << ",\"input_provenance\":{";
    out << "\"kind\":\"" << json_escape(entry.input_provenance->kind) << "\"";
    if (entry.input_provenance->source_session_id.has_value()) {
      out << ",\"source_session_id\":\""
          << json_escape(*entry.input_provenance->source_session_id) << "\"";
    }
    if (entry.input_provenance->source_channel.has_value()) {
      out << ",\"source_channel\":\""
          << json_escape(*entry.input_provenance->source_channel) << "\"";
    }
    if (entry.input_provenance->source_tool.has_value()) {
      out << ",\"source_tool\":\"" << json_escape(*entry.input_provenance->source_tool) << "\"";
    }
    if (entry.input_provenance->source_message_id.has_value()) {
      out << ",\"source_message_id\":\""
          << json_escape(*entry.input_provenance->source_message_id) << "\"";
    }
    out << "}";
  }
  if (!entry.metadata.empty()) {
    out << ",\"metadata\":{";
    bool first = true;
    for (const auto &[k, v] : entry.metadata) {
      if (!first) {
        out << ",";
      }
      first = false;
      out << "\"" << json_escape(k) << "\":\"" << json_escape(v) << "\"";
    }
    out << "}";
  }
  out << "}";
  return out.str();
}

common::Result<TranscriptEntry> parse_transcript_entry_jsonl(const std::string &line) {
  if (common::trim(line).empty()) {
    return common::Result<TranscriptEntry>::failure("empty transcript line");
  }
  TranscriptEntry entry;
  const std::string role = find_json_string_field(line, "role");
  if (role.empty()) {
    return common::Result<TranscriptEntry>::failure("transcript role missing");
  }
  entry.role = role_from_string(role);
  entry.content = find_json_string_field(line, "content");
  entry.timestamp = find_json_string_field(line, "timestamp");
  const std::string model = find_json_string_field(line, "model");
  if (!model.empty()) {
    entry.model = model;
  }
  const std::string provenance = find_json_object_field(line, "input_provenance");
  if (!provenance.empty()) {
    InputProvenance parsed_provenance;
    parsed_provenance.kind = find_json_string_field(provenance, "kind");
    if (!parsed_provenance.kind.empty()) {
      const std::string source_session_id =
          find_json_string_field(provenance, "source_session_id");
      if (!source_session_id.empty()) {
        parsed_provenance.source_session_id = source_session_id;
      }
      const std::string source_channel = find_json_string_field(provenance, "source_channel");
      if (!source_channel.empty()) {
        parsed_provenance.source_channel = source_channel;
      }
      const std::string source_tool = find_json_string_field(provenance, "source_tool");
      if (!source_tool.empty()) {
        parsed_provenance.source_tool = source_tool;
      }
      const std::string source_message_id =
          find_json_string_field(provenance, "source_message_id");
      if (!source_message_id.empty()) {
        parsed_provenance.source_message_id = source_message_id;
      }
      entry.input_provenance = std::move(parsed_provenance);
    }
  }
  entry.metadata = find_json_string_map_field(line, "metadata");
  return common::Result<TranscriptEntry>::success(std::move(entry));
}

} // namespace ghostclaw::sessions
