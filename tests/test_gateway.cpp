#include "test_framework.hpp"

#include "ghostclaw/agent/engine.hpp"
#include "ghostclaw/gateway/protocol.hpp"
#include "ghostclaw/gateway/server.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/sessions/store.hpp"
#include "ghostclaw/tools/tool_registry.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <unordered_map>

namespace {

std::filesystem::path make_temp_dir() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto base = std::filesystem::temp_directory_path() /
                    ("ghostclaw-gateway-test-" + std::to_string(rng()));
  std::filesystem::create_directories(base);
  return base;
}

class FakeMemory final : public ghostclaw::memory::IMemory {
public:
  [[nodiscard]] std::string_view name() const override { return "fake"; }
  [[nodiscard]] ghostclaw::common::Status store(const std::string &, const std::string &,
                                                ghostclaw::memory::MemoryCategory) override {
    return ghostclaw::common::Status::success();
  }
  [[nodiscard]] ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>
  recall(const std::string &, std::size_t) override {
    return ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>::success({});
  }
  [[nodiscard]] ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>
  get(const std::string &) override {
    return ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>::success(
        std::nullopt);
  }
  [[nodiscard]] ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>
  list(std::optional<ghostclaw::memory::MemoryCategory>) override {
    return ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>::success({});
  }
  [[nodiscard]] ghostclaw::common::Result<bool> forget(const std::string &) override {
    return ghostclaw::common::Result<bool>::success(false);
  }
  [[nodiscard]] ghostclaw::common::Result<std::size_t> count() override {
    return ghostclaw::common::Result<std::size_t>::success(0);
  }
  [[nodiscard]] ghostclaw::common::Status reindex() override {
    return ghostclaw::common::Status::success();
  }
  [[nodiscard]] bool health_check() override { return true; }
  [[nodiscard]] ghostclaw::memory::MemoryStats stats() override { return {}; }
};

class SequenceProvider final : public ghostclaw::providers::Provider {
public:
  explicit SequenceProvider(std::string response) : response_(std::move(response)) {}
  [[nodiscard]] ghostclaw::common::Result<std::string>
  chat(const std::string &, const std::string &, double) override {
    return ghostclaw::common::Result<std::string>::success(response_);
  }
  [[nodiscard]] ghostclaw::common::Result<std::string>
  chat_with_system(const std::optional<std::string> &, const std::string &, const std::string &,
                   double) override {
    return ghostclaw::common::Result<std::string>::success(response_);
  }
  [[nodiscard]] ghostclaw::common::Status warmup() override {
    return ghostclaw::common::Status::success();
  }
  [[nodiscard]] std::string name() const override { return "sequence"; }

private:
  std::string response_;
};

class ConcurrencyProbeProvider final : public ghostclaw::providers::Provider {
public:
  ConcurrencyProbeProvider(std::string response, std::size_t barrier_target,
                           std::chrono::milliseconds hold_time)
      : response_(std::move(response)), barrier_target_(barrier_target), hold_time_(hold_time) {}

  [[nodiscard]] ghostclaw::common::Result<std::string>
  chat(const std::string &, const std::string &, double) override {
    return execute(nullptr);
  }

  [[nodiscard]] ghostclaw::common::Result<std::string>
  chat_with_system(const std::optional<std::string> &, const std::string &, const std::string &,
                   double) override {
    return execute(nullptr);
  }

  [[nodiscard]] ghostclaw::common::Result<std::string>
  chat_with_system_stream(const std::optional<std::string> &, const std::string &,
                          const std::string &, double,
                          const ghostclaw::providers::StreamChunkCallback &on_chunk) override {
    return execute(&on_chunk);
  }

  [[nodiscard]] ghostclaw::common::Status warmup() override {
    return ghostclaw::common::Status::success();
  }

  [[nodiscard]] std::string name() const override { return "concurrency-probe"; }

  [[nodiscard]] int max_concurrency() const { return max_concurrency_.load(); }

private:
  [[nodiscard]] ghostclaw::common::Result<std::string>
  execute(const ghostclaw::providers::StreamChunkCallback *on_chunk) {
    const int current = active_.fetch_add(1) + 1;
    int observed = max_concurrency_.load();
    while (current > observed &&
           !max_concurrency_.compare_exchange_weak(observed, current)) {
    }

    if (barrier_target_ > 1) {
      std::unique_lock<std::mutex> lock(barrier_mutex_);
      ++barrier_arrivals_;
      if (barrier_arrivals_ < barrier_target_) {
        (void)barrier_cv_.wait_for(
            lock, std::chrono::milliseconds(250),
            [&]() { return barrier_arrivals_ >= barrier_target_; });
      } else {
        barrier_cv_.notify_all();
      }
    }

    std::this_thread::sleep_for(hold_time_);
    if (on_chunk != nullptr && *on_chunk) {
      (*on_chunk)(response_);
    }
    (void)active_.fetch_sub(1);
    return ghostclaw::common::Result<std::string>::success(response_);
  }

  std::string response_;
  std::size_t barrier_target_ = 1;
  std::chrono::milliseconds hold_time_{0};
  std::atomic<int> active_{0};
  std::atomic<int> max_concurrency_{0};
  std::mutex barrier_mutex_;
  std::condition_variable barrier_cv_;
  std::size_t barrier_arrivals_ = 0;
};

std::shared_ptr<ghostclaw::agent::AgentEngine>
make_engine_with_provider(const ghostclaw::config::Config &config,
                          const std::filesystem::path &workspace,
                          std::shared_ptr<ghostclaw::providers::Provider> provider) {
  auto memory = std::make_unique<FakeMemory>();
  ghostclaw::tools::ToolRegistry registry;
  return std::make_shared<ghostclaw::agent::AgentEngine>(config, std::move(provider),
                                                          std::move(memory), std::move(registry),
                                                          workspace);
}

std::shared_ptr<ghostclaw::agent::AgentEngine> make_engine(const ghostclaw::config::Config &config,
                                                            const std::filesystem::path &workspace) {
  auto provider = std::make_shared<SequenceProvider>("gateway-reply");
  return make_engine_with_provider(config, workspace, provider);
}

} // namespace

void register_gateway_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace gw = ghostclaw::gateway;

  tests.push_back({"gateway_server_binds_port", [] {
                     ghostclaw::config::Config config;
                     config.gateway.require_pairing = false;
                     const auto ws = make_temp_dir();
                     auto engine = make_engine(config, ws);

                     gw::GatewayServer server(config, engine);
                     gw::GatewayOptions options;
                     options.host = "127.0.0.1";
                     options.port = 0;
                     auto status = server.start(options);
                     require(status.ok(), status.error());
                     require(server.port() != 0, "expected non-zero bound port");
                     server.stop();
                   }});

  tests.push_back({"gateway_refuses_public_bind_without_tunnel", [] {
                     ghostclaw::config::Config config;
                     config.gateway.require_pairing = false;
                     config.gateway.allow_public_bind = false;
                     config.tunnel.provider = "none";
                     const auto ws = make_temp_dir();
                     auto engine = make_engine(config, ws);

                     gw::GatewayServer server(config, engine);
                     gw::GatewayOptions options;
                     options.host = "0.0.0.0";
                     options.port = 0;
                     auto status = server.start(options);
                     require(!status.ok(), "public bind should be rejected");
                   }});

  tests.push_back({"gateway_allows_public_bind_with_tunnel", [] {
                     ghostclaw::config::Config config;
                     config.gateway.require_pairing = false;
                     config.gateway.allow_public_bind = false;
                     config.tunnel.provider = "custom";
                     ghostclaw::config::CustomTunnelConfig custom;
                     custom.command = "/bin/sh";
                     custom.args = {"-c", "echo https://gateway-public.test; sleep 5"};
                     config.tunnel.custom = custom;
                     const auto ws = make_temp_dir();
                     auto engine = make_engine(config, ws);

                     gw::GatewayServer server(config, engine);
                     gw::GatewayOptions options;
                     options.host = "0.0.0.0";
                     options.port = 0;
                     auto status = server.start(options);
                     require(status.ok(), status.error());
                     require(server.public_url().has_value(), "expected tunnel URL");
                     server.stop();
                   }});

  tests.push_back({"gateway_health_endpoint_shape", [] {
                     ghostclaw::config::Config config;
                     config.api_key = "secret-api-key";
                     config.gateway.require_pairing = false;
                     const auto ws = make_temp_dir();
                     auto engine = make_engine(config, ws);

                     gw::GatewayServer server(config, engine);
                     gw::HttpRequest req;
                     req.method = "GET";
                     req.path = "/health";
                     auto resp = server.dispatch_for_test(req);
                     require(resp.status == 200, "health should return 200");
                     require(resp.body.find("\"status\":\"ok\"") != std::string::npos,
                             "health body missing status");
                     require(resp.body.find("secret-api-key") == std::string::npos,
                             "health should not leak secrets");
                   }});

  tests.push_back({"gateway_pair_and_webhook", [] {
                     ghostclaw::config::Config config;
                     config.gateway.require_pairing = true;
                     const auto ws = make_temp_dir();
                     auto engine = make_engine(config, ws);

                     gw::GatewayServer server(config, engine);
                     gw::GatewayOptions options;
                     options.host = "127.0.0.1";
                     options.port = 0;
                     auto started = server.start(options);
                     require(started.ok(), started.error());

                     gw::HttpRequest pair_req;
                     pair_req.method = "POST";
                     pair_req.path = "/pair";
                     pair_req.headers["x-pairing-code"] = server.pairing_code();
                     auto pair_resp = server.dispatch_for_test(pair_req);
                     require(pair_resp.status == 200, "pair should succeed");

                     const auto token_pos = pair_resp.body.find("\"token\":\"");
                     require(token_pos != std::string::npos, "pair response missing token");
                     const auto start = token_pos + 9;
                     const auto end = pair_resp.body.find('"', start);
                     require(end != std::string::npos, "token parse failed");
                     const std::string token = pair_resp.body.substr(start, end - start);

                     gw::HttpRequest webhook_req;
                     webhook_req.method = "POST";
                     webhook_req.path = "/webhook";
                     webhook_req.headers["authorization"] = "Bearer " + token;
                     webhook_req.body = R"({"message":"hello"})";
                     auto webhook_resp = server.dispatch_for_test(webhook_req);
                     require(webhook_resp.status == 200, "webhook should succeed");
                     require(webhook_resp.body.find("gateway-reply") != std::string::npos,
                             "webhook response mismatch");
                     require(
                         webhook_resp.body.find(
                             "\"session_id\":\"agent:ghostclaw:channel:webhook:peer:default\"") !=
                             std::string::npos,
                         "webhook should include normalized session key");

                     server.stop();
                   }});

  tests.push_back({"gateway_webhook_serializes_runs_per_session", [] {
                     ghostclaw::config::Config config;
                     config.gateway.require_pairing = false;
                     const auto ws = make_temp_dir();
                     auto provider = std::make_shared<ConcurrencyProbeProvider>(
                         "gateway-reply", 2, std::chrono::milliseconds(50));
                     auto engine = make_engine_with_provider(config, ws, provider);
                     gw::GatewayServer server(config, engine);

                     std::atomic<bool> go{false};
                     std::atomic<int> success_count{0};
                     auto run = [&](const std::string &session_id) {
                       while (!go.load(std::memory_order_acquire)) {
                         std::this_thread::yield();
                       }
                       gw::HttpRequest req;
                       req.method = "POST";
                       req.path = "/webhook";
                       req.body = std::string("{\"message\":\"hello\",\"session_id\":\"") +
                                  session_id + "\"}";
                       const auto resp = server.dispatch_for_test(req);
                       if (resp.status == 200) {
                         (void)success_count.fetch_add(1);
                       }
                     };

                     std::thread first(run, "same-session");
                     std::thread second(run, "same-session");
                     go.store(true, std::memory_order_release);
                     first.join();
                     second.join();

                     require(success_count.load() == 2, "both webhook calls should succeed");
                     require(provider->max_concurrency() == 1,
                             "same session should execute serially");
                   }});

  tests.push_back({"gateway_webhook_allows_parallel_runs_for_different_sessions", [] {
                     ghostclaw::config::Config config;
                     config.gateway.require_pairing = false;
                     const auto ws = make_temp_dir();
                     auto provider = std::make_shared<ConcurrencyProbeProvider>(
                         "gateway-reply", 2, std::chrono::milliseconds(50));
                     auto engine = make_engine_with_provider(config, ws, provider);
                     gw::GatewayServer server(config, engine);

                     std::atomic<bool> go{false};
                     std::atomic<int> success_count{0};
                     auto run = [&](const std::string &session_id) {
                       while (!go.load(std::memory_order_acquire)) {
                         std::this_thread::yield();
                       }
                       gw::HttpRequest req;
                       req.method = "POST";
                       req.path = "/webhook";
                       req.body = std::string("{\"message\":\"hello\",\"session_id\":\"") +
                                  session_id + "\"}";
                       const auto resp = server.dispatch_for_test(req);
                       if (resp.status == 200) {
                         (void)success_count.fetch_add(1);
                       }
                     };

                     std::thread first(run, "session-a");
                     std::thread second(run, "session-b");
                     go.store(true, std::memory_order_release);
                     first.join();
                     second.join();

                     require(success_count.load() == 2, "both webhook calls should succeed");
                     require(provider->max_concurrency() >= 2,
                             "different sessions should be allowed in parallel");
                   }});

  tests.push_back({"gateway_rpc_roundtrip", [] {
                     ghostclaw::config::Config config;
                     const auto ws = make_temp_dir();
                     auto engine = make_engine(config, ws);
                     FakeMemory memory;
                     ghostclaw::sessions::SessionStore session_store(ws / "sessions");
                     gw::RpcHandler rpc(engine, &memory, &session_store, config);

                     gw::RpcRequest req;
                     req.id = "1";
                     req.method = "agent.run";
                     req.params["message"] = "ping";
                     req.params["session_id"] = "agent:ghostclaw:channel:rpc:peer:test-peer";
                     auto resp = rpc.handle(req);
                     require(!resp.error.has_value(), "rpc should return success");
                     require(resp.result["content"] == "gateway-reply", "rpc content mismatch");
                     require(resp.result["session_id"] ==
                                 "agent:ghostclaw:channel:rpc:peer:test-peer",
                             "rpc session_id mismatch");

                     gw::RpcRequest history;
                     history.id = "2";
                     history.method = "session.history";
                     history.params["session_id"] = "agent:ghostclaw:channel:rpc:peer:test-peer";
                     history.params["limit"] = "10";
                     auto history_resp = rpc.handle(history);
                     require(!history_resp.error.has_value(), "session.history should succeed");
                     require(history_resp.result["count"] == "2",
                             "history should include user+assistant entries");
                   }});

  tests.push_back({"gateway_rpc_session_overrides_groups_and_provenance", [] {
                     ghostclaw::config::Config config;
                     const auto ws = make_temp_dir();
                     auto engine = make_engine(config, ws);
                     FakeMemory memory;
                     ghostclaw::sessions::SessionStore session_store(ws / "sessions");
                     gw::RpcHandler rpc(engine, &memory, &session_store, config);

                     gw::RpcRequest set_override;
                     set_override.id = "ov1";
                     set_override.method = "session.override.set";
                     set_override.params["session_id"] = "peer-42";
                     set_override.params["channel"] = "webhook";
                     set_override.params["model"] = "gpt-4.1-mini";
                     set_override.params["thinking_level"] = "high";
                     set_override.params["group_id"] = "team-red";
                     set_override.params["delivery_context"] = "webhook";
                     auto set_resp = rpc.handle(set_override);
                     require(!set_resp.error.has_value(), "session.override.set should succeed");
                     const std::string normalized_session = set_resp.result["session_id"];
                     require(normalized_session ==
                                 "agent:ghostclaw:channel:webhook:peer:peer-42",
                             "override session_id should be normalized");

                     gw::RpcRequest run_req;
                     run_req.id = "ov2";
                     run_req.method = "agent.run";
                     run_req.params["session_id"] = normalized_session;
                     run_req.params["message"] = "ping";
                     run_req.params["input_provenance_kind"] = "bridge";
                     run_req.params["input_provenance_source_tool"] = "router";
                     auto run_resp = rpc.handle(run_req);
                     require(!run_resp.error.has_value(), "agent.run should succeed");
                     require(run_resp.result["model"] == "gpt-4.1-mini",
                             "agent.run should use session override model");
                     require(run_resp.result["thinking_level"] == "high",
                             "agent.run should use session override thinking level");
                     require(run_resp.result["group_id"] == "team-red",
                             "agent.run should include group id");

                     gw::RpcRequest get_override;
                     get_override.id = "ov3";
                     get_override.method = "session.override.get";
                     get_override.params["session_id"] = "peer-42";
                     get_override.params["channel"] = "webhook";
                     auto get_resp = rpc.handle(get_override);
                     require(!get_resp.error.has_value(), "session.override.get should succeed");
                     require(get_resp.result["session_id"] == normalized_session,
                             "override get session mismatch");
                     require(get_resp.result["model"] == "gpt-4.1-mini",
                             "override get model mismatch");
                     require(get_resp.result["thinking_level"] == "high",
                             "override get thinking level mismatch");
                     require(get_resp.result["delivery_context"] == "webhook",
                             "override get delivery_context mismatch");
                     require(get_resp.result["group_id"] == "team-red",
                             "override get group mismatch");

                     gw::RpcRequest by_group;
                     by_group.id = "ov4";
                     by_group.method = "session.group.list";
                     by_group.params["group_id"] = "team-red";
                     auto group_resp = rpc.handle(by_group);
                     require(!group_resp.error.has_value(), "session.group.list should succeed");
                     require(group_resp.result["count"] == "1", "group size mismatch");
                     require(group_resp.result["session_0"] == normalized_session,
                             "group member mismatch");

                     gw::RpcRequest history_req;
                     history_req.id = "ov5";
                     history_req.method = "session.history";
                     history_req.params["session_id"] = normalized_session;
                     history_req.params["limit"] = "10";
                     auto history_resp = rpc.handle(history_req);
                     require(!history_resp.error.has_value(), "session.history should succeed");
                     require(history_resp.result["entries_json"].find("\"input_provenance\"") !=
                                 std::string::npos,
                             "history should include input provenance");
                     require(history_resp.result["entries_json"].find("\"source_tool\":\"router\"") !=
                                 std::string::npos,
                             "history provenance source_tool mismatch");
                   }});

  tests.push_back({"gateway_rpc_override_get_returns_defaults_for_missing_session", [] {
                     ghostclaw::config::Config config;
                     const auto ws = make_temp_dir();
                     auto engine = make_engine(config, ws);
                     FakeMemory memory;
                     ghostclaw::sessions::SessionStore session_store(ws / "sessions");
                     gw::RpcHandler rpc(engine, &memory, &session_store, config);

                     gw::RpcRequest get_override;
                     get_override.id = "ov-missing";
                     get_override.method = "session.override.get";
                     get_override.params["session_id"] = "ghost";
                     get_override.params["channel"] = "rpc";
                     auto resp = rpc.handle(get_override);
                     require(!resp.error.has_value(),
                             "session.override.get should return defaults when missing");
                     require(resp.result["session_id"] ==
                                 "agent:ghostclaw:channel:rpc:peer:ghost",
                             "missing session should normalize session id");
                     require(resp.result["model"] == config.default_model,
                             "missing session should use default model");
                     require(resp.result["thinking_level"] == "standard",
                             "missing session should use standard thinking");
                   }});

  tests.push_back({"gateway_ws_protocol_parse_subscribe", [] {
                     auto parsed = gw::parse_ws_client_message(
                         R"({"id":"abc","type":"subscribe","session":"agent:main","text":"hello"})");
                     require(parsed.ok(), parsed.error());
                     require(parsed.value().id == "abc", "id mismatch");
                     require(parsed.value().type == "subscribe", "type mismatch");
                     require(parsed.value().session == "agent:main", "session mismatch");
                     require(parsed.value().payload.at("text") == "hello", "payload mismatch");

                     gw::WsServerMessage outgoing{
                         .type = "event",
                         .id = "abc",
                         .session = "agent:main",
                         .payload = {{"event", "assistant.token"}, {"text", "hi"}},
                     };
                     const std::string json = outgoing.to_json();
                     require(json.find("\"type\":\"event\"") != std::string::npos, "missing type");
                     require(json.find("\"session\":\"agent:main\"") != std::string::npos,
                             "missing session");
                   }});

  tests.push_back({"gateway_ws_protocol_parse_rpc", [] {
                     auto parsed = gw::parse_ws_client_message(
                         R"({"id":"42","method":"agent.run","session_id":"s1","message":"ping"})");
                     require(parsed.ok(), parsed.error());
                     require(parsed.value().type == "rpc", "rpc type should be inferred");
                     require(parsed.value().method == "agent.run", "rpc method mismatch");
                     require(parsed.value().session == "s1", "session_id fallback mismatch");
                     require(parsed.value().payload.at("message") == "ping",
                             "rpc message payload mismatch");
                   }});

  tests.push_back({"gateway_ws_protocol_parse_rpc_numeric_fields", [] {
                     auto parsed = gw::parse_ws_client_message(
                         R"({"id":"43","method":"session.history","session_id":"s1","limit":25,"temperature":0.2})");
                     require(parsed.ok(), parsed.error());
                     require(parsed.value().payload.at("limit") == "25",
                             "numeric limit should be parsed");
                     require(parsed.value().payload.at("temperature") == "0.2",
                             "numeric temperature should be parsed");
                   }});

  tests.push_back({"gateway_ws_protocol_parse_rpc_override_fields", [] {
                     auto parsed = gw::parse_ws_client_message(
                         R"({"id":"44","method":"session.override.set","session_id":"s1","channel":"webhook","delivery_context":"webhook","group_id":"g1","thinking_level":"high"})");
                     require(parsed.ok(), parsed.error());
                     require(parsed.value().payload.at("channel") == "webhook",
                             "channel should be parsed");
                     require(parsed.value().payload.at("delivery_context") == "webhook",
                             "delivery_context should be parsed");
                     require(parsed.value().payload.at("group_id") == "g1",
                             "group_id should be parsed");
                     require(parsed.value().payload.at("thinking_level") == "high",
                             "thinking_level should be parsed");
                   }});

  tests.push_back({"gateway_webhook_session_send_policy_rate_limits", [] {
                     ghostclaw::config::Config config;
                     config.gateway.require_pairing = false;
                     config.gateway.session_send_policy_enabled = true;
                     config.gateway.session_send_policy_max_per_window = 1;
                     config.gateway.session_send_policy_window_seconds = 60;
                     const auto ws = make_temp_dir();
                     auto engine = make_engine(config, ws);
                     gw::GatewayServer server(config, engine);
                     gw::GatewayOptions options;
                     options.host = "127.0.0.1";
                     options.port = 0;
                     auto started = server.start(options);
                     require(started.ok(), started.error());

                     gw::HttpRequest first;
                     first.method = "POST";
                     first.path = "/webhook";
                     first.body = R"({"message":"hello","session_id":"ratelimit-peer"})";
                     auto first_resp = server.dispatch_for_test(first);
                     require(first_resp.status == 200, "first webhook should succeed");

                     gw::HttpRequest second = first;
                     auto second_resp = server.dispatch_for_test(second);
                     require(second_resp.status == 429,
                             "second webhook should be rate limited");
                     require(second_resp.body.find("session_rate_limited") != std::string::npos,
                             "rate limit error mismatch");
                     server.stop();
                   }});

  tests.push_back({"gateway_websocket_sidecar_enabled", [] {
                     ghostclaw::config::Config config;
                     config.gateway.require_pairing = false;
                     config.gateway.websocket_enabled = true;
                     config.gateway.websocket_port = 0;
                     config.gateway.websocket_host = "127.0.0.1";
                     const auto ws = make_temp_dir();
                     auto engine = make_engine(config, ws);

                     gw::GatewayServer server(config, engine);
                     gw::GatewayOptions options;
                     options.host = "127.0.0.1";
                     options.port = 0;
                     auto started = server.start(options);
                     require(started.ok(), started.error());
                     require(server.websocket_port() != 0, "websocket sidecar should bind");

                     gw::HttpRequest req;
                     req.method = "GET";
                     req.path = "/health";
                     const auto health = server.dispatch_for_test(req);
                     require(health.status == 200, "health status mismatch");
                     require(health.body.find("\"websocket\":\"ok\"") != std::string::npos,
                             "health should include websocket");
                     server.stop();
                   }});

  tests.push_back({"gateway_websocket_tls_requires_cert_and_key", [] {
                     ghostclaw::config::Config config;
                     config.gateway.require_pairing = false;
                     config.gateway.websocket_enabled = true;
                     config.gateway.websocket_tls_enabled = true;
                     config.gateway.websocket_tls_cert_file.clear();
                     config.gateway.websocket_tls_key_file.clear();
                     const auto ws = make_temp_dir();
                     auto engine = make_engine(config, ws);

                     gw::GatewayServer server(config, engine);
                     gw::GatewayOptions options;
                     options.host = "127.0.0.1";
                     options.port = 0;
                     auto started = server.start(options);
                     require(!started.ok(), "websocket tls should fail without cert/key");
                   }});
}
