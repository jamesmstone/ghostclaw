#include "ghostclaw/gateway/protocol.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"
#include "ghostclaw/providers/traits.hpp"
#include "ghostclaw/sessions/session_key.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ghostclaw::gateway {

namespace {

std::string to_json_object(const RpcMap &map) {
  std::ostringstream out;
  out << "{";
  bool first = true;
  for (const auto &[k, v] : map) {
    if (!first) {
      out << ",";
    }
    first = false;
    out << "\"" << common::json_escape(k) << "\":\"" << common::json_escape(v) << "\"";
  }
  out << "}";
  return out.str();
}

std::string find_json_string_field(const std::string &json, const std::string &field) {
  return common::json_get_string(json, field);
}

std::string find_json_numeric_field(const std::string &json, const std::string &field) {
  return common::json_get_number(json, field);
}

std::string normalize_session_id(const std::string &candidate, const std::string &channel,
                                 const std::string &fallback_peer) {
  if (!candidate.empty()) {
    if (sessions::is_session_key(candidate)) {
      return candidate;
    }
    auto key = sessions::make_session_key(
        {.agent_id = "ghostclaw", .channel_id = channel, .peer_id = common::trim(candidate)});
    if (key.ok()) {
      return key.value();
    }
  }
  auto fallback =
      sessions::make_session_key({.agent_id = "ghostclaw",
                                  .channel_id = channel,
                                  .peer_id = common::trim(fallback_peer)});
  return fallback.ok() ? fallback.value()
                       : "agent:ghostclaw:channel:" + channel + ":peer:" + fallback_peer;
}

void upsert_session_state(sessions::SessionStore *store, const std::string &session_id,
                          const std::string &model, const std::string &thinking_level,
                          const std::string &delivery_context,
                          const std::string &group_id = "") {
  if (store == nullptr || session_id.empty()) {
    return;
  }

  sessions::SessionState state;
  state.session_id = session_id;
  auto parsed = sessions::parse_session_key(session_id);
  if (parsed.ok()) {
    state.agent_id = parsed.value().agent_id;
    state.channel_id = parsed.value().channel_id;
    state.peer_id = parsed.value().peer_id;
  }
  state.model = model;
  state.thinking_level = thinking_level;
  state.group_id = group_id;
  state.delivery_context = delivery_context;
  (void)store->upsert_state(state);
}

std::string normalize_thinking_level(std::string value) {
  value = common::to_lower(common::trim(std::move(value)));
  if (value.empty()) {
    return "standard";
  }
  if (value == "low" || value == "minimal" || value == "standard" || value == "medium" ||
      value == "high" || value == "creative") {
    return value == "medium" ? "standard" : value;
  }
  return "standard";
}

std::optional<double> thinking_level_temperature(const std::string &thinking_level,
                                                 const double default_temperature) {
  const std::string normalized = normalize_thinking_level(thinking_level);
  if (normalized == "low" || normalized == "minimal") {
    return std::min(default_temperature, 0.2);
  }
  if (normalized == "high") {
    return std::max(default_temperature, 0.9);
  }
  if (normalized == "creative") {
    return std::max(default_temperature, 0.95);
  }
  return std::nullopt;
}

void append_transcript_entry(sessions::SessionStore *store, const std::string &session_id,
                             const sessions::TranscriptRole role, const std::string &content,
                             const std::string &model,
                             std::unordered_map<std::string, std::string> metadata = {},
                             std::optional<sessions::InputProvenance> input_provenance =
                                 std::nullopt) {
  if (store == nullptr || session_id.empty() || content.empty()) {
    return;
  }
  sessions::TranscriptEntry entry;
  entry.role = role;
  entry.content = content;
  if (!model.empty()) {
    entry.model = model;
  }
  entry.input_provenance = std::move(input_provenance);
  entry.metadata = std::move(metadata);
  (void)store->append_transcript(session_id, entry);
}

} // namespace

std::string RpcResponse::to_json() const {
  std::ostringstream out;
  out << "{";
  out << "\"id\":\"" << common::json_escape(id) << "\",";
  if (error.has_value()) {
    out << "\"error\":\"" << common::json_escape(*error) << "\"";
  } else {
    out << "\"result\":" << to_json_object(result);
  }
  out << "}";
  return out.str();
}

std::string WsServerMessage::to_json() const {
  std::ostringstream out;
  out << "{";
  out << "\"type\":\"" << common::json_escape(type) << "\"";
  if (!id.empty()) {
    out << ",\"id\":\"" << common::json_escape(id) << "\"";
  }
  if (!session.empty()) {
    out << ",\"session\":\"" << common::json_escape(session) << "\"";
  }
  if (error.has_value()) {
    out << ",\"error\":\"" << common::json_escape(*error) << "\"";
  }
  if (!payload.empty()) {
    out << ",\"payload\":" << to_json_object(payload);
  }
  out << "}";
  return out.str();
}

common::Result<WsClientMessage> parse_ws_client_message(const std::string &json) {
  WsClientMessage message;
  message.id = find_json_string_field(json, "id");
  message.type = find_json_string_field(json, "type");
  message.method = find_json_string_field(json, "method");
  message.session = find_json_string_field(json, "session");
  if (message.session.empty()) {
    message.session = find_json_string_field(json, "session_id");
  }
  if (message.type.empty()) {
    // Allow pure RPC envelopes where method is present but type is omitted.
    if (!message.method.empty()) {
      message.type = "rpc";
    } else {
      return common::Result<WsClientMessage>::failure("missing type field");
    }
  }

  const std::string event = find_json_string_field(json, "event");
  if (!event.empty()) {
    message.payload["event"] = event;
  }
  const std::string text = find_json_string_field(json, "text");
  if (!text.empty()) {
    message.payload["text"] = text;
  }
  const std::string token = find_json_string_field(json, "token");
  if (!token.empty()) {
    message.payload["token"] = token;
  }
  const std::string rpc_message = find_json_string_field(json, "message");
  if (!rpc_message.empty()) {
    message.payload["message"] = rpc_message;
  }
  const std::string model = find_json_string_field(json, "model");
  if (!model.empty()) {
    message.payload["model"] = model;
  }
  const std::string channel = find_json_string_field(json, "channel");
  if (!channel.empty()) {
    message.payload["channel"] = channel;
  }
  const std::string thinking_level = find_json_string_field(json, "thinking_level");
  if (!thinking_level.empty()) {
    message.payload["thinking_level"] = thinking_level;
  }
  const std::string delivery_context = find_json_string_field(json, "delivery_context");
  if (!delivery_context.empty()) {
    message.payload["delivery_context"] = delivery_context;
  }
  const std::string group_id = find_json_string_field(json, "group_id");
  if (!group_id.empty()) {
    message.payload["group_id"] = group_id;
  }
  const std::string peer_id = find_json_string_field(json, "peer_id");
  if (!peer_id.empty()) {
    message.payload["peer_id"] = peer_id;
  }
  const std::string provenance_kind = find_json_string_field(json, "input_provenance_kind");
  if (!provenance_kind.empty()) {
    message.payload["input_provenance_kind"] = provenance_kind;
  }
  const std::string provenance_source_session =
      find_json_string_field(json, "input_provenance_source_session_id");
  if (!provenance_source_session.empty()) {
    message.payload["input_provenance_source_session_id"] = provenance_source_session;
  }
  const std::string provenance_source_channel =
      find_json_string_field(json, "input_provenance_source_channel");
  if (!provenance_source_channel.empty()) {
    message.payload["input_provenance_source_channel"] = provenance_source_channel;
  }
  const std::string provenance_source_tool =
      find_json_string_field(json, "input_provenance_source_tool");
  if (!provenance_source_tool.empty()) {
    message.payload["input_provenance_source_tool"] = provenance_source_tool;
  }
  const std::string provenance_source_message =
      find_json_string_field(json, "input_provenance_source_message_id");
  if (!provenance_source_message.empty()) {
    message.payload["input_provenance_source_message_id"] = provenance_source_message;
  }
  const std::string temperature = find_json_string_field(json, "temperature");
  if (!temperature.empty()) {
    message.payload["temperature"] = temperature;
  } else {
    const std::string numeric_temperature = find_json_numeric_field(json, "temperature");
    if (!numeric_temperature.empty()) {
      message.payload["temperature"] = numeric_temperature;
    }
  }
  const std::string key = find_json_string_field(json, "key");
  if (!key.empty()) {
    message.payload["key"] = key;
  }
  const std::string limit = find_json_string_field(json, "limit");
  if (!limit.empty()) {
    message.payload["limit"] = limit;
  } else {
    const std::string numeric_limit = find_json_numeric_field(json, "limit");
    if (!numeric_limit.empty()) {
      message.payload["limit"] = numeric_limit;
    }
  }
  if (!message.session.empty()) {
    message.payload["session_id"] = message.session;
  }
  return common::Result<WsClientMessage>::success(std::move(message));
}

RpcHandler::RpcHandler(std::shared_ptr<agent::AgentEngine> agent, memory::IMemory *memory,
                       sessions::SessionStore *session_store, const config::Config &config)
    : agent_(std::move(agent)), memory_(memory), session_store_(session_store), config_(config) {}

RpcResponse RpcHandler::handle(const RpcRequest &request) {
  if (request.method == "agent.run") {
    return handle_agent_run(request);
  }
  if (request.method == "config.get") {
    return handle_config_get(request);
  }
  if (request.method == "session.list") {
    return handle_session_list(request);
  }
  if (request.method == "session.history") {
    return handle_session_history(request);
  }
  if (request.method == "session.override.set") {
    return handle_session_override_set(request);
  }
  if (request.method == "session.override.get") {
    return handle_session_override_get(request);
  }
  if (request.method == "session.group.list") {
    return handle_session_group_list(request);
  }
  if (request.method == "health") {
    return handle_health(request);
  }

  return RpcResponse{.id = request.id, .error = "unknown method: " + request.method};
}

RpcResponse RpcHandler::handle_agent_run(const RpcRequest &request) {
  if (!request.params.contains("message")) {
    return RpcResponse{.id = request.id, .error = "missing message param"};
  }
  if (agent_ == nullptr) {
    return RpcResponse{.id = request.id, .error = "agent unavailable"};
  }

  const auto model_it = request.params.find("model");
  const std::string model = (model_it != request.params.end() && !model_it->second.empty())
                                ? model_it->second
                                : config_.default_model;
  const auto channel_it = request.params.find("channel");
  const std::string channel = (channel_it != request.params.end() && !channel_it->second.empty())
                                  ? channel_it->second
                                  : "rpc";
  const auto peer_it = request.params.find("peer_id");
  const std::string fallback_peer =
      (peer_it != request.params.end() && !peer_it->second.empty()) ? peer_it->second : "default";
  const auto session_it = request.params.find("session_id");
  const std::string session_id = normalize_session_id(
      (session_it != request.params.end()) ? session_it->second : "", channel, fallback_peer);
  const std::string message = request.params.at("message");

  std::optional<sessions::SessionState> current_state;
  if (session_store_ != nullptr) {
    auto state = session_store_->get_state(session_id);
    if (state.ok()) {
      current_state = state.value();
    }
  }

  const std::string requested_model = common::trim((model_it != request.params.end())
                                                       ? model_it->second
                                                       : "");
  const std::string effective_model =
      !requested_model.empty()
          ? requested_model
          : ((current_state.has_value() && !common::trim(current_state->model).empty())
                 ? common::trim(current_state->model)
                 : config_.default_model);
  const auto thinking_it = request.params.find("thinking_level");
  const std::string thinking_level =
      normalize_thinking_level((thinking_it != request.params.end() &&
                                !common::trim(thinking_it->second).empty())
                                   ? thinking_it->second
                                   : (current_state.has_value()
                                          ? current_state->thinking_level
                                          : "standard"));
  const auto group_it = request.params.find("group_id");
  const std::string group_id =
      (group_it != request.params.end() && !common::trim(group_it->second).empty())
          ? common::trim(group_it->second)
          : (current_state.has_value() ? common::trim(current_state->group_id) : "");
  const auto context_it = request.params.find("delivery_context");
  const std::string delivery_context =
      (context_it != request.params.end() && !common::trim(context_it->second).empty())
          ? common::trim(context_it->second)
          : ((current_state.has_value() && !common::trim(current_state->delivery_context).empty())
                 ? common::trim(current_state->delivery_context)
                 : "rpc");

  sessions::InputProvenance provenance;
  const auto provenance_kind_it = request.params.find("input_provenance_kind");
  provenance.kind =
      (provenance_kind_it != request.params.end() &&
       !common::trim(provenance_kind_it->second).empty())
          ? common::trim(provenance_kind_it->second)
          : "rpc";
  const auto provenance_session_it =
      request.params.find("input_provenance_source_session_id");
  if (provenance_session_it != request.params.end() &&
      !common::trim(provenance_session_it->second).empty()) {
    provenance.source_session_id = common::trim(provenance_session_it->second);
  }
  const auto provenance_channel_it =
      request.params.find("input_provenance_source_channel");
  if (provenance_channel_it != request.params.end() &&
      !common::trim(provenance_channel_it->second).empty()) {
    provenance.source_channel = common::trim(provenance_channel_it->second);
  }
  const auto provenance_tool_it = request.params.find("input_provenance_source_tool");
  if (provenance_tool_it != request.params.end() &&
      !common::trim(provenance_tool_it->second).empty()) {
    provenance.source_tool = common::trim(provenance_tool_it->second);
  }
  const auto provenance_message_it =
      request.params.find("input_provenance_source_message_id");
  if (provenance_message_it != request.params.end() &&
      !common::trim(provenance_message_it->second).empty()) {
    provenance.source_message_id = common::trim(provenance_message_it->second);
  }

  upsert_session_state(session_store_, session_id, effective_model, thinking_level, delivery_context,
                       group_id);
  append_transcript_entry(session_store_, session_id, sessions::TranscriptRole::User, message,
                          effective_model,
                          {{"channel", channel},
                           {"source", "rpc"},
                           {"thinking_level", thinking_level},
                           {"group_id", group_id}},
                          provenance);

  agent::AgentOptions options;
  options.model_override = effective_model;
  const auto temperature_it = request.params.find("temperature");
  if (temperature_it != request.params.end() && !temperature_it->second.empty()) {
    try {
      options.temperature_override = std::stod(temperature_it->second);
    } catch (...) {
      return RpcResponse{.id = request.id, .error = "invalid temperature param"};
    }
  } else if (auto derived_temperature =
                 thinking_level_temperature(thinking_level, config_.default_temperature);
             derived_temperature.has_value()) {
    options.temperature_override = *derived_temperature;
  }

  auto result = agent_->run(message, options);
  if (!result.ok()) {
    append_transcript_entry(session_store_, session_id, sessions::TranscriptRole::System,
                            "agent.run failed: " + result.error(), effective_model,
                            {{"channel", channel},
                             {"source", "rpc"},
                             {"event", "error"},
                             {"thinking_level", thinking_level},
                             {"group_id", group_id}});
    return RpcResponse{.id = request.id, .error = result.error()};
  }

  append_transcript_entry(session_store_, session_id, sessions::TranscriptRole::Assistant,
                          result.value().content, effective_model,
                          {{"channel", channel},
                           {"source", "rpc"},
                           {"duration_ms", std::to_string(result.value().duration.count())},
                           {"tool_calls", std::to_string(result.value().tool_results.size())},
                           {"thinking_level", thinking_level},
                           {"group_id", group_id}});
  upsert_session_state(session_store_, session_id, effective_model, thinking_level, delivery_context,
                       group_id);

  RpcMap map;
  map["content"] = result.value().content;
  map["duration_ms"] = std::to_string(result.value().duration.count());
  map["tool_calls"] = std::to_string(result.value().tool_results.size());
  map["session_id"] = session_id;
  map["model"] = effective_model;
  map["thinking_level"] = thinking_level;
  if (!group_id.empty()) {
    map["group_id"] = group_id;
  }
  return RpcResponse{.id = request.id, .result = std::move(map)};
}

RpcResponse RpcHandler::handle_config_get(const RpcRequest &request) const {
  if (!request.params.contains("key")) {
    return RpcResponse{.id = request.id, .error = "missing key param"};
  }

  const std::string &key = request.params.at("key");
  RpcMap map;
  if (key == "default_provider") {
    map["value"] = config_.default_provider;
  } else if (key == "default_model") {
    map["value"] = config_.default_model;
  } else if (key == "memory.backend") {
    map["value"] = config_.memory.backend;
  } else if (key == "gateway.host") {
    map["value"] = config_.gateway.host;
  } else {
    return RpcResponse{.id = request.id, .error = "unknown key: " + key};
  }

  return RpcResponse{.id = request.id, .result = std::move(map)};
}

RpcResponse RpcHandler::handle_session_list(const RpcRequest &request) const {
  (void)request;
  RpcMap map;
  if (session_store_ == nullptr) {
    map["count"] = "0";
    map["sessions"] = "[]";
    return RpcResponse{.id = request.id, .result = std::move(map)};
  }
  auto states = session_store_->list_states();
  if (!states.ok()) {
    return RpcResponse{.id = request.id, .error = states.error()};
  }
  map["count"] = std::to_string(states.value().size());
  for (std::size_t i = 0; i < states.value().size(); ++i) {
    map["session_" + std::to_string(i)] = states.value()[i].session_id;
  }
  return RpcResponse{.id = request.id, .result = std::move(map)};
}

RpcResponse RpcHandler::handle_session_history(const RpcRequest &request) const {
  if (session_store_ == nullptr) {
    return RpcResponse{.id = request.id, .error = "session store unavailable"};
  }
  const auto session_it = request.params.find("session_id");
  if (session_it == request.params.end() || common::trim(session_it->second).empty()) {
    return RpcResponse{.id = request.id, .error = "missing session_id param"};
  }
  std::size_t limit = 50;
  const auto limit_it = request.params.find("limit");
  if (limit_it != request.params.end()) {
    try {
      limit = static_cast<std::size_t>(std::stoull(limit_it->second));
    } catch (...) {
      limit = 50;
    }
  }
  const std::string session_id = common::trim(session_it->second);
  auto entries = session_store_->load_transcript(session_id, limit);
  if (!entries.ok()) {
    return RpcResponse{.id = request.id, .error = entries.error()};
  }
  std::ostringstream history;
  history << "[";
  for (std::size_t i = 0; i < entries.value().size(); ++i) {
    if (i > 0) {
      history << ",";
    }
    history << "{";
    history << "\"role\":\""
            << common::json_escape(sessions::role_to_string(entries.value()[i].role))
            << "\",";
    history << "\"content\":\"" << common::json_escape(entries.value()[i].content) << "\",";
    history << "\"timestamp\":\"" << common::json_escape(entries.value()[i].timestamp)
            << "\"";
    if (entries.value()[i].model.has_value() && !entries.value()[i].model->empty()) {
      history << ",\"model\":\"" << common::json_escape(*entries.value()[i].model) << "\"";
    }
    if (!entries.value()[i].metadata.empty()) {
      history << ",\"metadata\":{";
      bool first_meta = true;
      for (const auto &[meta_key, meta_value] : entries.value()[i].metadata) {
        if (!first_meta) {
          history << ",";
        }
        first_meta = false;
        history << "\"" << common::json_escape(meta_key) << "\":\""
                << common::json_escape(meta_value) << "\"";
      }
      history << "}";
    }
    if (entries.value()[i].input_provenance.has_value() &&
        !common::trim(entries.value()[i].input_provenance->kind).empty()) {
      history << ",\"input_provenance\":{";
      history << "\"kind\":\""
              << common::json_escape(entries.value()[i].input_provenance->kind) << "\"";
      if (entries.value()[i].input_provenance->source_session_id.has_value()) {
        history << ",\"source_session_id\":\""
                << common::json_escape(
                       *entries.value()[i].input_provenance->source_session_id)
                << "\"";
      }
      if (entries.value()[i].input_provenance->source_channel.has_value()) {
        history << ",\"source_channel\":\""
                << common::json_escape(*entries.value()[i].input_provenance->source_channel)
                << "\"";
      }
      if (entries.value()[i].input_provenance->source_tool.has_value()) {
        history << ",\"source_tool\":\""
                << common::json_escape(*entries.value()[i].input_provenance->source_tool)
                << "\"";
      }
      if (entries.value()[i].input_provenance->source_message_id.has_value()) {
        history << ",\"source_message_id\":\""
                << common::json_escape(
                       *entries.value()[i].input_provenance->source_message_id)
                << "\"";
      }
      history << "}";
    }
    history << "}";
  }
  history << "]";

  RpcMap map;
  map["session_id"] = session_id;
  map["entries_json"] = history.str();
  map["count"] = std::to_string(entries.value().size());
  if (!entries.value().empty()) {
    map["last_role"] = sessions::role_to_string(entries.value().back().role);
    map["last_content"] = entries.value().back().content;
  }
  return RpcResponse{.id = request.id, .result = std::move(map)};
}

RpcResponse RpcHandler::handle_session_override_set(const RpcRequest &request) {
  if (session_store_ == nullptr) {
    return RpcResponse{.id = request.id, .error = "session store unavailable"};
  }
  const auto session_it = request.params.find("session_id");
  if (session_it == request.params.end() || common::trim(session_it->second).empty()) {
    return RpcResponse{.id = request.id, .error = "missing session_id param"};
  }

  const auto channel_it = request.params.find("channel");
  const std::string channel = (channel_it != request.params.end() && !channel_it->second.empty())
                                  ? common::trim(channel_it->second)
                                  : "rpc";
  const std::string session_id =
      normalize_session_id(common::trim(session_it->second), channel, common::trim(session_it->second));
  sessions::SessionState state;
  auto existing = session_store_->get_state(session_id);
  if (existing.ok()) {
    state = existing.value();
  } else {
    state.session_id = session_id;
  }

  const auto model_it = request.params.find("model");
  if (model_it != request.params.end() && !common::trim(model_it->second).empty()) {
    state.model = common::trim(model_it->second);
  } else if (state.model.empty()) {
    state.model = config_.default_model;
  }

  const auto thinking_it = request.params.find("thinking_level");
  if (thinking_it != request.params.end() && !common::trim(thinking_it->second).empty()) {
    state.thinking_level = normalize_thinking_level(thinking_it->second);
  } else if (state.thinking_level.empty()) {
    state.thinking_level = "standard";
  }

  const auto context_it = request.params.find("delivery_context");
  if (context_it != request.params.end() && !common::trim(context_it->second).empty()) {
    state.delivery_context = common::trim(context_it->second);
  } else if (state.delivery_context.empty()) {
    state.delivery_context = "rpc";
  }

  const auto group_it = request.params.find("group_id");
  if (group_it != request.params.end()) {
    state.group_id = common::trim(group_it->second);
  }

  const auto upsert = session_store_->upsert_state(state);
  if (!upsert.ok()) {
    return RpcResponse{.id = request.id, .error = upsert.error()};
  }

  RpcMap map;
  map["session_id"] = state.session_id;
  map["model"] = state.model;
  map["thinking_level"] = state.thinking_level;
  map["delivery_context"] = state.delivery_context;
  if (!common::trim(state.group_id).empty()) {
    map["group_id"] = state.group_id;
  }
  return RpcResponse{.id = request.id, .result = std::move(map)};
}

RpcResponse RpcHandler::handle_session_override_get(const RpcRequest &request) const {
  if (session_store_ == nullptr) {
    return RpcResponse{.id = request.id, .error = "session store unavailable"};
  }
  const auto session_it = request.params.find("session_id");
  if (session_it == request.params.end() || common::trim(session_it->second).empty()) {
    return RpcResponse{.id = request.id, .error = "missing session_id param"};
  }

  const auto channel_it = request.params.find("channel");
  const std::string channel = (channel_it != request.params.end() && !channel_it->second.empty())
                                  ? common::trim(channel_it->second)
                                  : "rpc";
  const std::string session_id =
      normalize_session_id(common::trim(session_it->second), channel, common::trim(session_it->second));
  auto state = session_store_->get_state(session_id);
  if (!state.ok() && state.error() != "session not found") {
    return RpcResponse{.id = request.id, .error = state.error()};
  }

  RpcMap map;
  if (!state.ok()) {
    map["session_id"] = session_id;
    map["model"] = config_.default_model;
    map["thinking_level"] = "standard";
    map["delivery_context"] = "default";
    return RpcResponse{.id = request.id, .result = std::move(map)};
  }

  map["session_id"] = state.value().session_id;
  map["model"] = state.value().model.empty() ? config_.default_model : state.value().model;
  map["thinking_level"] =
      state.value().thinking_level.empty() ? "standard" : state.value().thinking_level;
  map["delivery_context"] =
      state.value().delivery_context.empty() ? "default" : state.value().delivery_context;
  if (!common::trim(state.value().group_id).empty()) {
    map["group_id"] = state.value().group_id;
  }
  return RpcResponse{.id = request.id, .result = std::move(map)};
}

RpcResponse RpcHandler::handle_session_group_list(const RpcRequest &request) const {
  if (session_store_ == nullptr) {
    return RpcResponse{.id = request.id, .error = "session store unavailable"};
  }
  const auto group_it = request.params.find("group_id");
  if (group_it == request.params.end() || common::trim(group_it->second).empty()) {
    return RpcResponse{.id = request.id, .error = "missing group_id param"};
  }
  const std::string group_id = common::trim(group_it->second);
  auto states = session_store_->list_states_by_group(group_id);
  if (!states.ok()) {
    return RpcResponse{.id = request.id, .error = states.error()};
  }

  RpcMap map;
  map["group_id"] = group_id;
  map["count"] = std::to_string(states.value().size());
  for (std::size_t i = 0; i < states.value().size(); ++i) {
    map["session_" + std::to_string(i)] = states.value()[i].session_id;
  }
  return RpcResponse{.id = request.id, .result = std::move(map)};
}

RpcResponse RpcHandler::handle_health(const RpcRequest &request) const {
  RpcMap map;
  map["status"] = "ok";
  map["provider"] = config_.default_provider;
  map["memory"] = (memory_ != nullptr && memory_->health_check()) ? "ok" : "degraded";
  return RpcResponse{.id = request.id, .result = std::move(map)};
}

} // namespace ghostclaw::gateway
