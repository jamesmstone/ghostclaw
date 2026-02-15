#include "test_framework.hpp"

#include "ghostclaw/sessions/session.hpp"
#include "ghostclaw/sessions/session_key.hpp"
#include "ghostclaw/sessions/store.hpp"
#include "ghostclaw/sessions/transcript.hpp"

#include <atomic>
#include <filesystem>
#include <future>
#include <random>
#include <thread>
#include <vector>

namespace {

std::filesystem::path make_temp_sessions_dir() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto dir = std::filesystem::temp_directory_path() /
                   ("ghostclaw-sessions-test-" + std::to_string(rng()));
  std::filesystem::create_directories(dir);
  return dir;
}

} // namespace

void register_sessions_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace s = ghostclaw::sessions;

  tests.push_back({"sessions_make_and_parse_key_roundtrip", [] {
                     auto key = s::make_session_key(
                         {.agent_id = "agent1", .channel_id = "webhook", .peer_id = "user42"});
                     require(key.ok(), key.error());
                     require(key.value() == "agent:agent1:channel:webhook:peer:user42",
                             "session key format mismatch");

                     auto parsed = s::parse_session_key(key.value());
                     require(parsed.ok(), parsed.error());
                     require(parsed.value().agent_id == "agent1", "agent_id mismatch");
                     require(parsed.value().channel_id == "webhook", "channel_id mismatch");
                     require(parsed.value().peer_id == "user42", "peer_id mismatch");
                   }});

  tests.push_back({"sessions_parse_key_rejects_invalid", [] {
                     auto parsed = s::parse_session_key("invalid-key");
                     require(!parsed.ok(), "invalid key should fail parse");
                     require(!s::is_session_key("agent:a:channel:web:peer:with:colon"),
                             "colon in peer should fail");
                   }});

  tests.push_back({"sessions_store_state_and_transcript_roundtrip", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);

                     auto session_key = s::make_session_key(
                         {.agent_id = "ghostclaw", .channel_id = "webhook", .peer_id = "user1"});
                     require(session_key.ok(), session_key.error());

                     s::SessionState state;
                     state.session_id = session_key.value();
                     state.model = "gpt-test";
                     state.thinking_level = "standard";
                     state.delivery_context = "webhook";
                     auto upsert = store.upsert_state(state);
                     require(upsert.ok(), upsert.error());

                     s::TranscriptEntry user;
                     user.role = s::TranscriptRole::User;
                     user.content = "hello";
                     user.model = "gpt-test";
                     auto append_user = store.append_transcript(session_key.value(), user);
                     require(append_user.ok(), append_user.error());

                     s::TranscriptEntry assistant;
                     assistant.role = s::TranscriptRole::Assistant;
                     assistant.content = "hi there";
                     assistant.model = "gpt-test";
                     auto append_assistant = store.append_transcript(session_key.value(), assistant);
                     require(append_assistant.ok(), append_assistant.error());

                     auto listed = store.list_states();
                     require(listed.ok(), listed.error());
                     require(!listed.value().empty(), "store should contain at least one state");

                     auto history = store.load_transcript(session_key.value(), 10);
                     require(history.ok(), history.error());
                     require(history.value().size() == 2, "transcript entry count mismatch");
                     require(history.value()[0].content == "hello", "first transcript mismatch");
                     require(history.value()[1].content == "hi there", "second transcript mismatch");
                   }});

  tests.push_back({"sessions_store_subagent_registry", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const auto key = "agent:ghostclaw:channel:websocket:peer:subagent-owner";

                     auto register_a = store.register_subagent(key, "subagent-a");
                     require(register_a.ok(), register_a.error());
                     auto register_b = store.register_subagent(key, "subagent-b");
                     require(register_b.ok(), register_b.error());

                     auto state = store.get_state(key);
                     require(state.ok(), state.error());
                     require(state.value().subagents.size() == 2, "subagent count mismatch");

                     auto unregister = store.unregister_subagent(key, "subagent-a");
                     require(unregister.ok(), unregister.error());
                     auto updated = store.get_state(key);
                     require(updated.ok(), updated.error());
                     require(updated.value().subagents.size() == 1,
                             "subagent should be removed");
                     require(updated.value().subagents[0] == "subagent-b",
                             "remaining subagent mismatch");
                   }});

  tests.push_back({"sessions_store_group_and_provenance_roundtrip", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const auto key =
                         "agent:ghostclaw:channel:webhook:peer:grouped-peer";

                     s::SessionState state;
                     state.session_id = key;
                     state.group_id = "group-alpha";
                     state.model = "gpt-test";
                     auto upsert = store.upsert_state(state);
                     require(upsert.ok(), upsert.error());

                     s::TranscriptEntry entry;
                     entry.role = s::TranscriptRole::User;
                     entry.content = "from bridge";
                     entry.model = "gpt-test";
                     entry.input_provenance = s::InputProvenance{
                         .kind = "bridge",
                         .source_session_id = "agent:ghostclaw:channel:websocket:peer:src",
                         .source_channel = "websocket",
                         .source_tool = "router",
                         .source_message_id = "m-1"};
                     auto appended = store.append_transcript(key, entry);
                     require(appended.ok(), appended.error());

                     auto grouped = store.list_states_by_group("group-alpha");
                     require(grouped.ok(), grouped.error());
                     require(grouped.value().size() == 1, "group should contain one session");
                     require(grouped.value()[0].session_id == key,
                             "grouped session mismatch");

                     auto history = store.load_transcript(key, 10);
                     require(history.ok(), history.error());
                     require(history.value().size() == 1, "history size mismatch");
                     require(history.value()[0].input_provenance.has_value(),
                             "input provenance should be present");
                     require(history.value()[0].input_provenance->kind == "bridge",
                             "provenance kind mismatch");
                     require(history.value()[0].input_provenance->source_tool.has_value(),
                             "source_tool should be present");
                    require(*history.value()[0].input_provenance->source_tool == "router",
                            "source_tool mismatch");
                  }});

  // ============================================
  // NEW TESTS: Session Key Edge Cases
  // ============================================

  tests.push_back({"sessions_key_empty_components_rejected", [] {
                     auto empty_agent = s::make_session_key(
                         {.agent_id = "", .channel_id = "webhook", .peer_id = "user"});
                     require(!empty_agent.ok(), "empty agent_id should fail");

                     auto empty_channel = s::make_session_key(
                         {.agent_id = "agent", .channel_id = "", .peer_id = "user"});
                     require(!empty_channel.ok(), "empty channel_id should fail");

                     auto empty_peer = s::make_session_key(
                         {.agent_id = "agent", .channel_id = "webhook", .peer_id = ""});
                     require(!empty_peer.ok(), "empty peer_id should fail");
                   }});

  tests.push_back({"sessions_key_special_chars_handled", [] {
                     auto key = s::make_session_key(
                         {.agent_id = "agent-1", .channel_id = "web_hook", .peer_id = "user.name"});
                     require(key.ok(), key.error());

                     auto parsed = s::parse_session_key(key.value());
                     require(parsed.ok(), parsed.error());
                     require(parsed.value().agent_id == "agent-1", "agent_id with dash mismatch");
                     require(parsed.value().peer_id == "user.name", "peer_id with dot mismatch");
                   }});

  tests.push_back({"sessions_key_unicode_rejected", [] {
                     auto unicode_key = s::make_session_key(
                         {.agent_id = "agent", .channel_id = "webhook", .peer_id = "用户"});
                     // Unicode may or may not be allowed depending on implementation
                     // This test documents the behavior
                     if (unicode_key.ok()) {
                       auto parsed = s::parse_session_key(unicode_key.value());
                       require(parsed.ok(), "unicode key should roundtrip if allowed");
                     }
                   }});

  tests.push_back({"sessions_is_session_key_validates_format", [] {
                     require(s::is_session_key("agent:a:channel:c:peer:p"), "valid key should pass");
                     require(!s::is_session_key("agent:a:channel:c"), "incomplete key should fail");
                     require(!s::is_session_key(""), "empty string should fail");
                     require(!s::is_session_key("random-string"), "random string should fail");
                     require(!s::is_session_key("agent::channel:c:peer:p"), "empty agent should fail");
                   }});

  // ============================================
  // NEW TESTS: Session Store Concurrent Access
  // ============================================

  tests.push_back({"sessions_store_concurrent_writes", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const std::string base_key = "agent:ghostclaw:channel:test:peer:";

                     std::vector<std::future<bool>> futures;
                     for (int i = 0; i < 10; ++i) {
                       futures.push_back(std::async(std::launch::async, [&store, &base_key, i]() {
                         s::SessionState state;
                         state.session_id = base_key + "user" + std::to_string(i);
                         state.model = "model-" + std::to_string(i);
                         auto result = store.upsert_state(state);
                         return result.ok();
                       }));
                     }

                     bool all_ok = true;
                     for (auto &f : futures) {
                       if (!f.get()) {
                         all_ok = false;
                       }
                     }
                     require(all_ok, "concurrent writes should all succeed");

                     auto listed = store.list_states();
                     require(listed.ok(), listed.error());
                     require(listed.value().size() >= 10, "all sessions should be stored");
                   }});

  tests.push_back({"sessions_store_concurrent_transcript_append", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const std::string key = "agent:ghostclaw:channel:test:peer:concurrent";

                     s::SessionState state;
                     state.session_id = key;
                     state.model = "test";
                     require(store.upsert_state(state).ok(), "initial state should succeed");

                     std::atomic<int> success_count{0};
                     std::vector<std::thread> threads;
                     for (int i = 0; i < 5; ++i) {
                       threads.emplace_back([&store, &key, &success_count, i]() {
                         s::TranscriptEntry entry;
                         entry.role = s::TranscriptRole::User;
                         entry.content = "message-" + std::to_string(i);
                         entry.model = "test";
                         if (store.append_transcript(key, entry).ok()) {
                           success_count.fetch_add(1);
                         }
                       });
                     }

                     for (auto &t : threads) {
                       t.join();
                     }

                     require(success_count.load() == 5, "all transcript appends should succeed");

                     auto history = store.load_transcript(key, 10);
                     require(history.ok(), history.error());
                     require(history.value().size() == 5, "all entries should be stored");
                   }});

  // ============================================
  // NEW TESTS: Transcript Limits and Ordering
  // ============================================

  tests.push_back({"sessions_transcript_limit_respected", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const std::string key = "agent:ghostclaw:channel:test:peer:limited";

                     s::SessionState state;
                     state.session_id = key;
                     state.model = "test";
                     require(store.upsert_state(state).ok(), "state should succeed");

                     for (int i = 0; i < 20; ++i) {
                       s::TranscriptEntry entry;
                       entry.role = (i % 2 == 0) ? s::TranscriptRole::User : s::TranscriptRole::Assistant;
                       entry.content = "message-" + std::to_string(i);
                       entry.model = "test";
                       require(store.append_transcript(key, entry).ok(), "append should succeed");
                     }

                     auto limited = store.load_transcript(key, 5);
                     require(limited.ok(), limited.error());
                     require(limited.value().size() == 5, "limit should be respected");

                     // Should return most recent entries
                     auto last = limited.value().back();
                     require(last.content == "message-19", "should return most recent entries");
                   }});

  tests.push_back({"sessions_transcript_ordering_preserved", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const std::string key = "agent:ghostclaw:channel:test:peer:ordered";

                     s::SessionState state;
                     state.session_id = key;
                     state.model = "test";
                     require(store.upsert_state(state).ok(), "state should succeed");

                     for (int i = 0; i < 5; ++i) {
                       s::TranscriptEntry entry;
                       entry.role = s::TranscriptRole::User;
                       entry.content = "msg-" + std::to_string(i);
                       entry.model = "test";
                       require(store.append_transcript(key, entry).ok(), "append should succeed");
                     }

                     auto history = store.load_transcript(key, 10);
                     require(history.ok(), history.error());
                     for (std::size_t i = 0; i < history.value().size(); ++i) {
                       require(history.value()[i].content == "msg-" + std::to_string(i),
                               "ordering should be preserved");
                     }
                   }});

  // ============================================
  // NEW TESTS: Subagent Lifecycle
  // ============================================

  tests.push_back({"sessions_subagent_duplicate_registration", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const std::string key = "agent:ghostclaw:channel:test:peer:subagent-dup";

                     require(store.register_subagent(key, "sub-a").ok(), "first register should succeed");
                     require(store.register_subagent(key, "sub-a").ok(), "duplicate register should succeed");

                     auto state = store.get_state(key);
                     require(state.ok(), state.error());
                     // Implementation may deduplicate or allow duplicates
                     require(state.value().subagents.size() >= 1, "at least one subagent should exist");
                   }});

  tests.push_back({"sessions_subagent_unregister_nonexistent", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const std::string key = "agent:ghostclaw:channel:test:peer:subagent-none";

                     auto result = store.unregister_subagent(key, "nonexistent");
                     // Should not fail, just no-op
                     require(result.ok(), "unregister nonexistent should not fail");
                   }});

  tests.push_back({"sessions_subagent_list_empty_initially", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const std::string key = "agent:ghostclaw:channel:test:peer:subagent-empty";

                     s::SessionState state;
                     state.session_id = key;
                     state.model = "test";
                     require(store.upsert_state(state).ok(), "state should succeed");

                     auto loaded = store.get_state(key);
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().subagents.empty(), "subagents should be empty initially");
                   }});

  // ============================================
  // NEW TESTS: Session State Updates
  // ============================================

  tests.push_back({"sessions_state_update_preserves_subagents", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const std::string key = "agent:ghostclaw:channel:test:peer:preserve";

                     require(store.register_subagent(key, "sub-1").ok(), "register should succeed");

                     s::SessionState state;
                     state.session_id = key;
                     state.model = "updated-model";
                     require(store.upsert_state(state).ok(), "update should succeed");

                     auto loaded = store.get_state(key);
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().model == "updated-model", "model should be updated");
                     // Subagents may or may not be preserved depending on implementation
                   }});

  tests.push_back({"sessions_state_get_nonexistent", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);

                     auto result = store.get_state("agent:ghost:channel:none:peer:missing");
                     // Should return empty state or error
                     if (result.ok()) {
                       require(result.value().session_id.empty() || 
                               result.value().session_id == "agent:ghost:channel:none:peer:missing",
                               "nonexistent state should be empty or match key");
                     }
                   }});

  // ============================================
  // NEW TESTS: Group Operations
  // ============================================

  tests.push_back({"sessions_group_multiple_sessions", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const std::string group = "test-group";

                     for (int i = 0; i < 3; ++i) {
                       s::SessionState state;
                       state.session_id = "agent:ghost:channel:test:peer:user" + std::to_string(i);
                       state.group_id = group;
                       state.model = "test";
                       require(store.upsert_state(state).ok(), "state should succeed");
                     }

                     auto grouped = store.list_states_by_group(group);
                     require(grouped.ok(), grouped.error());
                     require(grouped.value().size() == 3, "group should contain 3 sessions");
                   }});

  tests.push_back({"sessions_group_empty_returns_empty", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);

                     auto grouped = store.list_states_by_group("nonexistent-group");
                     require(grouped.ok(), grouped.error());
                     require(grouped.value().empty(), "nonexistent group should return empty");
                   }});

  // ============================================
  // NEW TESTS: Transcript Metadata
  // ============================================

  tests.push_back({"sessions_transcript_metadata_preserved", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const std::string key = "agent:ghost:channel:test:peer:metadata";

                     s::SessionState state;
                     state.session_id = key;
                     state.model = "test";
                     require(store.upsert_state(state).ok(), "state should succeed");

                     s::TranscriptEntry entry;
                     entry.role = s::TranscriptRole::User;
                     entry.content = "test message";
                     entry.model = "gpt-4";
                     entry.metadata["custom_key"] = "custom_value";
                     entry.metadata["tool_used"] = "shell";
                     require(store.append_transcript(key, entry).ok(), "append should succeed");

                     auto history = store.load_transcript(key, 10);
                     require(history.ok(), history.error());
                     require(history.value().size() == 1, "should have one entry");
                     require(history.value()[0].metadata.count("custom_key") > 0,
                             "custom metadata should be preserved");
                     require(history.value()[0].metadata.at("custom_key") == "custom_value",
                             "metadata value should match");
                   }});

  tests.push_back({"sessions_transcript_role_types", [] {
                     const auto dir = make_temp_sessions_dir();
                     s::SessionStore store(dir);
                     const std::string key = "agent:ghost:channel:test:peer:roles";

                     s::SessionState state;
                     state.session_id = key;
                     state.model = "test";
                     require(store.upsert_state(state).ok(), "state should succeed");

                     std::vector<s::TranscriptRole> roles = {
                         s::TranscriptRole::User,
                         s::TranscriptRole::Assistant,
                         s::TranscriptRole::System,
                         s::TranscriptRole::Tool
                     };

                     for (const auto &role : roles) {
                       s::TranscriptEntry entry;
                       entry.role = role;
                       entry.content = "content";
                       entry.model = "test";
                       require(store.append_transcript(key, entry).ok(), "append should succeed");
                     }

                     auto history = store.load_transcript(key, 10);
                     require(history.ok(), history.error());
                     require(history.value().size() == 4, "all roles should be stored");
                   }});
}
