#pragma once

#include "ghostclaw/agent/engine.hpp"
#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/sessions/store.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace ghostclaw::gateway {

using RpcMap = std::unordered_map<std::string, std::string>;

struct RpcRequest {
  std::string id;
  std::string method;
  RpcMap params;
};

struct RpcResponse {
  std::string id;
  RpcMap result;
  std::optional<std::string> error;

  [[nodiscard]] std::string to_json() const;
};

struct WsClientMessage {
  std::string id;
  std::string type;
  std::string method;
  std::string session;
  RpcMap payload;
};

struct WsServerMessage {
  std::string type;
  std::string id;
  std::string session;
  RpcMap payload;
  std::optional<std::string> error;

  [[nodiscard]] std::string to_json() const;
};

[[nodiscard]] common::Result<WsClientMessage> parse_ws_client_message(const std::string &json);

class RpcHandler {
public:
  RpcHandler(std::shared_ptr<agent::AgentEngine> agent, memory::IMemory *memory,
             sessions::SessionStore *session_store, const config::Config &config);

  [[nodiscard]] RpcResponse handle(const RpcRequest &request);

private:
  [[nodiscard]] RpcResponse handle_agent_run(const RpcRequest &request);
  [[nodiscard]] RpcResponse handle_config_get(const RpcRequest &request) const;
  [[nodiscard]] RpcResponse handle_session_list(const RpcRequest &request) const;
  [[nodiscard]] RpcResponse handle_session_history(const RpcRequest &request) const;
  [[nodiscard]] RpcResponse handle_session_override_set(const RpcRequest &request);
  [[nodiscard]] RpcResponse handle_session_override_get(const RpcRequest &request) const;
  [[nodiscard]] RpcResponse handle_session_group_list(const RpcRequest &request) const;
  [[nodiscard]] RpcResponse handle_health(const RpcRequest &request) const;

  std::shared_ptr<agent::AgentEngine> agent_;
  memory::IMemory *memory_;
  sessions::SessionStore *session_store_;
  const config::Config &config_;
};

} // namespace ghostclaw::gateway
