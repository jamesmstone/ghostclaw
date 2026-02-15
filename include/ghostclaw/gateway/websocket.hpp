#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/gateway/protocol.hpp"

#include <openssl/ssl.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ghostclaw::gateway {

struct WebSocketOptions {
  std::string host = "127.0.0.1";
  std::uint16_t port = 0;
  std::size_t max_clients = 256;
  bool tls_enabled = false;
  std::string tls_cert_file;
  std::string tls_key_file;
  bool require_authorization = false;
  std::function<bool(const std::string &)> authorize;
  std::function<common::Result<RpcMap>(const WsClientMessage &,
                                       const std::function<void(const RpcMap &)> &)> rpc_handler;
};

struct WebSocketStats {
  std::size_t connected_clients = 0;
  std::size_t total_subscriptions = 0;
};

class WebSocketServer {
public:
  WebSocketServer();
  ~WebSocketServer();

  [[nodiscard]] common::Status start(const WebSocketOptions &options);
  void stop();

  [[nodiscard]] bool is_running() const;
  [[nodiscard]] std::uint16_t port() const;
  [[nodiscard]] WebSocketStats stats() const;

  [[nodiscard]] std::size_t publish_session_event(const std::string &session,
                                                  const RpcMap &payload);

private:
  struct ClientState {
    int fd = -1;
    SSL *ssl = nullptr;
    std::unordered_set<std::string> sessions;
    std::mutex write_mutex;
  };

  [[nodiscard]] common::Status validate_bind_address(const std::string &host) const;
  void accept_loop();
  void client_loop(std::shared_ptr<ClientState> client);
  [[nodiscard]] bool perform_handshake(const std::shared_ptr<ClientState> &client) const;
  void remove_client(int fd);

  [[nodiscard]] bool send_text_frame(const std::shared_ptr<ClientState> &client,
                                     const std::string &payload) const;
  [[nodiscard]] bool send_server_message(const std::shared_ptr<ClientState> &client,
                                         const WsServerMessage &message) const;
  void handle_client_message(const std::shared_ptr<ClientState> &client,
                             const WsClientMessage &message);

  WebSocketOptions options_;
  std::atomic<bool> running_{false};
  int listen_fd_ = -1;
  std::thread accept_thread_;
  std::uint16_t bound_port_ = 0;
  SSL_CTX *tls_ctx_ = nullptr;

  mutable std::mutex clients_mutex_;
  std::unordered_map<int, std::shared_ptr<ClientState>> clients_;
};

} // namespace ghostclaw::gateway
