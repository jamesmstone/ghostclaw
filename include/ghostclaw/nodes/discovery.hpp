#pragma once

#include "ghostclaw/common/result.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ghostclaw::nodes {

struct DiscoveredNode {
  std::string node_id;
  std::string host;
  std::uint16_t port = 0;
  std::string endpoint;
  std::vector<std::string> capabilities;
  std::string source;
};

struct CapabilityAdvertisement {
  std::string node_id;
  std::string display_name;
  std::string websocket_url;
  std::vector<std::string> capabilities;
};

class NodeDiscovery {
public:
  [[nodiscard]] static std::vector<DiscoveredNode>
  discover_bonjour(std::string_view service_name = "_ghostclaw-node._tcp");
  [[nodiscard]] static std::string
  encode_capability_advertisement(const CapabilityAdvertisement &advertisement);
  [[nodiscard]] static common::Result<CapabilityAdvertisement>
  decode_capability_advertisement(std::string_view record);
};

class WebSocketPairingProtocol {
public:
  [[nodiscard]] static std::string
  build_pairing_hello(const std::string &node_id, const std::string &nonce,
                      const std::vector<std::string> &capabilities);
  [[nodiscard]] static common::Result<std::unordered_map<std::string, std::string>>
  parse_pairing_hello(std::string_view payload);
  [[nodiscard]] static common::Result<std::string>
  websocket_accept_key(std::string_view sec_websocket_key);
};

} // namespace ghostclaw::nodes
