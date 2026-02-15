#pragma once

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/sessions/session_key.hpp"
#include "ghostclaw/sessions/store.hpp"
#include "ghostclaw/sessions/transcript.hpp"
#include "ghostclaw/tools/tool.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ghostclaw::tools::builtin::sessions_internal {

struct StoreHandle {
  std::shared_ptr<sessions::SessionStore> owned;
  sessions::SessionStore *store = nullptr;
};

inline std::string json_escape(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const char ch : value) {
    switch (ch) {
    case '"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped.push_back(ch);
      break;
    }
  }
  return escaped;
}

inline std::string random_id(const std::size_t bytes = 8) {
  static thread_local std::mt19937_64 rng(std::random_device{}());
  std::ostringstream out;
  for (std::size_t i = 0; i < bytes; ++i) {
    const auto value = static_cast<unsigned>(rng() & 0xFFULL);
    out << "0123456789abcdef"[value >> 4U] << "0123456789abcdef"[value & 0x0FU];
  }
  return out.str();
}

inline std::optional<std::string> optional_arg(const ToolArgs &args, const std::string &name) {
  const auto it = args.find(name);
  if (it == args.end()) {
    return std::nullopt;
  }
  const std::string value = common::trim(it->second);
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

inline common::Result<std::string> required_arg(const ToolArgs &args, const std::string &name) {
  auto value = optional_arg(args, name);
  if (!value.has_value()) {
    return common::Result<std::string>::failure("Missing argument: " + name);
  }
  return common::Result<std::string>::success(*value);
}

inline std::size_t parse_size_arg(const ToolArgs &args, const std::string &name,
                                  const std::size_t default_value,
                                  const std::size_t max_value) {
  const auto maybe = optional_arg(args, name);
  if (!maybe.has_value()) {
    return default_value;
  }
  try {
    const auto parsed = static_cast<std::size_t>(std::stoull(*maybe));
    return std::min(parsed, max_value);
  } catch (...) {
    return default_value;
  }
}

inline bool parse_bool_arg(const ToolArgs &args, const std::string &name, const bool default_value) {
  const auto maybe = optional_arg(args, name);
  if (!maybe.has_value()) {
    return default_value;
  }
  const std::string lowered = common::to_lower(*maybe);
  if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
    return true;
  }
  if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
    return false;
  }
  return default_value;
}

inline StoreHandle resolve_store(const std::shared_ptr<sessions::SessionStore> &injected,
                                 const ToolContext &ctx) {
  if (injected != nullptr) {
    return StoreHandle{.owned = injected, .store = injected.get()};
  }

  std::filesystem::path root = ctx.workspace_path;
  if (root.empty()) {
    root = std::filesystem::temp_directory_path() / "ghostclaw-sessions-tools-fallback";
  }
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  auto store = std::make_shared<sessions::SessionStore>(root / "sessions");
  return StoreHandle{.owned = std::move(store), .store = store.get()};
}

inline std::string default_parent_session_id(const ToolContext &ctx) {
  if (!common::trim(ctx.session_id).empty()) {
    return common::trim(ctx.session_id);
  }
  auto key = sessions::make_session_key(
      {.agent_id = common::trim(ctx.agent_id).empty() ? "ghostclaw" : common::trim(ctx.agent_id),
       .channel_id = "local",
       .peer_id = "main"});
  if (key.ok()) {
    return key.value();
  }
  return "agent:ghostclaw:channel:local:peer:main";
}

inline std::string session_state_json(const sessions::SessionState &state) {
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

inline std::string transcript_entry_json(const sessions::TranscriptEntry &entry,
                                         const bool include_metadata) {
  std::ostringstream out;
  out << "{";
  out << "\"role\":\"" << json_escape(sessions::role_to_string(entry.role)) << "\",";
  out << "\"content\":\"" << json_escape(entry.content) << "\",";
  out << "\"timestamp\":\"" << json_escape(entry.timestamp) << "\"";
  if (entry.model.has_value() && !entry.model->empty()) {
    out << ",\"model\":\"" << json_escape(*entry.model) << "\"";
  }
  if (include_metadata && !entry.metadata.empty()) {
    out << ",\"metadata\":{";
    bool first = true;
    for (const auto &[key, value] : entry.metadata) {
      if (!first) {
        out << ",";
      }
      first = false;
      out << "\"" << json_escape(key) << "\":\"" << json_escape(value) << "\"";
    }
    out << "}";
  }
  if (include_metadata && entry.input_provenance.has_value()) {
    out << ",\"input_provenance\":{";
    out << "\"kind\":\"" << json_escape(entry.input_provenance->kind) << "\"";
    if (entry.input_provenance->source_session_id.has_value()) {
      out << ",\"source_session_id\":\""
          << json_escape(*entry.input_provenance->source_session_id) << "\"";
    }
    if (entry.input_provenance->source_channel.has_value()) {
      out << ",\"source_channel\":\"" << json_escape(*entry.input_provenance->source_channel)
          << "\"";
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
  out << "}";
  return out.str();
}

inline common::Status ensure_session_exists(sessions::SessionStore *store, const std::string &session_id,
                                            const ToolContext &ctx) {
  auto existing = store->get_state(session_id);
  if (existing.ok()) {
    return common::Status::success();
  }

  sessions::SessionState state;
  state.session_id = session_id;
  state.agent_id = common::trim(ctx.agent_id).empty() ? "ghostclaw" : common::trim(ctx.agent_id);
  state.channel_id = "internal";
  state.peer_id = session_id;
  state.model = "";
  state.thinking_level = "standard";
  state.delivery_context = "sessions_tool";
  state.updated_at = memory::now_rfc3339();
  return store->upsert_state(state);
}

inline std::string child_session_key_for_spawn(const std::string &agent_id) {
  const auto key = sessions::make_session_key({.agent_id = agent_id, .channel_id = "subagent",
                                               .peer_id = "sa-" + random_id(8)});
  if (key.ok()) {
    return key.value();
  }
  return "agent:" + agent_id + ":channel:subagent:peer:sa-" + random_id(8);
}

} // namespace ghostclaw::tools::builtin::sessions_internal
