#pragma once

#include "ghostclaw/common/result.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ghostclaw::browser {

using JsonMap = std::unordered_map<std::string, std::string>;
using EventCallback = std::function<void(const std::string &, const JsonMap &)>;

class ICDPTransport {
public:
  virtual ~ICDPTransport() = default;
  [[nodiscard]] virtual common::Status connect(const std::string &ws_url) = 0;
  virtual void close() = 0;
  [[nodiscard]] virtual bool is_connected() const = 0;
  [[nodiscard]] virtual common::Status send_text(const std::string &payload) = 0;
  [[nodiscard]] virtual common::Result<std::string>
  receive_text(std::chrono::milliseconds timeout) = 0;
};

class CDPClient {
public:
  CDPClient();
  explicit CDPClient(std::unique_ptr<ICDPTransport> transport);
  ~CDPClient();

  [[nodiscard]] common::Status connect(const std::string &ws_url);
  void disconnect();
  [[nodiscard]] bool is_connected() const;

  [[nodiscard]] common::Result<JsonMap>
  send_command(const std::string &method, const JsonMap &params = {},
               std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
  void on_event(const std::string &method, EventCallback callback);

  [[nodiscard]] common::Result<std::string> capture_screenshot();
  [[nodiscard]] common::Result<JsonMap> get_accessibility_tree();
  [[nodiscard]] common::Result<JsonMap> evaluate_js(const std::string &expression);

private:
  struct PendingRequest {
    std::mutex mutex;
    std::condition_variable cv;
    bool complete = false;
    std::optional<JsonMap> result;
    std::optional<std::string> error;
  };

  void reader_loop();
  void handle_incoming_message(const std::string &json);

  std::unique_ptr<ICDPTransport> transport_;
  std::atomic<bool> running_{false};
  std::thread reader_thread_;

  mutable std::mutex state_mutex_;
  int next_id_ = 1;
  std::unordered_map<int, std::shared_ptr<PendingRequest>> pending_requests_;
  std::unordered_map<std::string, std::vector<EventCallback>> event_handlers_;
};

} // namespace ghostclaw::browser
