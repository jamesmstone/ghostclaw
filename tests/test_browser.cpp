#include "test_framework.hpp"

#include "ghostclaw/browser/actions.hpp"
#include "ghostclaw/browser/cdp.hpp"
#include "ghostclaw/browser/chrome.hpp"
#include "ghostclaw/browser/profiles.hpp"
#include "ghostclaw/browser/server.hpp"

#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

std::string find_json_string_field(const std::string &json, const std::string &field) {
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
  const auto end = json.find('"', quote + 1);
  if (end == std::string::npos || end <= quote) {
    return "";
  }
  return json.substr(quote + 1, end - quote - 1);
}

int find_json_int_field(const std::string &json, const std::string &field) {
  const std::string key = "\"" + field + "\"";
  const auto key_pos = json.find(key);
  if (key_pos == std::string::npos) {
    return 0;
  }
  const auto colon = json.find(':', key_pos + key.size());
  if (colon == std::string::npos) {
    return 0;
  }
  std::size_t pos = colon + 1;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
    ++pos;
  }
  std::size_t end = pos;
  while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end])) != 0) {
    ++end;
  }
  if (end <= pos) {
    return 0;
  }
  return std::stoi(json.substr(pos, end - pos));
}

class FakeCDPTransport final : public ghostclaw::browser::ICDPTransport {
public:
  [[nodiscard]] ghostclaw::common::Status connect(const std::string &) override {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = true;
    return ghostclaw::common::Status::success();
  }

  void close() override {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
    cv_.notify_all();
  }

  [[nodiscard]] bool is_connected() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
  }

  [[nodiscard]] ghostclaw::common::Status send_text(const std::string &payload) override {
    std::lock_guard<std::mutex> lock(mutex_);
    outbound_.push_back(payload);
    const int id = find_json_int_field(payload, "id");
    const std::string method = find_json_string_field(payload, "method");

    if (method == "Page.captureScreenshot") {
      inbound_.push_back("{\"id\":" + std::to_string(id) +
                         ",\"result\":{\"data\":\"base64-image\"}}");
    } else if (method == "Page.printToPDF") {
      inbound_.push_back("{\"id\":" + std::to_string(id) +
                         ",\"result\":{\"data\":\"base64-pdf\"}}");
    } else if (method == "Page.navigate") {
      inbound_.push_back("{\"id\":" + std::to_string(id) +
                         ",\"result\":{\"frameId\":\"frame-1\"}}");
    } else if (method == "Input.dispatchKeyEvent") {
      inbound_.push_back("{\"id\":" + std::to_string(id) + ",\"result\":{}}");
    } else if (method == "Accessibility.getFullAXTree") {
      inbound_.push_back("{\"id\":" + std::to_string(id) +
                         ",\"result\":{\"nodes\":\"[]\"}}");
    } else if (method == "Runtime.evaluate") {
      inbound_.push_back(
          "{\"id\":" + std::to_string(id) +
          ",\"result\":{\"result\":{\"type\":\"string\",\"value\":\"ok\"}}}");
    } else {
      inbound_.push_back("{\"id\":" + std::to_string(id) +
                         ",\"result\":{\"product\":\"Chrome/125\"}}");
    }
    cv_.notify_all();
    return ghostclaw::common::Status::success();
  }

  [[nodiscard]] ghostclaw::common::Result<std::string>
  receive_text(std::chrono::milliseconds timeout) override {
    std::unique_lock<std::mutex> lock(mutex_);
    const bool ready = cv_.wait_for(lock, timeout,
                                    [&]() { return !inbound_.empty() || !connected_; });
    if (!ready) {
      return ghostclaw::common::Result<std::string>::failure("timeout");
    }
    if (!connected_ && inbound_.empty()) {
      return ghostclaw::common::Result<std::string>::failure("closed");
    }
    const std::string value = inbound_.front();
    inbound_.pop_front();
    return ghostclaw::common::Result<std::string>::success(value);
  }

  void enqueue_event(const std::string &event_json) {
    std::lock_guard<std::mutex> lock(mutex_);
    inbound_.push_back(event_json);
    cv_.notify_all();
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool connected_ = false;
  std::deque<std::string> inbound_;
  std::vector<std::string> outbound_;
};

class FakeBrowserActions final : public ghostclaw::browser::IBrowserActions {
public:
  [[nodiscard]] ghostclaw::common::Result<ghostclaw::browser::BrowserActionResult>
  execute(const ghostclaw::browser::BrowserAction &action) override {
    seen.push_back(action);
    ghostclaw::browser::BrowserActionResult out;
    out.success = true;
    if (action.action == "navigate") {
      auto it = action.params.find("url");
      out.data["url"] = (it == action.params.end()) ? "" : it->second;
      out.data["status"] = "ok";
      return ghostclaw::common::Result<ghostclaw::browser::BrowserActionResult>::success(
          std::move(out));
    }
    if (action.action == "screenshot") {
      out.data["data"] = "base64-image";
      auto format_it = action.params.find("format");
      out.data["format"] = (format_it == action.params.end()) ? "png" : format_it->second;
      return ghostclaw::common::Result<ghostclaw::browser::BrowserActionResult>::success(
          std::move(out));
    }
    if (action.action == "snapshot") {
      out.data["nodes"] = "[]";
      return ghostclaw::common::Result<ghostclaw::browser::BrowserActionResult>::success(
          std::move(out));
    }
    if (action.action == "evaluate") {
      out.data["result"] = "ok";
      return ghostclaw::common::Result<ghostclaw::browser::BrowserActionResult>::success(
          std::move(out));
    }
    out.data["status"] = "ok";
    return ghostclaw::common::Result<ghostclaw::browser::BrowserActionResult>::success(
        std::move(out));
  }

  [[nodiscard]] ghostclaw::common::Result<std::vector<ghostclaw::browser::BrowserActionResult>>
  execute_batch(const std::vector<ghostclaw::browser::BrowserAction> &actions) override {
    std::vector<ghostclaw::browser::BrowserActionResult> out;
    out.reserve(actions.size());
    for (const auto &action : actions) {
      auto item = execute(action);
      if (!item.ok()) {
        return ghostclaw::common::Result<
            std::vector<ghostclaw::browser::BrowserActionResult>>::failure(item.error());
      }
      out.push_back(item.value());
    }
    return ghostclaw::common::Result<
        std::vector<ghostclaw::browser::BrowserActionResult>>::success(std::move(out));
  }

  std::vector<ghostclaw::browser::BrowserAction> seen;
};

} // namespace

void register_browser_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace b = ghostclaw::browser;

  tests.push_back({"browser_cdp_send_command_roundtrip", [] {
                     auto transport = std::make_unique<FakeCDPTransport>();
                     auto *raw = transport.get();
                     b::CDPClient client(std::move(transport));
                     auto connected = client.connect("ws://127.0.0.1:9222/devtools/browser");
                     require(connected.ok(), connected.error());

                     auto result = client.send_command("Browser.getVersion");
                     require(result.ok(), result.error());
                     require(result.value().at("product") == "Chrome/125",
                             "cdp result mismatch");
                     (void)raw;
                     client.disconnect();
                   }});

  tests.push_back({"browser_cdp_event_callback", [] {
                     auto transport = std::make_unique<FakeCDPTransport>();
                     auto *raw = transport.get();
                     b::CDPClient client(std::move(transport));
                     auto connected = client.connect("ws://127.0.0.1:9222/devtools/browser");
                     require(connected.ok(), connected.error());

                     std::mutex mutex;
                     std::condition_variable cv;
                     bool saw_event = false;
                     client.on_event("Network.requestWillBeSent",
                                     [&](const std::string &, const b::JsonMap &) {
                                       std::lock_guard<std::mutex> lock(mutex);
                                       saw_event = true;
                                       cv.notify_all();
                                     });

                     raw->enqueue_event(
                         R"({"method":"Network.requestWillBeSent","params":{"requestId":"1"}})");
                     std::unique_lock<std::mutex> lock(mutex);
                     const bool done = cv.wait_for(lock, std::chrono::milliseconds(300),
                                                   [&]() { return saw_event; });
                     require(done, "cdp event callback should fire");
                     client.disconnect();
                   }});

  tests.push_back({"browser_cdp_high_level_helpers", [] {
                     auto transport = std::make_unique<FakeCDPTransport>();
                     b::CDPClient client(std::move(transport));
                     auto connected = client.connect("ws://127.0.0.1:9222/devtools/browser");
                     require(connected.ok(), connected.error());

                     auto screenshot = client.capture_screenshot();
                     require(screenshot.ok(), screenshot.error());
                     require(screenshot.value() == "base64-image",
                             "capture_screenshot mismatch");

                     auto tree = client.get_accessibility_tree();
                     require(tree.ok(), tree.error());
                     require(tree.value().at("nodes") == "[]",
                             "accessibility tree mismatch");

                     auto eval = client.evaluate_js("1+1");
                     require(eval.ok(), eval.error());
                     require(eval.value().contains("result"),
                             "evaluate_js should return result payload");
                     client.disconnect();
                   }});

  tests.push_back({"browser_profiles_acquire_and_release", [] {
                     const auto root = std::filesystem::temp_directory_path() / "ghostclaw-browser-test";
                     std::vector<b::BrowserInstallation> injected{
                         {.kind = b::BrowserKind::Chromium,
                          .id = "chromium",
                          .display_name = "Chromium",
                          .executable = "/bin/echo",
                          .available = true}};
                     b::BrowserProfileManager manager(root, injected);

                     auto profile = manager.acquire_profile("phase11-session", "chromium");
                     require(profile.ok(), profile.error());
                     require(profile.value().devtools_port >= 18800 &&
                                 profile.value().devtools_port <= 18899,
                             "devtools port should be in reserved range");
                     require(!profile.value().color_hex.empty(), "profile color should be set");

                     const auto active = manager.list_active_profiles();
                     require(active.size() == 1, "active profile count mismatch");
                     auto released = manager.release_profile(profile.value().profile_id);
                     require(released.ok(), released.error());
                     require(manager.list_active_profiles().empty(),
                             "profile should be released");
                   }});

  tests.push_back({"browser_chrome_launch_args_and_ws_url", [] {
                     b::BrowserProfile profile;
                     profile.profile_id = "p1";
                     profile.browser_executable = "/bin/echo";
                     profile.user_data_dir = "/tmp/ghostclaw-browser-profile";
                     profile.devtools_port = 18888;

                     b::ChromeLaunchOptions options;
                     options.profile = profile;
                     options.start_url = "https://example.com";
                     options.headless = true;

                     auto args = b::build_chrome_launch_args(options);
                     require(args.ok(), args.error());
                     require(args.value().size() >= 5, "launch args should be populated");
                     require(args.value()[1].find("--remote-debugging-port=18888") == 0,
                             "missing debugging port argument");

                     auto ws = b::build_devtools_ws_url(18888, "/devtools/browser/test-id");
                     require(ws.ok(), ws.error());
                     require(ws.value() == "ws://127.0.0.1:18888/devtools/browser/test-id",
                             "devtools ws url mismatch");
                   }});

  tests.push_back({"browser_actions_execute_full_matrix", [] {
                     auto transport = std::make_unique<FakeCDPTransport>();
                     b::CDPClient client(std::move(transport));
                     auto connected = client.connect("ws://127.0.0.1:9222/devtools/browser");
                     require(connected.ok(), connected.error());

                     b::BrowserActions actions(client);
                     const std::vector<b::BrowserAction> batch{
                         {.action = "navigate", .params = {{"url", "https://example.com"}}},
                         {.action = "click", .params = {{"selector", "#submit"}}},
                         {.action = "type", .params = {{"text", "hello"}}},
                         {.action = "fill",
                          .params = {{"selector", "#email"}, {"value", "user@example.com"}}},
                         {.action = "press", .params = {{"key", "Enter"}}},
                         {.action = "hover", .params = {{"selector", "#menu"}}},
                         {.action = "drag", .params = {{"from", "#a"}, {"to", "#b"}}},
                         {.action = "select",
                          .params = {{"selector", "#country"}, {"value", "US"}}},
                         {.action = "scroll", .params = {{"x", "0"}, {"y", "240"}}},
                         {.action = "screenshot", .params = {{"format", "png"}}},
                         {.action = "snapshot", .params = {}},
                         {.action = "pdf", .params = {}},
                         {.action = "evaluate", .params = {{"expression", "1 + 1"}}}};

                     auto results = actions.execute_batch(batch);
                     require(results.ok(), results.error());
                     require(results.value().size() == batch.size(), "batch result count mismatch");
                     for (const auto &result : results.value()) {
                       require(result.success, "every action should report success");
                     }
                     require(results.value()[9].data.contains("data"),
                             "screenshot should return data");
                     require(results.value()[11].data["data"] == "base64-pdf",
                             "pdf data mismatch");

                     client.disconnect();
                   }});

  tests.push_back({"browser_actions_reject_unsupported_action", [] {
                     auto transport = std::make_unique<FakeCDPTransport>();
                     b::CDPClient client(std::move(transport));
                     auto connected = client.connect("ws://127.0.0.1:9222/devtools/browser");
                     require(connected.ok(), connected.error());

                     b::BrowserActions actions(client);
                     b::BrowserAction action;
                     action.action = "do_the_thing";
                     auto result = actions.execute(action);
                     require(!result.ok(), "unsupported action should fail");
                     require(result.error().find("unsupported browser action") != std::string::npos,
                             "unexpected unsupported action error");
                     client.disconnect();
                   }});

  tests.push_back({"browser_http_server_routes_and_tabs", [] {
                     FakeBrowserActions actions;
                     b::BrowserHttpServer server(actions);

                     b::BrowserHttpRequest open_req;
                     open_req.method = "POST";
                     open_req.path = "/tabs/open";
                     open_req.body = R"({"url":"https://example.com"})";
                     const auto open_resp = server.dispatch_for_test(open_req);
                     require(open_resp.status == 200, "tabs open should succeed");
                     const std::string tab_id = find_json_string_field(open_resp.body, "id");
                     require(!tab_id.empty(), "tabs open should return tab id");

                     b::BrowserHttpRequest list_req;
                     list_req.method = "GET";
                     list_req.path = "/tabs";
                     const auto list_resp = server.dispatch_for_test(list_req);
                     require(list_resp.status == 200, "tabs list should succeed");
                     require(list_resp.body.find(tab_id) != std::string::npos,
                             "tabs list should include opened tab");

                     b::BrowserHttpRequest navigate_req;
                     navigate_req.method = "POST";
                     navigate_req.path = "/navigate";
                     navigate_req.body = std::string("{\"tab_id\":\"") + tab_id +
                                         "\",\"url\":\"https://example.org\"}";
                     const auto navigate_resp = server.dispatch_for_test(navigate_req);
                     require(navigate_resp.status == 200, "navigate should succeed");

                     b::BrowserHttpRequest act_req;
                     act_req.method = "POST";
                     act_req.path = "/act";
                     act_req.body = R"({"action":"click","selector":"#ok"})";
                     const auto act_resp = server.dispatch_for_test(act_req);
                     require(act_resp.status == 200, "single act should succeed");
                     require(act_resp.body.find("\"success\":true") != std::string::npos,
                             "single act response should contain success");

                     b::BrowserHttpRequest batch_req;
                     batch_req.method = "POST";
                     batch_req.path = "/act";
                     batch_req.body =
                         R"({"actions":[{"action":"click","selector":"#one"},{"action":"type","text":"hello"}]})";
                     const auto batch_resp = server.dispatch_for_test(batch_req);
                     require(batch_resp.status == 200, "batch act should succeed");
                     require(batch_resp.body.find("\"count\":2") != std::string::npos,
                             "batch response should report action count");

                     b::BrowserHttpRequest screenshot_req;
                     screenshot_req.method = "POST";
                     screenshot_req.path = "/screenshot";
                     screenshot_req.body = std::string("{\"tab_id\":\"") + tab_id + "\"}";
                     const auto screenshot_resp = server.dispatch_for_test(screenshot_req);
                     require(screenshot_resp.status == 200, "screenshot should succeed");
                     require(screenshot_resp.body.find("base64-image") != std::string::npos,
                             "screenshot payload mismatch");

                     b::BrowserHttpRequest snapshot_req;
                     snapshot_req.method = "GET";
                     snapshot_req.path = "/snapshot";
                     const auto snapshot_resp = server.dispatch_for_test(snapshot_req);
                     require(snapshot_resp.status == 200, "snapshot should succeed");
                     require(snapshot_resp.body.find("\"nodes\":[]") != std::string::npos ||
                                 snapshot_resp.body.find("\"nodes\":\"[]\"") !=
                                     std::string::npos,
                             "snapshot payload mismatch");

                     b::BrowserHttpRequest close_req;
                     close_req.method = "DELETE";
                     close_req.path = "/tabs/" + tab_id;
                     const auto close_resp = server.dispatch_for_test(close_req);
                     require(close_resp.status == 200, "tabs close should succeed");

                     require(!actions.seen.empty(), "expected browser actions to be executed");
                   }});

  tests.push_back({"browser_http_server_start_stop", [] {
                     FakeBrowserActions actions;
                     b::BrowserHttpServer server(actions);
                     b::BrowserServerOptions options;
                     options.host = "127.0.0.1";
                     options.port = 0;
                     auto started = server.start(options);
                     if (!started.ok()) {
                       require(started.error().find("Operation not permitted") !=
                                   std::string::npos ||
                                   started.error().find("not implemented on Windows") !=
                                       std::string::npos,
                               "unexpected browser server start error");
                       return;
                     }
                     require(server.port() != 0, "server should bind ephemeral port");
                     require(server.is_running(), "server should report running");
                     server.stop();
                     require(!server.is_running(), "server should report stopped");
                   }});
}
