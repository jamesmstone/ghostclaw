#include "ghostclaw/gateway/server.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"
#include "ghostclaw/config/config.hpp"
#include "ghostclaw/observability/global.hpp"
#include "ghostclaw/providers/traits.hpp"
#include "ghostclaw/sessions/session_key.hpp"
#include "ghostclaw/tunnel/factory.hpp"

#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ghostclaw::gateway {

namespace {

constexpr std::size_t kMaxBodySize = 64 * 1024;
constexpr int kListenBacklog = 64;

std::string trim_copy(const std::string &value) { return common::trim(value); }
bool is_loopback_host(const std::string &host) {
  const std::string lowered = common::to_lower(common::trim(host));
  return lowered == "127.0.0.1" || lowered == "localhost" || lowered == "::1" ||
         lowered == "[::1]";
}

std::string header_lookup(const HttpRequest &request, const std::string &key) {
  const std::string lowered = common::to_lower(key);
  auto it = request.headers.find(lowered);
  if (it == request.headers.end()) {
    return "";
  }
  return it->second;
}

std::string json_escape(const std::string &value) { return common::json_escape(value); }

std::string json_string(const std::string &value) {
  return "\"" + json_escape(value) + "\"";
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
                          const std::string &delivery_context, const std::string &group_id = "") {
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

std::string find_json_string_field(const std::string &json, const std::string &field) {
  return common::json_get_string(json, field);
}

std::string find_json_numeric_field(const std::string &json, const std::string &field) {
  return common::json_get_number(json, field);
}

std::optional<sessions::SessionState> lookup_session_state(sessions::SessionStore *store,
                                                           const std::string &session_id) {
  if (store == nullptr || session_id.empty()) {
    return std::nullopt;
  }
  auto state = store->get_state(session_id);
  if (!state.ok()) {
    return std::nullopt;
  }
  return state.value();
}

std::string sha256_hex(const std::string &text) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(text.data()), text.size(), digest);
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (unsigned char c : digest) {
    out << std::setw(2) << static_cast<int>(c);
  }
  return out.str();
}

std::string status_text(int status) {
  switch (status) {
  case 200:
    return "OK";
  case 400:
    return "Bad Request";
  case 401:
    return "Unauthorized";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 413:
    return "Payload Too Large";
  case 429:
    return "Too Many Requests";
  case 500:
    return "Internal Server Error";
  default:
    return "OK";
  }
}

std::unordered_map<std::string, std::string> parse_query_string(const std::string &query) {
  std::unordered_map<std::string, std::string> out;
  std::stringstream stream(query);
  std::string part;
  while (std::getline(stream, part, '&')) {
    if (part.empty()) {
      continue;
    }
    const auto eq = part.find('=');
    if (eq == std::string::npos) {
      out[part] = "";
      continue;
    }
    out[part.substr(0, eq)] = part.substr(eq + 1);
  }
  return out;
}

HttpResponse make_json_response(int status, const std::string &body) {
  HttpResponse response;
  response.status = status;
  response.content_type = "application/json";
  response.body = body;
  return response;
}

HttpResponse make_text_response(int status, const std::string &body) {
  HttpResponse response;
  response.status = status;
  response.content_type = "text/plain";
  response.body = body;
  return response;
}

std::string render_http_response(const HttpResponse &response) {
  std::ostringstream out;
  out << "HTTP/1.1 " << response.status << " " << status_text(response.status) << "\r\n";
  out << "Content-Type: " << response.content_type << "\r\n";
  out << "Content-Length: " << response.body.size() << "\r\n";
  out << "Connection: close\r\n";
  for (const auto &[k, v] : response.headers) {
    out << k << ": " << v << "\r\n";
  }
  out << "\r\n";
  out << response.body;
  return out.str();
}

common::Result<HttpRequest> parse_http_request(const std::string &raw) {
  const auto header_end = raw.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return common::Result<HttpRequest>::failure("incomplete request");
  }

  const std::string headers_part = raw.substr(0, header_end);
  const std::string body = raw.substr(header_end + 4);

  std::istringstream head_stream(headers_part);
  std::string line;
  if (!std::getline(head_stream, line)) {
    return common::Result<HttpRequest>::failure("missing request line");
  }
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }

  std::istringstream req_line(line);
  HttpRequest request;
  std::string http_version;
  if (!(req_line >> request.method >> request.raw_path >> http_version)) {
    return common::Result<HttpRequest>::failure("invalid request line");
  }

  while (std::getline(head_stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    const std::string key = common::to_lower(trim_copy(line.substr(0, colon)));
    const std::string value = trim_copy(line.substr(colon + 1));
    request.headers[key] = value;
  }

  request.body = body;
  const auto qpos = request.raw_path.find('?');
  if (qpos == std::string::npos) {
    request.path = request.raw_path;
  } else {
    request.path = request.raw_path.substr(0, qpos);
    request.query = parse_query_string(request.raw_path.substr(qpos + 1));
  }

  return common::Result<HttpRequest>::success(std::move(request));
}

} // namespace

GatewayServer::GatewayServer(const config::Config &config, std::shared_ptr<agent::AgentEngine> agent,
                             memory::IMemory *memory)
    : config_(config), agent_(std::move(agent)), memory_(memory),
      tunnel_(tunnel::create_tunnel(config.tunnel)) {}

GatewayServer::~GatewayServer() { stop(); }

common::Status GatewayServer::start(const GatewayOptions &options) {
#ifdef _WIN32
  return common::Status::error("gateway server is not implemented on Windows");
#else
  if (running_) {
    return common::Status::error("gateway already running");
  }

  tunnel_public_url_.clear();
  if (tunnel_ != nullptr && tunnel_->name() != "none" && options.port != 0) {
    const auto started_tunnel = tunnel_->start("127.0.0.1", options.port);
    if (started_tunnel.ok()) {
      tunnel_public_url_ = started_tunnel.value();
    }
  }

  if (!is_loopback_host(options.host) && !config_.gateway.allow_public_bind) {
    const bool has_tunnel = tunnel_ != nullptr && tunnel_->name() != "none";
    if (!has_tunnel) {
      if (tunnel_ != nullptr) {
        (void)tunnel_->stop();
      }
      return common::Status::error(
          "refusing public bind without tunnel or allow_public_bind=true");
    }
  }

  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    return common::Status::error("failed to create listen socket");
  }

  int reuse = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(options.port);
  if (inet_pton(AF_INET, options.host.c_str(), &addr.sin_addr) != 1) {
    close(listen_fd_);
    listen_fd_ = -1;
    return common::Status::error("invalid bind host");
  }

  if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    const std::string msg = std::strerror(errno);
    close(listen_fd_);
    listen_fd_ = -1;
    return common::Status::error("bind failed: " + msg);
  }

  if (listen(listen_fd_, kListenBacklog) != 0) {
    const std::string msg = std::strerror(errno);
    close(listen_fd_);
    listen_fd_ = -1;
    return common::Status::error("listen failed: " + msg);
  }

  sockaddr_in actual{};
  socklen_t actual_len = sizeof(actual);
  if (getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&actual), &actual_len) == 0) {
    bound_port_ = ntohs(actual.sin_port);
  } else {
    bound_port_ = options.port;
  }

  if (tunnel_ != nullptr && tunnel_->name() != "none" && tunnel_public_url_.empty() &&
      bound_port_ != 0) {
    const auto started_tunnel = tunnel_->start("127.0.0.1", bound_port_);
    if (started_tunnel.ok()) {
      tunnel_public_url_ = started_tunnel.value();
    }
  }

  auto bind_validation = validate_bind_address(options.host);
  if (!bind_validation.ok()) {
    if (listen_fd_ >= 0) {
      shutdown(listen_fd_, SHUT_RDWR);
      close(listen_fd_);
      listen_fd_ = -1;
    }
    if (tunnel_ != nullptr) {
      (void)tunnel_->stop();
    }
    return bind_validation;
  }

  if (config_.gateway.require_pairing) {
    pairing_code_ = security::generate_pairing_code();
    pairing_state_ = std::make_unique<security::PairingState>(pairing_code_, 5);
  } else {
    pairing_state_.reset();
    pairing_code_.clear();
  }

  if (!session_store_) {
    auto workspace = config::workspace_dir();
    if (workspace.ok()) {
      session_store_ = std::make_unique<sessions::SessionStore>(workspace.value() / "sessions");
    } else {
      session_store_ = std::make_unique<sessions::SessionStore>(
          std::filesystem::temp_directory_path() / "ghostclaw-sessions-fallback");
    }
  }
  if (config_.gateway.session_send_policy_enabled) {
    send_policy_ = std::make_unique<sessions::SessionSendPolicy>(
        config_.gateway.session_send_policy_max_per_window,
        std::chrono::seconds(config_.gateway.session_send_policy_window_seconds));
  } else {
    send_policy_.reset();
  }

  websocket_port_ = 0;
  websocket_server_.reset();
  if (config_.gateway.websocket_enabled) {
    auto server = std::make_unique<WebSocketServer>();
    WebSocketServer *ws_raw = server.get();
    std::uint16_t ws_port = config_.gateway.websocket_port;
    if (ws_port == 0) {
      if (bound_port_ == std::numeric_limits<std::uint16_t>::max()) {
        if (listen_fd_ >= 0) {
          shutdown(listen_fd_, SHUT_RDWR);
          close(listen_fd_);
          listen_fd_ = -1;
        }
        if (tunnel_ != nullptr) {
          (void)tunnel_->stop();
        }
        return common::Status::error(
            "websocket auto-port unavailable when gateway port is 65535");
      }
      ws_port = static_cast<std::uint16_t>(bound_port_ + 1);
    }
    WebSocketOptions ws_options;
    ws_options.host = config_.gateway.websocket_host;
    ws_options.port = ws_port;
    ws_options.tls_enabled = config_.gateway.websocket_tls_enabled;
    ws_options.tls_cert_file = config_.gateway.websocket_tls_cert_file;
    ws_options.tls_key_file = config_.gateway.websocket_tls_key_file;
    ws_options.require_authorization = config_.gateway.require_pairing;
    ws_options.authorize = [this](const std::string &authorization) {
      if (!config_.gateway.require_pairing) {
        return true;
      }
      return validate_bearer(authorization);
    };
    ws_options.rpc_handler =
        [this, ws_raw](const WsClientMessage &request,
                       const std::function<void(const RpcMap &)> &emit_event)
        -> common::Result<RpcMap> {
      const std::string method = request.method.empty() ? request.type : request.method;
      if (method.empty() || method == "rpc") {
        return common::Result<RpcMap>::failure("missing rpc method");
      }

      if (method == "ping") {
        return common::Result<RpcMap>::success({{"status", "ok"}});
      }

      if (method == "agent.run") {
        const auto message_it = request.payload.find("message");
        if (message_it == request.payload.end() || message_it->second.empty()) {
          return common::Result<RpcMap>::failure("missing message");
        }
        if (!agent_) {
          return common::Result<RpcMap>::failure("agent unavailable");
        }

        const auto peer_it = request.payload.find("peer_id");
        const std::string fallback_peer =
            (peer_it != request.payload.end() && !peer_it->second.empty()) ? peer_it->second
                                                                            : "default";
        const auto session_param_it = request.payload.find("session_id");
        const std::string session_candidate =
            (session_param_it != request.payload.end() && !session_param_it->second.empty())
                ? session_param_it->second
                : request.session;
        const std::string session =
            normalize_session_id(session_candidate, "websocket", fallback_peer);

        if (send_policy_ != nullptr && !send_policy_->allow(session)) {
          const RpcMap rate_limited{{"event", "assistant.error"},
                                    {"error", "session_rate_limited"},
                                    {"channel", "websocket"}};
          emit_event(rate_limited);
          if (ws_raw != nullptr) {
            (void)ws_raw->publish_session_event(session, rate_limited);
          }
          return common::Result<RpcMap>::failure("session send policy rate limit exceeded");
        }

        const auto current_state = lookup_session_state(session_store_.get(), session);
        const auto model_it = request.payload.find("model");
        const std::string model =
            (model_it != request.payload.end() && !common::trim(model_it->second).empty())
                ? common::trim(model_it->second)
                : ((current_state.has_value() && !common::trim(current_state->model).empty())
                       ? common::trim(current_state->model)
                       : config_.default_model);
        const auto thinking_it = request.payload.find("thinking_level");
        const std::string thinking_level =
            normalize_thinking_level((thinking_it != request.payload.end() &&
                                      !common::trim(thinking_it->second).empty())
                                         ? thinking_it->second
                                         : (current_state.has_value()
                                                ? current_state->thinking_level
                                                : "standard"));
        const auto group_it = request.payload.find("group_id");
        const std::string group_id =
            (group_it != request.payload.end() && !common::trim(group_it->second).empty())
                ? common::trim(group_it->second)
                : (current_state.has_value() ? common::trim(current_state->group_id) : "");

        sessions::InputProvenance provenance;
        const auto provenance_kind_it = request.payload.find("input_provenance_kind");
        provenance.kind =
            (provenance_kind_it != request.payload.end() &&
             !common::trim(provenance_kind_it->second).empty())
                ? common::trim(provenance_kind_it->second)
                : "websocket";
        const auto provenance_session_it =
            request.payload.find("input_provenance_source_session_id");
        if (provenance_session_it != request.payload.end() &&
            !common::trim(provenance_session_it->second).empty()) {
          provenance.source_session_id = common::trim(provenance_session_it->second);
        }
        const auto provenance_channel_it =
            request.payload.find("input_provenance_source_channel");
        if (provenance_channel_it != request.payload.end() &&
            !common::trim(provenance_channel_it->second).empty()) {
          provenance.source_channel = common::trim(provenance_channel_it->second);
        }
        const auto provenance_tool_it =
            request.payload.find("input_provenance_source_tool");
        if (provenance_tool_it != request.payload.end() &&
            !common::trim(provenance_tool_it->second).empty()) {
          provenance.source_tool = common::trim(provenance_tool_it->second);
        }
        const auto provenance_message_it =
            request.payload.find("input_provenance_source_message_id");
        if (provenance_message_it != request.payload.end() &&
            !common::trim(provenance_message_it->second).empty()) {
          provenance.source_message_id = common::trim(provenance_message_it->second);
        }

        upsert_session_state(session_store_.get(), session, model, thinking_level, "websocket",
                             group_id);
        append_transcript_entry(session_store_.get(), session, sessions::TranscriptRole::User,
                                message_it->second, model,
                                {{"channel", "websocket"},
                                 {"source", "rpc"},
                                 {"thinking_level", thinking_level},
                                 {"group_id", group_id}},
                                provenance);

        const auto lane = session_lane(session);
        std::unique_lock<std::mutex> lane_lock(*lane, std::defer_lock);
        if (!lane_lock.try_lock()) {
          const RpcMap queued{{"event", "assistant.queued"}, {"channel", "websocket"}};
          emit_event(queued);
          if (ws_raw != nullptr) {
            (void)ws_raw->publish_session_event(session, queued);
          }
          lane_lock.lock();
        }

        const RpcMap start{{"event", "assistant.start"}, {"channel", "websocket"}};
        emit_event(start);
        if (ws_raw != nullptr) {
          (void)ws_raw->publish_session_event(session, start);
        }

        std::string stream_error;
        bool stream_failed = false;
        agent::AgentResponse response;
        agent::AgentOptions run_options;
        run_options.model_override = model;
        const auto temperature_it = request.payload.find("temperature");
        if (temperature_it != request.payload.end() && !temperature_it->second.empty()) {
          try {
            run_options.temperature_override = std::stod(temperature_it->second);
          } catch (...) {
            return common::Result<RpcMap>::failure("invalid temperature");
          }
        } else if (auto derived_temperature =
                       thinking_level_temperature(thinking_level, config_.default_temperature);
                   derived_temperature.has_value()) {
          run_options.temperature_override = *derived_temperature;
        }

        auto status = agent_->run_stream(
            message_it->second,
            {.on_token =
                 [&](std::string_view token) {
                   const RpcMap event{{"event", "assistant.token"}, {"text", std::string(token)}};
                   emit_event(event);
                   if (ws_raw != nullptr) {
                     (void)ws_raw->publish_session_event(session, event);
                   }
                 },
             .on_done = [&](const agent::AgentResponse &done) { response = done; },
             .on_error =
                 [&](const std::string &error) {
                   stream_failed = true;
                   stream_error = error;
                 }},
            run_options);
        if (!status.ok()) {
          stream_failed = true;
          stream_error = status.error();
        }
        if (stream_failed) {
          const RpcMap event{{"event", "assistant.error"}, {"error", stream_error}};
          emit_event(event);
          if (ws_raw != nullptr) {
            (void)ws_raw->publish_session_event(session, event);
          }
          append_transcript_entry(session_store_.get(), session, sessions::TranscriptRole::System,
                                  "agent.run failed: " + stream_error, model,
                                  {{"channel", "websocket"},
                                   {"source", "rpc"},
                                   {"event", "assistant.error"},
                                   {"thinking_level", thinking_level},
                                   {"group_id", group_id}});
          return common::Result<RpcMap>::failure(stream_error);
        }

        const RpcMap done{{"event", "assistant.done"},
                          {"duration_ms", std::to_string(response.duration.count())},
                          {"tool_calls", std::to_string(response.tool_results.size())}};
        emit_event(done);
        if (ws_raw != nullptr) {
          (void)ws_raw->publish_session_event(session, done);
        }
        append_transcript_entry(
            session_store_.get(), session, sessions::TranscriptRole::Assistant, response.content,
            model,
            {{"channel", "websocket"},
             {"source", "rpc"},
             {"duration_ms", std::to_string(response.duration.count())},
             {"tool_calls", std::to_string(response.tool_results.size())},
             {"thinking_level", thinking_level},
             {"group_id", group_id}});
        upsert_session_state(session_store_.get(), session, model, thinking_level, "websocket",
                             group_id);

        RpcMap result;
        result["content"] = response.content;
        result["session_id"] = session;
        result["duration_ms"] = std::to_string(response.duration.count());
        result["tool_calls"] = std::to_string(response.tool_results.size());
        result["model"] = model;
        result["thinking_level"] = thinking_level;
        if (!group_id.empty()) {
          result["group_id"] = group_id;
        }
        return common::Result<RpcMap>::success(std::move(result));
      }

      RpcHandler rpc(agent_, memory_, session_store_.get(), config_);
      RpcRequest rpc_request;
      rpc_request.id = request.id;
      rpc_request.method = method;
      rpc_request.params = request.payload;
      if (!request.session.empty() && !rpc_request.params.contains("session_id")) {
        rpc_request.params["session_id"] =
            normalize_session_id(request.session, "websocket", request.session);
      }
      auto response = rpc.handle(rpc_request);
      if (response.error.has_value()) {
        return common::Result<RpcMap>::failure(*response.error);
      }
      return common::Result<RpcMap>::success(std::move(response.result));
    };

    auto ws_status = server->start(ws_options);
    if (!ws_status.ok()) {
      if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
      }
      if (tunnel_ != nullptr) {
        (void)tunnel_->stop();
      }
      return common::Status::error("failed to start websocket sidecar: " + ws_status.error());
    }
    websocket_port_ = server->port();
    websocket_server_ = std::move(server);
  }

  running_ = true;
  accept_thread_ = std::thread([this]() { accept_loop(); });
  return common::Status::success();
#endif
}

void GatewayServer::stop() {
#ifndef _WIN32
  if (!running_) {
    return;
  }
  running_ = false;
  if (listen_fd_ >= 0) {
    shutdown(listen_fd_, SHUT_RDWR);
    close(listen_fd_);
    listen_fd_ = -1;
  }
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
  if (websocket_server_ != nullptr) {
    websocket_server_->stop();
    websocket_server_.reset();
  }
  websocket_port_ = 0;
  session_store_.reset();
  send_policy_.reset();
  if (tunnel_ != nullptr) {
    (void)tunnel_->stop();
  }
  tunnel_public_url_.clear();
  {
    std::lock_guard<std::mutex> lock(session_lanes_mutex_);
    session_lanes_.clear();
  }
#endif
}

std::uint16_t GatewayServer::port() const { return bound_port_; }

std::string GatewayServer::pairing_code() const { return pairing_code_; }

bool GatewayServer::is_running() const { return running_.load(); }

std::uint16_t GatewayServer::websocket_port() const { return websocket_port_; }

std::optional<std::string> GatewayServer::public_url() const {
  if (!tunnel_public_url_.empty()) {
    return tunnel_public_url_;
  }
  if (tunnel_ == nullptr) {
    return std::nullopt;
  }
  return tunnel_->public_url();
}

std::shared_ptr<std::mutex> GatewayServer::session_lane(const std::string &session_id) {
  const std::string key = common::trim(session_id).empty() ? "default" : common::trim(session_id);
  std::lock_guard<std::mutex> lock(session_lanes_mutex_);
  auto it = session_lanes_.find(key);
  if (it != session_lanes_.end()) {
    if (auto existing = it->second.lock()) {
      return existing;
    }
    session_lanes_.erase(it);
  }
  auto created = std::make_shared<std::mutex>();
  session_lanes_[key] = created;
  return created;
}

common::Status GatewayServer::validate_bind_address(const std::string &host) const {
  if (!is_loopback_host(host)) {
    const bool tunnel_active = public_url().has_value();
    if (!tunnel_active && !config_.gateway.allow_public_bind) {
      return common::Status::error(
          "refusing public bind without tunnel or allow_public_bind=true");
    }
  }
  return common::Status::success();
}

HttpResponse GatewayServer::dispatch_for_test(const HttpRequest &request) {
  if (request.method == "GET" && request.path == "/health") {
    return handle_health(request);
  }
  if (request.method == "POST" && request.path == "/pair") {
    return handle_pair(request);
  }
  if (request.method == "POST" && request.path == "/webhook") {
    return handle_webhook(request);
  }
  if (request.method == "GET" && request.path == "/whatsapp") {
    return handle_whatsapp_verify(request);
  }
  if (request.method == "POST" && request.path == "/whatsapp") {
    return handle_whatsapp_message(request);
  }
  return make_json_response(404, R"({"error":"not found"})");
}

HttpResponse GatewayServer::handle_health(const HttpRequest &) const {
  std::ostringstream body;
  body << "{";
  body << "\"status\":\"ok\",";
  body << "\"version\":\"0.1.0\",";
  body << "\"provider\":" << json_string(config_.default_provider) << ",";
  body << "\"components\":{";
  body << "\"gateway\":\"ok\",";
  body << "\"websocket\":\""
       << ((websocket_server_ != nullptr && websocket_server_->is_running()) ? "ok" : "disabled")
       << "\",";
  body << "\"memory\":\"" << ((memory_ != nullptr && memory_->health_check()) ? "ok" : "degraded")
       << "\"";
  body << "}";
  if (websocket_port_ != 0) {
    body << ",\"websocket_port\":" << websocket_port_;
  }
  body << "}";
  return make_json_response(200, body.str());
}

HttpResponse GatewayServer::handle_pair(const HttpRequest &request) {
  if (!config_.gateway.require_pairing) {
    return make_json_response(200, R"({"status":"pairing_disabled"})");
  }

  if (!pairing_state_) {
    return make_json_response(500, R"({"error":"pairing_state_missing"})");
  }

  std::string code = header_lookup(request, "x-pairing-code");
  if (code.empty()) {
    code = find_json_string_field(request.body, "code");
  }
  if (code.empty()) {
    return make_json_response(400, R"({"error":"missing_pairing_code"})");
  }

  auto result = pairing_state_->verify(code);
  if (result.type == security::PairingResultType::Success) {
    std::ostringstream body;
    body << "{\"status\":\"paired\",\"token\":" << json_string(result.bearer_token) << "}";
    return make_json_response(200, body.str());
  }
  if (result.type == security::PairingResultType::LockedOut) {
    auto response = make_json_response(429, R"({"error":"locked_out"})");
    response.headers["Retry-After"] = std::to_string(result.retry_after_seconds);
    return response;
  }
  return make_json_response(401, R"({"error":"invalid_pairing_code"})");
}

bool GatewayServer::validate_bearer(const std::string &authorization) const {
  constexpr std::string_view prefix = "Bearer ";
  if (authorization.rfind(prefix.data(), 0) != 0) {
    return false;
  }
  const std::string token = authorization.substr(prefix.size());
  const std::string token_hash = sha256_hex(token);

  if (pairing_state_) {
    for (const auto &stored_hash : pairing_state_->token_hashes()) {
      if (security::constant_time_equals(stored_hash, token_hash)) {
        return true;
      }
    }
  }

  for (const auto &stored : config_.gateway.paired_tokens) {
    if (security::constant_time_equals(stored, token) ||
        security::constant_time_equals(stored, token_hash)) {
      return true;
    }
  }
  return false;
}

HttpResponse GatewayServer::handle_webhook(const HttpRequest &request) {
  if (config_.gateway.require_pairing) {
    const std::string auth = header_lookup(request, "authorization");
    if (!validate_bearer(auth)) {
      return make_json_response(401, R"({"error":"unauthorized"})");
    }
  }

  const std::string message = find_json_string_field(request.body, "message");
  const std::string session = [&]() {
    const std::string by_session = find_json_string_field(request.body, "session");
    if (!by_session.empty()) {
      return normalize_session_id(by_session, "webhook", by_session);
    }
    const std::string by_session_id = find_json_string_field(request.body, "session_id");
    if (!by_session_id.empty()) {
      return normalize_session_id(by_session_id, "webhook", by_session_id);
    }
    return normalize_session_id("", "webhook", "default");
  }();
  if (message.empty()) {
    return make_json_response(400, R"({"error":"invalid_body"})");
  }

  if (send_policy_ != nullptr && !send_policy_->allow(session)) {
    return make_json_response(429, R"({"error":"session_rate_limited"})");
  }

  const auto current_state = lookup_session_state(session_store_.get(), session);
  const std::string requested_model = common::trim(find_json_string_field(request.body, "model"));
  const std::string model =
      !requested_model.empty()
          ? requested_model
          : ((current_state.has_value() && !common::trim(current_state->model).empty())
                 ? common::trim(current_state->model)
                 : config_.default_model);
  const std::string requested_thinking =
      common::trim(find_json_string_field(request.body, "thinking_level"));
  const std::string thinking_level = normalize_thinking_level(
      !requested_thinking.empty()
          ? requested_thinking
          : (current_state.has_value() ? current_state->thinking_level : "standard"));
  const std::string requested_group = common::trim(find_json_string_field(request.body, "group_id"));
  const std::string group_id =
      !requested_group.empty()
          ? requested_group
          : (current_state.has_value() ? common::trim(current_state->group_id) : "");

  sessions::InputProvenance provenance;
  const std::string provenance_kind =
      common::trim(find_json_string_field(request.body, "input_provenance_kind"));
  provenance.kind = provenance_kind.empty() ? "webhook" : provenance_kind;
  const std::string provenance_session =
      common::trim(find_json_string_field(request.body, "input_provenance_source_session_id"));
  if (!provenance_session.empty()) {
    provenance.source_session_id = provenance_session;
  }
  const std::string provenance_channel =
      common::trim(find_json_string_field(request.body, "input_provenance_source_channel"));
  if (!provenance_channel.empty()) {
    provenance.source_channel = provenance_channel;
  }
  const std::string provenance_tool =
      common::trim(find_json_string_field(request.body, "input_provenance_source_tool"));
  if (!provenance_tool.empty()) {
    provenance.source_tool = provenance_tool;
  }
  const std::string provenance_message_id =
      common::trim(find_json_string_field(request.body, "input_provenance_source_message_id"));
  if (!provenance_message_id.empty()) {
    provenance.source_message_id = provenance_message_id;
  }

  observability::record_channel_message("webhook", "inbound");
  if (!agent_) {
    observability::record_error("gateway.webhook", "agent unavailable");
    return make_json_response(500, R"({"error":"agent_unavailable"})");
  }
  upsert_session_state(session_store_.get(), session, model, thinking_level, "webhook", group_id);
  append_transcript_entry(session_store_.get(), session, sessions::TranscriptRole::User, message,
                          model,
                          {{"channel", "webhook"},
                           {"source", "http"},
                           {"thinking_level", thinking_level},
                           {"group_id", group_id}},
                          provenance);

  bool stream_failed = false;
  std::string stream_error;
  agent::AgentResponse agent_response;
  const bool ws_enabled = websocket_server_ != nullptr && websocket_server_->is_running();
  const auto lane = session_lane(session);
  std::unique_lock<std::mutex> lane_lock(*lane, std::defer_lock);
  if (!lane_lock.try_lock()) {
    if (ws_enabled) {
      (void)websocket_server_->publish_session_event(session,
                                                     {{"event", "assistant.queued"},
                                                      {"channel", "webhook"}});
    }
    lane_lock.lock();
  }
  agent::AgentOptions run_options;
  run_options.model_override = model;
  const std::string explicit_temperature = common::trim(find_json_numeric_field(request.body, "temperature"));
  if (!explicit_temperature.empty()) {
    try {
      run_options.temperature_override = std::stod(explicit_temperature);
    } catch (...) {
      return make_json_response(400, R"({"error":"invalid_temperature"})");
    }
  } else if (auto derived_temperature =
                 thinking_level_temperature(thinking_level, config_.default_temperature);
             derived_temperature.has_value()) {
    run_options.temperature_override = *derived_temperature;
  }
  if (ws_enabled) {
    (void)websocket_server_->publish_session_event(session,
                                                   {{"event", "assistant.start"},
                                                    {"channel", "webhook"}});
    auto status = agent_->run_stream(
        message,
        {.on_token =
             [&](std::string_view token) {
               (void)websocket_server_->publish_session_event(
                   session, {{"event", "assistant.token"}, {"text", std::string(token)}});
             },
         .on_done = [&](const agent::AgentResponse &response) { agent_response = response; },
         .on_error =
             [&](const std::string &error) {
               stream_failed = true;
               stream_error = error;
             }},
        run_options);
    if (!status.ok()) {
      stream_failed = true;
      stream_error = status.error();
    }
    if (stream_failed) {
      observability::record_error("gateway.webhook", stream_error);
      (void)websocket_server_->publish_session_event(session,
                                                     {{"event", "assistant.error"},
                                                      {"error", stream_error}});
      append_transcript_entry(session_store_.get(), session, sessions::TranscriptRole::System,
                              "agent.run failed: " + stream_error, model,
                              {{"channel", "webhook"},
                               {"source", "http"},
                               {"event", "assistant.error"},
                               {"thinking_level", thinking_level},
                               {"group_id", group_id}});
      return make_json_response(500, std::string("{\"error\":") + json_string(stream_error) + "}");
    }
    (void)websocket_server_->publish_session_event(
        session,
        {{"event", "assistant.done"},
         {"duration_ms", std::to_string(agent_response.duration.count())},
         {"tool_calls", std::to_string(agent_response.tool_results.size())}});
  } else {
    auto response = agent_->run(message, run_options);
    if (!response.ok()) {
      observability::record_error("gateway.webhook", response.error());
      append_transcript_entry(session_store_.get(), session, sessions::TranscriptRole::System,
                              "agent.run failed: " + response.error(), model,
                              {{"channel", "webhook"},
                               {"source", "http"},
                               {"event", "assistant.error"},
                               {"thinking_level", thinking_level},
                               {"group_id", group_id}});
      return make_json_response(500, std::string("{\"error\":") + json_string(response.error()) + "}");
    }
    agent_response = response.value();
  }
  append_transcript_entry(
      session_store_.get(), session, sessions::TranscriptRole::Assistant, agent_response.content,
      model,
      {{"channel", "webhook"},
       {"source", "http"},
       {"duration_ms", std::to_string(agent_response.duration.count())},
       {"tool_calls", std::to_string(agent_response.tool_results.size())},
       {"thinking_level", thinking_level},
       {"group_id", group_id}});
  upsert_session_state(session_store_.get(), session, model, thinking_level, "webhook", group_id);

  observability::record_channel_message("webhook", "outbound");
  std::ostringstream body;
  body << "{";
  body << "\"response\":" << json_string(agent_response.content) << ",";
  body << "\"session_id\":" << json_string(session) << ",";
  body << "\"model\":" << json_string(model) << ",";
  body << "\"thinking_level\":" << json_string(thinking_level) << ",";
  if (!group_id.empty()) {
    body << "\"group_id\":" << json_string(group_id) << ",";
  }
  body << "\"duration_ms\":" << agent_response.duration.count() << ",";
  body << "\"tool_calls\":" << agent_response.tool_results.size();
  body << "}";
  return make_json_response(200, body.str());
}

HttpResponse GatewayServer::handle_whatsapp_verify(const HttpRequest &request) const {
  if (!config_.channels.whatsapp.has_value()) {
    return make_json_response(404, R"({"error":"whatsapp_not_configured"})");
  }
  const auto token_it = request.query.find("hub.verify_token");
  const auto challenge_it = request.query.find("hub.challenge");
  if (token_it == request.query.end() || challenge_it == request.query.end()) {
    return make_json_response(400, R"({"error":"missing_query"})");
  }
  if (token_it->second != config_.channels.whatsapp->verify_token) {
    return make_json_response(403, R"({"error":"invalid_verify_token"})");
  }
  return make_text_response(200, challenge_it->second);
}

HttpResponse GatewayServer::handle_whatsapp_message(const HttpRequest &request) {
  if (!agent_) {
    return make_json_response(500, R"({"error":"agent_unavailable"})");
  }
  const std::string text = find_json_string_field(request.body, "message");
  if (!text.empty()) {
    observability::record_channel_message("whatsapp", "inbound");
    (void)agent_->run(text);
    observability::record_channel_message("whatsapp", "outbound");
  }
  return make_json_response(200, R"({"status":"ok"})");
}

void GatewayServer::accept_loop() {
#ifndef _WIN32
  while (running_) {
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    int client = accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr), &len);
    if (client < 0) {
      if (!running_) {
        break;
      }
      continue;
    }
    handle_client(client);
    close(client);
  }
#endif
}

void GatewayServer::handle_client(int client_fd) {
#ifndef _WIN32
  std::string raw;
  raw.reserve(4096);
  std::array<char, 4096> buf{};

  std::size_t content_length = 0;
  bool header_parsed = false;
  while (raw.size() < (kMaxBodySize + 8192)) {
    const ssize_t n = recv(client_fd, buf.data(), buf.size(), 0);
    if (n <= 0) {
      break;
    }
    raw.append(buf.data(), static_cast<std::size_t>(n));

    if (!header_parsed) {
      const auto header_end = raw.find("\r\n\r\n");
      if (header_end != std::string::npos) {
        header_parsed = true;
        auto parsed = parse_http_request(raw.substr(0, header_end + 4));
        if (parsed.ok()) {
          const std::string cl = header_lookup(parsed.value(), "content-length");
          if (!cl.empty()) {
            try {
              content_length = static_cast<std::size_t>(std::stoull(cl));
            } catch (...) {
              content_length = 0;
            }
          }
        }
        if (content_length > kMaxBodySize) {
          const auto text = render_http_response(
              make_json_response(413, R"({"error":"request_too_large"})"));
          send(client_fd, text.data(), text.size(), 0);
          return;
        }
      }
    }

    if (header_parsed) {
      const auto header_end = raw.find("\r\n\r\n");
      if (header_end != std::string::npos &&
          raw.size() >= header_end + 4 + content_length) {
        break;
      }
    }
  }

  auto parsed = parse_http_request(raw);
  HttpResponse response;
  if (!parsed.ok()) {
    response = make_json_response(400, R"({"error":"invalid_request"})");
  } else {
    response = dispatch_for_test(parsed.value());
  }
  const std::string text = render_http_response(response);
  send(client_fd, text.data(), text.size(), 0);
#endif
}

} // namespace ghostclaw::gateway
