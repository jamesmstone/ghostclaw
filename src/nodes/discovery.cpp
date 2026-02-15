#include "ghostclaw/nodes/discovery.hpp"

#include "ghostclaw/common/fs.hpp"

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <sstream>

namespace ghostclaw::nodes {

namespace {

constexpr std::string_view kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::vector<std::string> split(const std::string &value, const char delimiter) {
  std::vector<std::string> out;
  std::stringstream stream(value);
  std::string part;
  while (std::getline(stream, part, delimiter)) {
    const std::string trimmed = common::trim(part);
    if (!trimmed.empty()) {
      out.push_back(trimmed);
    }
  }
  return out;
}

void dedupe_and_sort(std::vector<std::string> *values) {
  if (values == nullptr) {
    return;
  }
  std::sort(values->begin(), values->end());
  values->erase(std::unique(values->begin(), values->end()), values->end());
}

std::string json_escape(const std::string &value) {
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

std::string extract_json_string_field(const std::string &json, const std::string &field) {
  const std::string key = "\"" + field + "\"";
  const auto key_pos = json.find(key);
  if (key_pos == std::string::npos) {
    return "";
  }
  const auto colon = json.find(':', key_pos + key.size());
  if (colon == std::string::npos) {
    return "";
  }
  const auto quote = json.find('"', colon + 1);
  if (quote == std::string::npos) {
    return "";
  }

  std::string out;
  bool escaped = false;
  for (std::size_t i = quote + 1; i < json.size(); ++i) {
    const char ch = json[i];
    if (escaped) {
      switch (ch) {
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      default:
        out.push_back(ch);
        break;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      return out;
    }
    out.push_back(ch);
  }
  return "";
}

} // namespace

std::vector<DiscoveredNode> NodeDiscovery::discover_bonjour(const std::string_view service_name) {
  (void)service_name;

  std::vector<DiscoveredNode> out;
  const char *raw = std::getenv("GHOSTCLAW_MDNS_NODES");
  if (raw == nullptr || *raw == '\0') {
    return out;
  }

  for (const auto &entry : split(raw, ',')) {
    // Expected entry shape: node-id@host:port#cap1;cap2
    std::string node_id;
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
    std::vector<std::string> capabilities;

    const auto at = entry.find('@');
    const auto hash = entry.find('#');
    const auto endpoint_part = (at == std::string::npos)
                                   ? std::string()
                                   : entry.substr(at + 1, hash == std::string::npos
                                                            ? std::string::npos
                                                            : (hash - at - 1));
    node_id = common::trim(at == std::string::npos ? entry.substr(0, hash) : entry.substr(0, at));

    if (hash != std::string::npos) {
      capabilities = split(entry.substr(hash + 1), ';');
    }

    if (!endpoint_part.empty()) {
      const auto colon = endpoint_part.rfind(':');
      if (colon != std::string::npos) {
        host = common::trim(endpoint_part.substr(0, colon));
        try {
          port = static_cast<std::uint16_t>(std::stoul(common::trim(endpoint_part.substr(colon + 1))));
        } catch (...) {
          port = 0;
        }
      } else {
        host = endpoint_part;
      }
    }

    if (node_id.empty()) {
      continue;
    }

    dedupe_and_sort(&capabilities);
    DiscoveredNode node;
    node.node_id = node_id;
    node.host = host;
    node.port = port;
    node.endpoint = port == 0 ? ("ws://" + host) : ("ws://" + host + ":" + std::to_string(port));
    node.capabilities = std::move(capabilities);
    node.source = "bonjour-env";
    out.push_back(std::move(node));
  }

  return out;
}

std::string
NodeDiscovery::encode_capability_advertisement(const CapabilityAdvertisement &advertisement) {
  std::ostringstream out;
  out << "id=" << common::trim(advertisement.node_id);
  out << ";name=" << common::trim(advertisement.display_name);
  out << ";ws=" << common::trim(advertisement.websocket_url);
  out << ";caps=";
  for (std::size_t i = 0; i < advertisement.capabilities.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << common::trim(advertisement.capabilities[i]);
  }
  return out.str();
}

common::Result<CapabilityAdvertisement>
NodeDiscovery::decode_capability_advertisement(const std::string_view record) {
  CapabilityAdvertisement out;
  for (const auto &part : split(std::string(record), ';')) {
    const auto equal = part.find('=');
    if (equal == std::string::npos) {
      continue;
    }
    const std::string key = common::to_lower(common::trim(part.substr(0, equal)));
    const std::string value = common::trim(part.substr(equal + 1));
    if (key == "id") {
      out.node_id = value;
    } else if (key == "name") {
      out.display_name = value;
    } else if (key == "ws") {
      out.websocket_url = value;
    } else if (key == "caps") {
      out.capabilities = split(value, ',');
    }
  }

  if (out.node_id.empty()) {
    return common::Result<CapabilityAdvertisement>::failure("capability advertisement missing id");
  }
  dedupe_and_sort(&out.capabilities);
  return common::Result<CapabilityAdvertisement>::success(std::move(out));
}

std::string WebSocketPairingProtocol::build_pairing_hello(
    const std::string &node_id, const std::string &nonce,
    const std::vector<std::string> &capabilities) {
  std::ostringstream out;
  out << "{";
  out << "\"type\":\"pairing.hello\",";
  out << "\"node_id\":\"" << json_escape(common::trim(node_id)) << "\",";
  out << "\"nonce\":\"" << json_escape(common::trim(nonce)) << "\",";
  out << "\"capabilities\":\"";
  for (std::size_t i = 0; i < capabilities.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << json_escape(common::trim(capabilities[i]));
  }
  out << "\"";
  out << "}";
  return out.str();
}

common::Result<std::unordered_map<std::string, std::string>>
WebSocketPairingProtocol::parse_pairing_hello(const std::string_view payload) {
  const std::string text(payload);
  std::unordered_map<std::string, std::string> out;
  out["type"] = extract_json_string_field(text, "type");
  out["node_id"] = extract_json_string_field(text, "node_id");
  out["nonce"] = extract_json_string_field(text, "nonce");
  out["capabilities"] = extract_json_string_field(text, "capabilities");

  if (out["type"] != "pairing.hello") {
    return common::Result<std::unordered_map<std::string, std::string>>::failure(
        "invalid pairing hello type");
  }
  if (out["node_id"].empty()) {
    return common::Result<std::unordered_map<std::string, std::string>>::failure(
        "pairing hello missing node_id");
  }
  if (out["nonce"].empty()) {
    return common::Result<std::unordered_map<std::string, std::string>>::failure(
        "pairing hello missing nonce");
  }
  return common::Result<std::unordered_map<std::string, std::string>>::success(std::move(out));
}

common::Result<std::string>
WebSocketPairingProtocol::websocket_accept_key(const std::string_view sec_websocket_key) {
  const std::string key = common::trim(std::string(sec_websocket_key));
  if (key.empty()) {
    return common::Result<std::string>::failure("sec_websocket_key is required");
  }

  const std::string source = key + std::string(kWebSocketGuid);
  std::array<unsigned char, SHA_DIGEST_LENGTH> digest{};
  SHA1(reinterpret_cast<const unsigned char *>(source.data()), source.size(), digest.data());

  const int output_len = 4 * static_cast<int>((digest.size() + 2) / 3);
  std::string output(static_cast<std::size_t>(output_len), '\0');
  EVP_EncodeBlock(reinterpret_cast<unsigned char *>(output.data()), digest.data(),
                  static_cast<int>(digest.size()));
  return common::Result<std::string>::success(std::move(output));
}

} // namespace ghostclaw::nodes
