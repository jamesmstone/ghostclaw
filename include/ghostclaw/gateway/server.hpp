#pragma once

#include "ghostclaw/agent/engine.hpp"
#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/gateway/protocol.hpp"
#include "ghostclaw/gateway/websocket.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/sessions/send_policy.hpp"
#include "ghostclaw/sessions/store.hpp"
#include "ghostclaw/security/pairing.hpp"
#include "ghostclaw/tunnel/tunnel.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ghostclaw::gateway {

struct GatewayOptions {
  std::string host = "127.0.0.1";
  std::uint16_t port = 8080;
  bool verbose = false;
};

struct HttpRequest {
  std::string method;
  std::string path;
  std::string raw_path;
  std::unordered_map<std::string, std::string> headers;
  std::unordered_map<std::string, std::string> query;
  std::string body;
};

struct HttpResponse {
  int status = 200;
  std::string content_type = "application/json";
  std::string body;
  std::unordered_map<std::string, std::string> headers;
};

class GatewayServer {
public:
  GatewayServer(const config::Config &config, std::shared_ptr<agent::AgentEngine> agent,
                memory::IMemory *memory = nullptr);
  ~GatewayServer();

  [[nodiscard]] common::Status start(const GatewayOptions &options);
  void stop();

  [[nodiscard]] std::uint16_t port() const;
  [[nodiscard]] std::string pairing_code() const;
  [[nodiscard]] bool is_running() const;
  [[nodiscard]] std::optional<std::string> public_url() const;
  [[nodiscard]] std::uint16_t websocket_port() const;

  [[nodiscard]] HttpResponse dispatch_for_test(const HttpRequest &request);

private:
  [[nodiscard]] common::Status validate_bind_address(const std::string &host) const;
  [[nodiscard]] HttpResponse handle_health(const HttpRequest &request) const;
  [[nodiscard]] HttpResponse handle_pair(const HttpRequest &request);
  [[nodiscard]] HttpResponse handle_webhook(const HttpRequest &request);
  [[nodiscard]] HttpResponse handle_whatsapp_verify(const HttpRequest &request) const;
  [[nodiscard]] HttpResponse handle_whatsapp_message(const HttpRequest &request);

  [[nodiscard]] bool validate_bearer(const std::string &authorization) const;
  [[nodiscard]] std::shared_ptr<std::mutex> session_lane(const std::string &session_id);

  void accept_loop();
  void handle_client(int client_fd);

  const config::Config &config_;
  std::shared_ptr<agent::AgentEngine> agent_;
  memory::IMemory *memory_;
  std::unique_ptr<security::PairingState> pairing_state_;
  std::string pairing_code_;
  std::unique_ptr<tunnel::ITunnel> tunnel_;
  std::string tunnel_public_url_;
  std::unique_ptr<WebSocketServer> websocket_server_;
  std::uint16_t websocket_port_ = 0;
  std::unique_ptr<sessions::SessionStore> session_store_;
  std::unique_ptr<sessions::SessionSendPolicy> send_policy_;

  std::atomic<bool> running_{false};
  int listen_fd_ = -1;
  std::thread accept_thread_;
  std::uint16_t bound_port_ = 0;

  std::mutex session_lanes_mutex_;
  std::unordered_map<std::string, std::weak_ptr<std::mutex>> session_lanes_;
};

} // namespace ghostclaw::gateway
