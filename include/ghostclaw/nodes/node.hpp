#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/security/policy.hpp"
#include "ghostclaw/tools/tool.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ghostclaw::nodes {

struct NodeDescriptor {
  std::string node_id;
  std::string display_name;
  std::string endpoint;
  std::string transport = "ws";
  std::vector<std::string> capabilities;
  bool paired = false;
  bool connected = false;
  std::string pair_token;
  std::string updated_at;
};

struct PairingRequest {
  std::string request_id;
  std::string node_id;
  std::vector<std::string> requested_capabilities;
  std::string created_at;
};

struct NodeActionResult {
  bool success = true;
  bool truncated = false;
  std::string output;
  std::unordered_map<std::string, std::string> metadata;
};

class NodeRegistry {
public:
  NodeRegistry() = default;

  [[nodiscard]] common::Status advertise(NodeDescriptor descriptor);
  [[nodiscard]] common::Result<NodeDescriptor> get(std::string_view node_id) const;
  [[nodiscard]] std::vector<NodeDescriptor> list() const;

  [[nodiscard]] common::Result<PairingRequest>
  create_pairing_request(const std::string &node_id,
                         std::vector<std::string> requested_capabilities);
  [[nodiscard]] std::vector<PairingRequest> pending_pairings() const;
  [[nodiscard]] common::Result<NodeDescriptor> approve_pairing(std::string_view request_id,
                                                               const std::string &token);
  [[nodiscard]] common::Status reject_pairing(std::string_view request_id);

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, NodeDescriptor> nodes_;
  std::unordered_map<std::string, PairingRequest> pending_;
};

class NodeActionExecutor {
public:
  explicit NodeActionExecutor(std::shared_ptr<security::SecurityPolicy> policy = nullptr);

  [[nodiscard]] common::Result<NodeActionResult> invoke(std::string_view action,
                                                        const tools::ToolArgs &args,
                                                        const tools::ToolContext &ctx) const;

private:
  std::shared_ptr<security::SecurityPolicy> policy_;
};

[[nodiscard]] std::vector<std::string> default_node_capabilities();
[[nodiscard]] std::vector<std::string> default_node_commands();

} // namespace ghostclaw::nodes
