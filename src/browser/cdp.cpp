#include "ghostclaw/browser/cdp.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"
#include "ghostclaw/providers/traits.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <vector>

#include <openssl/evp.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ghostclaw::browser {

namespace {

struct ParsedWsUrl {
  std::string host;
  std::uint16_t port = 0;
  std::string path;
};

std::string trim_copy(const std::string &value) { return common::trim(value); }

bool is_digit_text(const std::string &value) {
  if (value.empty()) {
    return false;
  }
  std::size_t start = (value.front() == '-' || value.front() == '+') ? 1 : 0;
  if (start >= value.size()) {
    return false;
  }
  bool saw_digit = false;
  bool saw_dot = false;
  for (std::size_t i = start; i < value.size(); ++i) {
    const char ch = value[i];
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      saw_digit = true;
      continue;
    }
    if (ch == '.' && !saw_dot) {
      saw_dot = true;
      continue;
    }
    return false;
  }
  return saw_digit;
}

bool looks_like_json_literal(const std::string &value) {
  const std::string trimmed = trim_copy(value);
  if (trimmed.empty()) {
    return false;
  }
  if (trimmed == "true" || trimmed == "false" || trimmed == "null") {
    return true;
  }
  if (trimmed.starts_with('{') || trimmed.starts_with('[')) {
    return true;
  }
  return is_digit_text(trimmed);
}

std::string encode_json_value(const std::string &value) {
  if (looks_like_json_literal(value)) {
    return trim_copy(value);
  }
  return "\"" + common::json_escape(value) + "\"";
}

std::string encode_json_object(const JsonMap &map) {
  std::ostringstream out;
  out << "{";
  bool first = true;
  for (const auto &[key, value] : map) {
    if (!first) {
      out << ",";
    }
    first = false;
    out << "\"" << common::json_escape(key) << "\":" << encode_json_value(value);
  }
  out << "}";
  return out.str();
}

// JSON parsing delegated to common::json_util
std::string find_json_string_field(const std::string &json, const std::string &field) {
  return common::json_get_string(json, field);
}

std::string find_json_number_field(const std::string &json, const std::string &field) {
  return common::json_get_number(json, field);
}

std::string find_json_object_field(const std::string &json, const std::string &field) {
  return common::json_get_object(json, field);
}

JsonMap parse_flat_json_object(const std::string &json) {
  return common::json_parse_flat(json);
}

common::Result<ParsedWsUrl> parse_ws_url(const std::string &url) {
  const std::string trimmed = trim_copy(url);
  if (!trimmed.starts_with("ws://")) {
    return common::Result<ParsedWsUrl>::failure("only ws:// URLs are supported");
  }
  const std::size_t host_start = 5;
  std::size_t path_start = trimmed.find('/', host_start);
  const std::string host_port =
      (path_start == std::string::npos) ? trimmed.substr(host_start)
                                        : trimmed.substr(host_start, path_start - host_start);
  if (path_start == std::string::npos) {
    path_start = trimmed.size();
  }
  if (host_port.empty()) {
    return common::Result<ParsedWsUrl>::failure("missing websocket host");
  }

  std::string host;
  std::uint16_t port = 80;
  const auto colon = host_port.rfind(':');
  if (colon != std::string::npos) {
    host = host_port.substr(0, colon);
    const std::string port_text = host_port.substr(colon + 1);
    try {
      const auto parsed = std::stoul(port_text);
      if (parsed == 0 || parsed > 65535) {
        return common::Result<ParsedWsUrl>::failure("invalid websocket port");
      }
      port = static_cast<std::uint16_t>(parsed);
    } catch (...) {
      return common::Result<ParsedWsUrl>::failure("invalid websocket port");
    }
  } else {
    host = host_port;
  }
  if (host.empty()) {
    return common::Result<ParsedWsUrl>::failure("missing websocket host");
  }

  ParsedWsUrl parsed;
  parsed.host = host;
  parsed.port = port;
  parsed.path =
      (path_start < trimmed.size()) ? trimmed.substr(path_start) : "/devtools/browser";
  if (parsed.path.empty()) {
    parsed.path = "/devtools/browser";
  }
  return common::Result<ParsedWsUrl>::success(std::move(parsed));
}

#ifndef _WIN32
bool send_all(const int fd, const std::uint8_t *data, const std::size_t size) {
  std::size_t sent = 0;
  while (sent < size) {
    const ssize_t n = send(fd, data + sent, size - sent, 0);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

bool recv_exact(const int fd, std::uint8_t *data, const std::size_t size) {
  std::size_t received = 0;
  while (received < size) {
    const ssize_t n = recv(fd, data + received, size - received, 0);
    if (n <= 0) {
      return false;
    }
    received += static_cast<std::size_t>(n);
  }
  return true;
}

std::string random_websocket_key() {
  std::array<std::uint8_t, 16> bytes{};
  std::random_device rd;
  for (auto &byte : bytes) {
    byte = static_cast<std::uint8_t>(rd() & 0xFF);
  }
  const int out_len = 4 * static_cast<int>((bytes.size() + 2) / 3);
  std::string key(static_cast<std::size_t>(out_len), '\0');
  EVP_EncodeBlock(reinterpret_cast<unsigned char *>(key.data()), bytes.data(),
                  static_cast<int>(bytes.size()));
  return key;
}

class WebSocketTcpTransport final : public ICDPTransport {
public:
  ~WebSocketTcpTransport() override { close(); }

  common::Status connect(const std::string &ws_url) override {
    if (connected_.load()) {
      return common::Status::error("transport already connected");
    }
    auto parsed = parse_ws_url(ws_url);
    if (!parsed.ok()) {
      return common::Status::error(parsed.error());
    }

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      return common::Status::error("failed to create socket");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(parsed.value().port);
    if (inet_pton(AF_INET, parsed.value().host.c_str(), &addr.sin_addr) != 1) {
      hostent *he = gethostbyname(parsed.value().host.c_str());
      if (he == nullptr || he->h_addr_list == nullptr || he->h_addr_list[0] == nullptr) {
        ::close(fd);
        return common::Status::error("failed to resolve websocket host");
      }
      std::memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(addr.sin_addr));
    }

    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
      const std::string message = std::strerror(errno);
      ::close(fd);
      return common::Status::error("websocket connect failed: " + message);
    }

    const std::string ws_key = random_websocket_key();
    std::ostringstream req;
    req << "GET " << parsed.value().path << " HTTP/1.1\r\n";
    req << "Host: " << parsed.value().host << ":" << parsed.value().port << "\r\n";
    req << "Upgrade: websocket\r\n";
    req << "Connection: Upgrade\r\n";
    req << "Sec-WebSocket-Key: " << ws_key << "\r\n";
    req << "Sec-WebSocket-Version: 13\r\n";
    req << "\r\n";
    const std::string handshake = req.str();
    if (!send_all(fd, reinterpret_cast<const std::uint8_t *>(handshake.data()), handshake.size())) {
      ::close(fd);
      return common::Status::error("websocket handshake send failed");
    }

    std::string response;
    response.reserve(1024);
    std::array<char, 1024> buffer{};
    while (response.find("\r\n\r\n") == std::string::npos) {
      const ssize_t n = recv(fd, buffer.data(), buffer.size(), 0);
      if (n <= 0) {
        ::close(fd);
        return common::Status::error("websocket handshake receive failed");
      }
      response.append(buffer.data(), static_cast<std::size_t>(n));
      if (response.size() > 16 * 1024) {
        ::close(fd);
        return common::Status::error("websocket handshake too large");
      }
    }
    if (response.find(" 101 ") == std::string::npos &&
        response.find(" 101\r\n") == std::string::npos) {
      ::close(fd);
      return common::Status::error("websocket handshake rejected");
    }

    {
      std::lock_guard<std::mutex> lock(io_mutex_);
      fd_ = fd;
    }
    connected_.store(true);
    return common::Status::success();
  }

  void close() override {
    int fd = -1;
    {
      std::lock_guard<std::mutex> lock(io_mutex_);
      fd = fd_;
      fd_ = -1;
    }
    connected_.store(false);
    if (fd >= 0) {
      shutdown(fd, SHUT_RDWR);
      ::close(fd);
    }
  }

  bool is_connected() const override { return connected_.load(); }

  common::Status send_text(const std::string &payload) override {
    if (!connected_.load()) {
      return common::Status::error("transport not connected");
    }
    int fd = -1;
    {
      std::lock_guard<std::mutex> lock(io_mutex_);
      fd = fd_;
    }
    if (fd < 0) {
      return common::Status::error("transport socket unavailable");
    }

    std::vector<std::uint8_t> frame;
    frame.reserve(payload.size() + 16);
    frame.push_back(0x81u); // FIN + text

    const std::size_t size = payload.size();
    if (size <= 125u) {
      frame.push_back(static_cast<std::uint8_t>(0x80u | size));
    } else if (size <= 65535u) {
      frame.push_back(0x80u | 126u);
      frame.push_back(static_cast<std::uint8_t>((size >> 8u) & 0xFFu));
      frame.push_back(static_cast<std::uint8_t>(size & 0xFFu));
    } else {
      frame.push_back(0x80u | 127u);
      for (int shift = 56; shift >= 0; shift -= 8) {
        frame.push_back(static_cast<std::uint8_t>((size >> static_cast<std::size_t>(shift)) & 0xFFu));
      }
    }

    std::array<std::uint8_t, 4> mask{};
    std::random_device rd;
    for (auto &byte : mask) {
      byte = static_cast<std::uint8_t>(rd() & 0xFF);
    }
    frame.insert(frame.end(), mask.begin(), mask.end());

    for (std::size_t i = 0; i < payload.size(); ++i) {
      frame.push_back(static_cast<std::uint8_t>(payload[i]) ^ mask[i % mask.size()]);
    }

    if (!send_all(fd, frame.data(), frame.size())) {
      return common::Status::error("websocket send failed");
    }
    return common::Status::success();
  }

  common::Result<std::string> receive_text(std::chrono::milliseconds timeout) override {
    if (!connected_.load()) {
      return common::Result<std::string>::failure("transport not connected");
    }
    int fd = -1;
    {
      std::lock_guard<std::mutex> lock(io_mutex_);
      fd = fd_;
    }
    if (fd < 0) {
      return common::Result<std::string>::failure("transport socket unavailable");
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);
    timeval tv{};
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<decltype(tv.tv_usec)>((timeout.count() % 1000) * 1000);
    const int ready = select(fd + 1, &read_fds, nullptr, nullptr, &tv);
    if (ready < 0) {
      return common::Result<std::string>::failure("websocket select failed");
    }
    if (ready == 0) {
      return common::Result<std::string>::failure("timeout");
    }

    std::array<std::uint8_t, 2> header{};
    if (!recv_exact(fd, header.data(), header.size())) {
      connected_.store(false);
      return common::Result<std::string>::failure("websocket receive failed");
    }

    const std::uint8_t opcode = static_cast<std::uint8_t>(header[0] & 0x0Fu);
    std::uint64_t payload_len = static_cast<std::uint64_t>(header[1] & 0x7Fu);
    const bool masked = (header[1] & 0x80u) != 0;
    if (payload_len == 126u) {
      std::array<std::uint8_t, 2> ext{};
      if (!recv_exact(fd, ext.data(), ext.size())) {
        return common::Result<std::string>::failure("websocket frame header failed");
      }
      payload_len = (static_cast<std::uint64_t>(ext[0]) << 8u) |
                    static_cast<std::uint64_t>(ext[1]);
    } else if (payload_len == 127u) {
      std::array<std::uint8_t, 8> ext{};
      if (!recv_exact(fd, ext.data(), ext.size())) {
        return common::Result<std::string>::failure("websocket frame header failed");
      }
      payload_len = 0;
      for (const auto byte : ext) {
        payload_len = (payload_len << 8u) | static_cast<std::uint64_t>(byte);
      }
    }

    std::array<std::uint8_t, 4> mask{};
    if (masked) {
      if (!recv_exact(fd, mask.data(), mask.size())) {
        return common::Result<std::string>::failure("websocket mask read failed");
      }
    }
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(payload_len));
    if (!payload.empty() && !recv_exact(fd, payload.data(), payload.size())) {
      return common::Result<std::string>::failure("websocket payload read failed");
    }
    if (masked) {
      for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] ^= mask[i % mask.size()];
      }
    }
    if (opcode == 0x8u) {
      connected_.store(false);
      return common::Result<std::string>::failure("websocket closed");
    }
    if (opcode == 0x9u) {
      // ping -> pong
      std::vector<std::uint8_t> pong;
      pong.reserve(payload.size() + 8);
      pong.push_back(0x8Au);
      if (payload.size() <= 125u) {
        pong.push_back(static_cast<std::uint8_t>(payload.size()));
      } else {
        pong.push_back(0);
      }
      pong.insert(pong.end(), payload.begin(), payload.end());
      (void)send_all(fd, pong.data(), pong.size());
      return common::Result<std::string>::failure("ping");
    }
    if (opcode != 0x1u) {
      return common::Result<std::string>::failure("unsupported frame opcode");
    }
    return common::Result<std::string>::success(
        std::string(reinterpret_cast<const char *>(payload.data()), payload.size()));
  }

private:
  mutable std::mutex io_mutex_;
  int fd_ = -1;
  std::atomic<bool> connected_{false};
};
#endif

} // namespace

CDPClient::CDPClient()
#ifdef _WIN32
    : transport_(nullptr) {}
#else
    : transport_(std::make_unique<WebSocketTcpTransport>()) {}
#endif

CDPClient::CDPClient(std::unique_ptr<ICDPTransport> transport)
    : transport_(std::move(transport)) {}

CDPClient::~CDPClient() { disconnect(); }

common::Status CDPClient::connect(const std::string &ws_url) {
#ifdef _WIN32
  (void)ws_url;
  return common::Status::error("CDP client is not implemented on Windows");
#else
  if (transport_ == nullptr) {
    return common::Status::error("CDP transport unavailable");
  }
  if (running_.load()) {
    return common::Status::success();
  }
  const auto status = transport_->connect(ws_url);
  if (!status.ok()) {
    return status;
  }
  running_.store(true);
  reader_thread_ = std::thread([this]() { reader_loop(); });
  return common::Status::success();
#endif
}

void CDPClient::disconnect() {
  running_.store(false);
  if (transport_ != nullptr) {
    transport_->close();
  }
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }

  std::vector<std::shared_ptr<PendingRequest>> pending;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (auto &[id, request] : pending_requests_) {
      (void)id;
      pending.push_back(request);
    }
    pending_requests_.clear();
  }
  for (const auto &request : pending) {
    std::lock_guard<std::mutex> request_lock(request->mutex);
    request->complete = true;
    request->error = "CDP client disconnected";
    request->cv.notify_all();
  }
}

bool CDPClient::is_connected() const {
  return transport_ != nullptr && transport_->is_connected();
}

common::Result<JsonMap> CDPClient::send_command(const std::string &method, const JsonMap &params,
                                                const std::chrono::milliseconds timeout) {
  if (common::trim(method).empty()) {
    return common::Result<JsonMap>::failure("method is required");
  }
  if (transport_ == nullptr || !transport_->is_connected()) {
    return common::Result<JsonMap>::failure("CDP client is not connected");
  }

  int id = 0;
  auto pending = std::make_shared<PendingRequest>();
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    id = next_id_++;
    pending_requests_[id] = pending;
  }

  std::ostringstream payload;
  payload << "{";
  payload << "\"id\":" << id << ",";
  payload << "\"method\":\"" << common::json_escape(method) << "\",";
  payload << "\"params\":" << encode_json_object(params);
  payload << "}";

  const auto send_status = transport_->send_text(payload.str());
  if (!send_status.ok()) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    pending_requests_.erase(id);
    return common::Result<JsonMap>::failure(send_status.error());
  }

  std::unique_lock<std::mutex> lock(pending->mutex);
  const bool done = pending->cv.wait_for(lock, timeout, [&]() { return pending->complete; });
  if (!done) {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    pending_requests_.erase(id);
    return common::Result<JsonMap>::failure("CDP command timeout");
  }
  if (pending->error.has_value()) {
    return common::Result<JsonMap>::failure(*pending->error);
  }
  if (!pending->result.has_value()) {
    return common::Result<JsonMap>::failure("CDP command returned no result");
  }
  return common::Result<JsonMap>::success(*pending->result);
}

void CDPClient::on_event(const std::string &method, EventCallback callback) {
  if (common::trim(method).empty() || !callback) {
    return;
  }
  std::lock_guard<std::mutex> lock(state_mutex_);
  event_handlers_[method].push_back(std::move(callback));
}

common::Result<std::string> CDPClient::capture_screenshot() {
  auto result = send_command("Page.captureScreenshot", {{"format", "png"}});
  if (!result.ok()) {
    return common::Result<std::string>::failure(result.error());
  }
  const auto it = result.value().find("data");
  if (it == result.value().end()) {
    return common::Result<std::string>::failure("screenshot data missing");
  }
  return common::Result<std::string>::success(it->second);
}

common::Result<JsonMap> CDPClient::get_accessibility_tree() {
  return send_command("Accessibility.getFullAXTree");
}

common::Result<JsonMap> CDPClient::evaluate_js(const std::string &expression) {
  if (common::trim(expression).empty()) {
    return common::Result<JsonMap>::failure("expression is required");
  }
  return send_command("Runtime.evaluate",
                      {{"expression", expression}, {"returnByValue", "true"}});
}

void CDPClient::reader_loop() {
  while (running_.load()) {
    if (transport_ == nullptr || !transport_->is_connected()) {
      break;
    }
    auto incoming = transport_->receive_text(std::chrono::milliseconds(200));
    if (!incoming.ok()) {
      const std::string error = incoming.error();
      if (error == "timeout" || error == "ping") {
        continue;
      }
      if (!running_.load()) {
        break;
      }

      std::vector<std::shared_ptr<PendingRequest>> pending;
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (auto &[id, request] : pending_requests_) {
          (void)id;
          pending.push_back(request);
        }
        pending_requests_.clear();
      }
      for (const auto &request : pending) {
        std::lock_guard<std::mutex> request_lock(request->mutex);
        request->complete = true;
        request->error = "CDP receive failed: " + error;
        request->cv.notify_all();
      }
      break;
    }
    handle_incoming_message(incoming.value());
  }
}

void CDPClient::handle_incoming_message(const std::string &json) {
  const std::string id_text = find_json_number_field(json, "id");
  if (!id_text.empty()) {
    int id = 0;
    try {
      id = std::stoi(id_text);
    } catch (...) {
      return;
    }

    std::shared_ptr<PendingRequest> pending;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      const auto it = pending_requests_.find(id);
      if (it == pending_requests_.end()) {
        return;
      }
      pending = it->second;
      pending_requests_.erase(it);
    }

    JsonMap result = parse_flat_json_object(find_json_object_field(json, "result"));
    const std::string error_message = find_json_string_field(json, "message");
    {
      std::lock_guard<std::mutex> lock(pending->mutex);
      pending->complete = true;
      if (!error_message.empty() && json.find("\"error\"") != std::string::npos) {
        pending->error = error_message;
      } else {
        pending->result = std::move(result);
      }
    }
    pending->cv.notify_all();
    return;
  }

  const std::string method = find_json_string_field(json, "method");
  if (method.empty()) {
    return;
  }
  const JsonMap params = parse_flat_json_object(find_json_object_field(json, "params"));

  std::vector<EventCallback> callbacks;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    const auto it = event_handlers_.find(method);
    if (it != event_handlers_.end()) {
      callbacks = it->second;
    }
  }
  for (const auto &callback : callbacks) {
    if (callback) {
      callback(method, params);
    }
  }
}

} // namespace ghostclaw::browser
