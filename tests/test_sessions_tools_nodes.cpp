#include "test_framework.hpp"

#include "ghostclaw/nodes/discovery.hpp"
#include "ghostclaw/nodes/node.hpp"
#include "ghostclaw/security/policy.hpp"
#include "ghostclaw/sessions/session_key.hpp"
#include "ghostclaw/sessions/store.hpp"
#include "ghostclaw/tools/builtin/sessions.hpp"

#include <filesystem>
#include <algorithm>
#include <memory>
#include <random>
#include <string>

namespace {

std::filesystem::path make_temp_dir() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto dir = std::filesystem::temp_directory_path() /
                   ("ghostclaw-sessions-tools-nodes-test-" + std::to_string(rng()));
  std::filesystem::create_directories(dir);
  return dir;
}

std::string extract_json_field(const std::string &json, const std::string &field) {
  const std::string key = "\"" + field + "\":\"";
  const auto pos = json.find(key);
  if (pos == std::string::npos) {
    return "";
  }
  const auto start = pos + key.size();
  const auto end = json.find('"', start);
  if (end == std::string::npos) {
    return "";
  }
  return json.substr(start, end - start);
}

} // namespace

void register_sessions_tools_nodes_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace tools = ghostclaw::tools;
  namespace sessions = ghostclaw::sessions;
  namespace nodes = ghostclaw::nodes;

  tests.push_back({"sessions_tools_send_history_list", [] {
                     const auto dir = make_temp_dir();
                     auto store = std::make_shared<sessions::SessionStore>(dir / "sessions");

                     auto parent_key = sessions::make_session_key(
                         {.agent_id = "ghostclaw", .channel_id = "local", .peer_id = "main"});
                     require(parent_key.ok(), parent_key.error());
                     auto target_key = sessions::make_session_key(
                         {.agent_id = "ghostclaw", .channel_id = "local", .peer_id = "target"});
                     require(target_key.ok(), target_key.error());

                     tools::ToolContext ctx;
                     ctx.workspace_path = dir;
                     ctx.agent_id = "ghostclaw";
                     ctx.session_id = parent_key.value();

                     tools::SessionsSendTool send_tool(store);
                     auto sent = send_tool.execute(
                         {{"session_id", target_key.value()}, {"message", "hello target"}}, ctx);
                     require(sent.ok(), sent.error());
                     require(sent.value().output.find("accepted") != std::string::npos,
                             "sessions_send should accept");

                     tools::SessionsHistoryTool history_tool(store);
                     auto history = history_tool.execute(
                         {{"session_id", target_key.value()}, {"limit", "10"}}, ctx);
                     require(history.ok(), history.error());
                     require(history.value().output.find("hello target") != std::string::npos,
                             "history should include sent message");

                     tools::SessionsListTool list_tool(store);
                     auto listed = list_tool.execute({{"limit", "10"}}, ctx);
                     require(listed.ok(), listed.error());
                     require(listed.value().output.find(target_key.value()) != std::string::npos,
                             "sessions_list should include target session");
                   }});

  tests.push_back({"sessions_spawn_and_subagents_actions", [] {
                     const auto dir = make_temp_dir();
                     auto store = std::make_shared<sessions::SessionStore>(dir / "sessions");

                     auto parent_key = sessions::make_session_key(
                         {.agent_id = "ghostclaw", .channel_id = "local", .peer_id = "main"});
                     require(parent_key.ok(), parent_key.error());

                     sessions::SessionState parent;
                     parent.session_id = parent_key.value();
                     parent.agent_id = "ghostclaw";
                     parent.channel_id = "local";
                     parent.peer_id = "main";
                     auto upserted_parent = store->upsert_state(parent);
                     require(upserted_parent.ok(), upserted_parent.error());

                     tools::ToolContext ctx;
                     ctx.workspace_path = dir;
                     ctx.agent_id = "ghostclaw";
                     ctx.session_id = parent_key.value();

                     tools::SessionsSpawnTool spawn_tool(store);
                     auto spawned = spawn_tool.execute(
                         {{"task", "Check project health"}, {"parent_session_id", parent_key.value()}},
                         ctx);
                     require(spawned.ok(), spawned.error());

                     const std::string child_session_id =
                         extract_json_field(spawned.value().output, "child_session_id");
                     require(!child_session_id.empty(), "spawn should return child_session_id");

                     auto child_state = store->get_state(child_session_id);
                     require(child_state.ok(), child_state.error());

                     tools::SubagentsTool subagents_tool(store);
                     auto listed = subagents_tool.execute(
                         {{"action", "list"}, {"parent_session_id", parent_key.value()}}, ctx);
                     require(listed.ok(), listed.error());
                     require(listed.value().output.find(child_session_id) != std::string::npos,
                             "subagents list should include spawned child");

                     auto steered = subagents_tool.execute(
                         {{"action", "steer"},
                          {"parent_session_id", parent_key.value()},
                          {"target", child_session_id},
                          {"message", "Continue with markdown summary"}},
                         ctx);
                     require(steered.ok(), steered.error());

                     auto child_history = store->load_transcript(child_session_id, 20);
                     require(child_history.ok(), child_history.error());
                     bool found_steer_message = false;
                     for (const auto &entry : child_history.value()) {
                       if (entry.content.find("markdown summary") != std::string::npos) {
                         found_steer_message = true;
                         break;
                       }
                     }
                     require(found_steer_message, "steer message should be appended to child transcript");

                     auto killed = subagents_tool.execute(
                         {{"action", "kill"},
                          {"parent_session_id", parent_key.value()},
                          {"target", child_session_id}},
                         ctx);
                     require(killed.ok(), killed.error());

                     auto parent_after = store->get_state(parent_key.value());
                     require(parent_after.ok(), parent_after.error());
                     require(std::find(parent_after.value().subagents.begin(),
                                       parent_after.value().subagents.end(),
                                       child_session_id) == parent_after.value().subagents.end(),
                             "killed child should be removed from parent subagent list");
                   }});

  tests.push_back({"nodes_registry_pairing_flow", [] {
                     nodes::NodeRegistry registry;
                     nodes::NodeDescriptor descriptor;
                     descriptor.node_id = "node-alpha";
                     descriptor.display_name = "Node Alpha";
                     descriptor.endpoint = "ws://127.0.0.1:8787";
                     descriptor.capabilities = {"system.run"};
                     auto advertised = registry.advertise(descriptor);
                     require(advertised.ok(), advertised.error());

                     auto request = registry.create_pairing_request(
                         "node-alpha", {"camera.snap", "system.run"});
                     require(request.ok(), request.error());
                     require(!registry.pending_pairings().empty(),
                             "pairing request should be pending");

                     auto approved = registry.approve_pairing(request.value().request_id, "token-123");
                     require(approved.ok(), approved.error());
                     require(approved.value().paired, "node should be marked paired");
                     require(approved.value().connected, "approved node should be connected");
                     require(approved.value().pair_token == "token-123", "pair token mismatch");
                     require(registry.pending_pairings().empty(),
                             "pending pairings should be empty after approve");
                   }});

  tests.push_back({"nodes_discovery_and_ws_pairing_protocol", [] {
#ifdef _WIN32
                     _putenv_s("GHOSTCLAW_MDNS_NODES",
                               "node-1@127.0.0.1:8787#camera.snap;system.run");
#else
                     setenv("GHOSTCLAW_MDNS_NODES",
                            "node-1@127.0.0.1:8787#camera.snap;system.run", 1);
#endif
                     const auto discovered = nodes::NodeDiscovery::discover_bonjour();
                     require(discovered.size() == 1, "should discover one node from env override");
                     require(discovered[0].node_id == "node-1", "discovered node id mismatch");

                     nodes::CapabilityAdvertisement advertisement;
                     advertisement.node_id = "node-1";
                     advertisement.display_name = "Node One";
                     advertisement.websocket_url = "ws://127.0.0.1:8787";
                     advertisement.capabilities = {"camera.snap", "system.run"};

                     const std::string encoded =
                         nodes::NodeDiscovery::encode_capability_advertisement(advertisement);
                     auto decoded = nodes::NodeDiscovery::decode_capability_advertisement(encoded);
                     require(decoded.ok(), decoded.error());
                     require(decoded.value().node_id == "node-1", "decoded node_id mismatch");
                     require(decoded.value().capabilities.size() == 2,
                             "decoded capabilities count mismatch");

                     const std::string hello = nodes::WebSocketPairingProtocol::build_pairing_hello(
                         "node-1", "nonce-abc", {"system.run"});
                     auto parsed = nodes::WebSocketPairingProtocol::parse_pairing_hello(hello);
                     require(parsed.ok(), parsed.error());
                     require(parsed.value().at("node_id") == "node-1", "pairing node_id mismatch");

                     auto accept = nodes::WebSocketPairingProtocol::websocket_accept_key(
                         "dGhlIHNhbXBsZSBub25jZQ==");
                     require(accept.ok(), accept.error());
                     require(accept.value() == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
                             "websocket accept key mismatch");
#ifdef _WIN32
                     _putenv_s("GHOSTCLAW_MDNS_NODES", "");
#else
                     unsetenv("GHOSTCLAW_MDNS_NODES");
#endif
                   }});

  tests.push_back({"nodes_actions_system_run_and_location", [] {
                     const auto dir = make_temp_dir();
                     auto policy = std::make_shared<ghostclaw::security::SecurityPolicy>();
                     policy->workspace_dir = dir;
                     policy->workspace_only = true;
                     policy->allowed_commands = {"echo"};

                     nodes::NodeActionExecutor executor(policy);
                     tools::ToolContext ctx;
                     ctx.workspace_path = dir;

                     auto run_ok = executor.invoke("system.run", {{"command", "echo hello-node"}}, ctx);
                     require(run_ok.ok(), run_ok.error());
                     require(run_ok.value().success, "system.run allowed command should succeed");
                     require(run_ok.value().output.find("hello-node") != std::string::npos,
                             "system.run output mismatch");

                     auto run_denied =
                         executor.invoke("system.run", {{"command", "rm -rf /tmp/nope"}}, ctx);
                     require(!run_denied.ok(), "disallowed command should be rejected");

#ifdef _WIN32
                     _putenv_s("GHOSTCLAW_GPS_LAT", "40.7");
                     _putenv_s("GHOSTCLAW_GPS_LON", "-74.0");
#else
                     setenv("GHOSTCLAW_GPS_LAT", "40.7", 1);
                     setenv("GHOSTCLAW_GPS_LON", "-74.0", 1);
#endif
                     auto location = executor.invoke("location.get", {}, ctx);
                     require(location.ok(), location.error());
                     require(location.value().success, "location.get should succeed with env vars");
                     require(location.value().output.find("40.7") != std::string::npos,
                             "location output should contain latitude");
#ifdef _WIN32
                     _putenv_s("GHOSTCLAW_GPS_LAT", "");
                     _putenv_s("GHOSTCLAW_GPS_LON", "");
#else
                     unsetenv("GHOSTCLAW_GPS_LAT");
                     unsetenv("GHOSTCLAW_GPS_LON");
#endif
                   }});
}
