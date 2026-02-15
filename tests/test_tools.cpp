#include "test_framework.hpp"

#include "ghostclaw/agent/tool_executor.hpp"
#include "ghostclaw/canvas/host.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/security/approval.hpp"
#include "ghostclaw/security/policy.hpp"
#include "ghostclaw/security/tool_policy.hpp"
#include "ghostclaw/tools/approval.hpp"
#include "ghostclaw/tools/builtin/browser.hpp"
#include "ghostclaw/tools/builtin/canvas.hpp"
#include "ghostclaw/tools/builtin/calendar.hpp"
#include "ghostclaw/tools/builtin/email.hpp"
#include "ghostclaw/tools/builtin/file_edit.hpp"
#include "ghostclaw/tools/builtin/file_read.hpp"
#include "ghostclaw/tools/builtin/file_write.hpp"
#include "ghostclaw/tools/builtin/memory_forget.hpp"
#include "ghostclaw/tools/builtin/memory_recall.hpp"
#include "ghostclaw/tools/builtin/memory_store.hpp"
#include "ghostclaw/tools/builtin/message.hpp"
#include "ghostclaw/tools/builtin/reminder.hpp"
#include "ghostclaw/tools/builtin/shell.hpp"
#include "ghostclaw/tools/builtin/skills.hpp"
#include "ghostclaw/tools/builtin/web_fetch.hpp"
#include "ghostclaw/tools/builtin/web_search.hpp"
#include "ghostclaw/tools/plugin/plugin_loader.hpp"
#include "ghostclaw/tools/policy.hpp"
#include "ghostclaw/tools/tool_registry.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <thread>
#include <unordered_map>

namespace {

std::filesystem::path make_temp_dir() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto base = std::filesystem::temp_directory_path() /
                    ("ghostclaw-tools-test-" + std::to_string(rng()));
  std::filesystem::create_directories(base);
  return base;
}

std::shared_ptr<ghostclaw::security::SecurityPolicy>
make_policy(const std::filesystem::path &workspace) {
  auto policy = std::make_shared<ghostclaw::security::SecurityPolicy>();
  policy->workspace_dir = workspace;
  policy->workspace_only = true;
  policy->allowed_commands = {"echo", "python", "ls", "cat"};
  policy->forbidden_paths = {"/etc", "/root", "/proc", "/sys"};
  policy->autonomy = ghostclaw::security::AutonomyLevel::Full;
  return policy;
}

class FakeMemory final : public ghostclaw::memory::IMemory {
public:
  [[nodiscard]] std::string_view name() const override { return "fake"; }

  [[nodiscard]] ghostclaw::common::Status store(const std::string &key, const std::string &content,
                                                ghostclaw::memory::MemoryCategory category) override {
    ghostclaw::memory::MemoryEntry entry;
    entry.key = key;
    entry.content = content;
    entry.category = category;
    entry.created_at = "2024-01-01T00:00:00Z";
    entry.updated_at = entry.created_at;
    data[key] = std::move(entry);
    ++store_calls;
    return ghostclaw::common::Status::success();
  }

  [[nodiscard]] ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>
  recall(const std::string &, std::size_t limit) override {
    std::vector<ghostclaw::memory::MemoryEntry> out;
    for (const auto &[_, entry] : data) {
      out.push_back(entry);
    }
    if (out.size() > limit) {
      out.resize(limit);
    }
    return ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>::success(
        std::move(out));
  }

  [[nodiscard]] ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>
  get(const std::string &key) override {
    if (!data.contains(key)) {
      return ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>::success(
          std::nullopt);
    }
    return ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>::success(
        data[key]);
  }

  [[nodiscard]] ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>
  list(std::optional<ghostclaw::memory::MemoryCategory>) override {
    std::vector<ghostclaw::memory::MemoryEntry> out;
    for (const auto &[_, entry] : data) {
      out.push_back(entry);
    }
    return ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>::success(
        std::move(out));
  }

  [[nodiscard]] ghostclaw::common::Result<bool> forget(const std::string &key) override {
    const auto erased = data.erase(key);
    return ghostclaw::common::Result<bool>::success(erased > 0);
  }

  [[nodiscard]] ghostclaw::common::Result<std::size_t> count() override {
    return ghostclaw::common::Result<std::size_t>::success(data.size());
  }

  [[nodiscard]] ghostclaw::common::Status reindex() override {
    return ghostclaw::common::Status::success();
  }

  [[nodiscard]] bool health_check() override { return true; }

  [[nodiscard]] ghostclaw::memory::MemoryStats stats() override {
    ghostclaw::memory::MemoryStats st;
    st.total_entries = data.size();
    return st;
  }

  std::unordered_map<std::string, ghostclaw::memory::MemoryEntry> data;
  std::size_t store_calls = 0;
};

class DummySafeTool final : public ghostclaw::tools::ITool {
public:
  [[nodiscard]] std::string_view name() const override { return "dummy_safe"; }
  [[nodiscard]] std::string_view description() const override { return "safe dummy tool"; }
  [[nodiscard]] std::string parameters_schema() const override {
    return R"({"type":"object","properties":{"value":{"type":"string"}}})";
  }
  [[nodiscard]] ghostclaw::common::Result<ghostclaw::tools::ToolResult>
  execute(const ghostclaw::tools::ToolArgs &, const ghostclaw::tools::ToolContext &) override {
    ghostclaw::tools::ToolResult result;
    result.output = "ok";
    return ghostclaw::common::Result<ghostclaw::tools::ToolResult>::success(std::move(result));
  }
  [[nodiscard]] bool is_safe() const override { return true; }
  [[nodiscard]] std::string_view group() const override { return "test"; }
};

class SleepTool final : public ghostclaw::tools::ITool {
public:
  explicit SleepTool(std::string tool_name, int millis) : name_(std::move(tool_name)), millis_(millis) {}

  [[nodiscard]] std::string_view name() const override { return name_; }
  [[nodiscard]] std::string_view description() const override { return "sleep tool"; }
  [[nodiscard]] std::string parameters_schema() const override { return R"({"type":"object"})"; }
  [[nodiscard]] ghostclaw::common::Result<ghostclaw::tools::ToolResult>
  execute(const ghostclaw::tools::ToolArgs &, const ghostclaw::tools::ToolContext &) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(millis_));
    ghostclaw::tools::ToolResult result;
    result.output = "slept";
    return ghostclaw::common::Result<ghostclaw::tools::ToolResult>::success(std::move(result));
  }
  [[nodiscard]] bool is_safe() const override { return true; }
  [[nodiscard]] std::string_view group() const override { return "test"; }

private:
  std::string name_;
  int millis_ = 0;
};

class AlwaysFailTool final : public ghostclaw::tools::ITool {
public:
  [[nodiscard]] std::string_view name() const override { return "always_fail"; }
  [[nodiscard]] std::string_view description() const override { return "always failing tool"; }
  [[nodiscard]] std::string parameters_schema() const override { return R"({"type":"object"})"; }
  [[nodiscard]] ghostclaw::common::Result<ghostclaw::tools::ToolResult>
  execute(const ghostclaw::tools::ToolArgs &, const ghostclaw::tools::ToolContext &) override {
    return ghostclaw::common::Result<ghostclaw::tools::ToolResult>::failure("fail");
  }
  [[nodiscard]] bool is_safe() const override { return false; }
  [[nodiscard]] std::string_view group() const override { return "test"; }
};

class SafeShellTool final : public ghostclaw::tools::ITool {
public:
  [[nodiscard]] std::string_view name() const override { return "shell"; }
  [[nodiscard]] std::string_view description() const override { return "safe shell for approval test"; }
  [[nodiscard]] std::string parameters_schema() const override { return R"({"type":"object"})"; }
  [[nodiscard]] ghostclaw::common::Result<ghostclaw::tools::ToolResult>
  execute(const ghostclaw::tools::ToolArgs &, const ghostclaw::tools::ToolContext &) override {
    ghostclaw::tools::ToolResult result;
    result.output = "ok";
    return ghostclaw::common::Result<ghostclaw::tools::ToolResult>::success(std::move(result));
  }
  [[nodiscard]] bool is_safe() const override { return true; }
  [[nodiscard]] std::string_view group() const override { return "runtime"; }
};

} // namespace

void register_tools_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace tools = ghostclaw::tools;
  namespace agent = ghostclaw::agent;

  tests.push_back({"tool_spec_generation", [] {
                     DummySafeTool tool;
                     const auto spec = tool.spec();
                     require(spec.name == "dummy_safe", "spec name mismatch");
                     require(spec.safe, "safe flag mismatch");
                   }});

  tests.push_back({"tool_policy_group_expansion", [] {
                     auto expanded = tools::ToolPolicy::expand_group("fs");
                     require(expanded.size() == 3, "fs group size mismatch");
                   }});

  tests.push_back({"tool_policy_deny_overrides_allow", [] {
                     tools::ToolPolicy policy({"runtime"}, {"shell"}, {"shell"});
                     require(!policy.is_allowed("shell"), "deny should override allow");
                   }});

  tests.push_back({"tool_registry_register_lookup", [] {
                     tools::ToolRegistry registry;
                     registry.register_tool(std::make_unique<DummySafeTool>());
                     auto *tool = registry.get_tool("dummy_safe");
                     require(tool != nullptr, "tool lookup failed");
                     require(registry.all_specs().size() == 1, "spec count mismatch");
                   }});

  tests.push_back({"shell_tool_allowed_command", [] {
                     const auto ws = make_temp_dir();
                     auto policy = make_policy(ws);
                     tools::ShellTool shell(policy);

                     tools::ToolContext ctx;
                     ctx.workspace_path = ws;
                     auto result = shell.execute({{"command", "echo hello"}}, ctx);
                     require(result.ok(), result.error());
                     require(result.value().success, "echo should succeed");
                     require(result.value().output.find("hello") != std::string::npos,
                             "output should contain command result");
                   }});

  tests.push_back({"shell_tool_disallowed_command", [] {
                     const auto ws = make_temp_dir();
                     auto policy = make_policy(ws);
                     tools::ShellTool shell(policy);
                     tools::ToolContext ctx;
                     ctx.workspace_path = ws;

                     auto result = shell.execute({{"command", "rm -rf /tmp/nope"}}, ctx);
                     require(!result.ok(), "disallowed command should fail");
                   }});

  tests.push_back({"shell_tool_output_truncation", [] {
                     const auto ws = make_temp_dir();
                     auto policy = make_policy(ws);
                     tools::ShellTool shell(policy);
                     tools::ToolContext ctx;
                     ctx.workspace_path = ws;
                     auto result =
                         shell.execute({{"command", "python -c 'print(\"x\" * 1200000)'"}}, ctx);
                     require(result.ok(), result.error());
                     require(result.value().truncated, "large output should be truncated");
                   }});

  tests.push_back({"file_read_success", [] {
                     const auto ws = make_temp_dir();
                     auto policy = make_policy(ws);
                     const auto path = ws / "note.txt";
                     std::ofstream(path) << "hello file";

                     tools::FileReadTool tool(policy);
                     tools::ToolContext ctx;
                     ctx.workspace_path = ws;
                     auto result = tool.execute({{"path", "note.txt"}}, ctx);
                     require(result.ok(), result.error());
                     require(result.value().output == "hello file", "file content mismatch");
                   }});

  tests.push_back({"file_read_outside_workspace_rejected", [] {
                     const auto ws = make_temp_dir();
                     auto policy = make_policy(ws);
                     tools::FileReadTool tool(policy);

                     tools::ToolContext ctx;
                     ctx.workspace_path = ws;
                     auto result = tool.execute({{"path", "/etc/hosts"}}, ctx);
                     require(!result.ok(), "outside file should fail");
                   }});

  tests.push_back({"file_read_binary_rejected", [] {
                     const auto ws = make_temp_dir();
                     auto policy = make_policy(ws);
                     const auto path = ws / "blob.bin";
                     std::ofstream out(path, std::ios::binary);
                     out.write("abc\0def", 7);
                     out.close();

                     tools::FileReadTool tool(policy);
                     tools::ToolContext ctx;
                     ctx.workspace_path = ws;
                     auto result = tool.execute({{"path", "blob.bin"}}, ctx);
                     require(!result.ok(), "binary file should be rejected");
                   }});

  tests.push_back({"file_write_and_edit", [] {
                     const auto ws = make_temp_dir();
                     auto policy = make_policy(ws);
                     tools::ToolContext ctx;
                     ctx.workspace_path = ws;

                     tools::FileWriteTool writer(policy);
                     auto write_result =
                         writer.execute({{"path", "doc.txt"}, {"content", "hello world"}}, ctx);
                     require(write_result.ok(), write_result.error());
                     require(std::filesystem::exists(ws / "doc.txt"), "written file should exist");

                     tools::FileEditTool editor(policy);
                     auto edit_result = editor.execute(
                         {{"path", "doc.txt"}, {"old_string", "world"}, {"new_string", "ghostclaw"}},
                         ctx);
                     require(edit_result.ok(), edit_result.error());

                     std::ifstream in(ws / "doc.txt");
                     std::string content;
                     in >> content >> content;
                     require(content == "ghostclaw", "edited content mismatch");
                   }});

  tests.push_back({"file_write_readonly_rejected", [] {
                     const auto ws = make_temp_dir();
                     auto policy = make_policy(ws);
                     policy->autonomy = ghostclaw::security::AutonomyLevel::ReadOnly;
                     tools::FileWriteTool writer(policy);
                     tools::ToolContext ctx;
                     ctx.workspace_path = ws;
                     auto result = writer.execute({{"path", "a.txt"}, {"content", "x"}}, ctx);
                     require(!result.ok(), "readonly should reject writes");
                   }});

  tests.push_back({"file_edit_non_unique_rejected", [] {
                     const auto ws = make_temp_dir();
                     auto policy = make_policy(ws);
                     std::ofstream(ws / "dup.txt") << "same same";
                     tools::FileEditTool editor(policy);
                     tools::ToolContext ctx;
                     ctx.workspace_path = ws;
                     auto result = editor.execute(
                         {{"path", "dup.txt"}, {"old_string", "same"}, {"new_string", "one"}}, ctx);
                     require(!result.ok(), "non-unique replacement should fail");
                   }});

  tests.push_back({"memory_tools_store_recall_forget", [] {
                     FakeMemory memory;
                     tools::MemoryStoreTool store(&memory);
                     tools::MemoryRecallTool recall(&memory);
                     tools::MemoryForgetTool forget(&memory);
                     tools::ToolContext ctx;

                     auto s = store.execute({{"key", "k"}, {"content", "memory text"}}, ctx);
                     require(s.ok(), s.error());
                     auto r = recall.execute({{"query", "memory"}, {"limit", "5"}}, ctx);
                     require(r.ok(), r.error());
                     require(r.value().output.find("k") != std::string::npos, "recall output mismatch");
                     auto f = forget.execute({{"key", "k"}}, ctx);
                     require(f.ok(), f.error());
                     require(f.value().output.find("forgotten") != std::string::npos,
                             "forget output mismatch");
                   }});

  tests.push_back({"skills_tool_list_search_load", [] {
                     const auto ws = make_temp_dir();
                     std::error_code ec;
                     std::filesystem::create_directories(ws / "skills" / "alpha", ec);
                     std::ofstream(ws / "skills" / "alpha" / "SKILL.md")
                         << "---\nname: alpha\ndescription: Alpha skill\n---\nUse {baseDir}/references";

                     tools::SkillsTool tool;
                     tools::ToolContext ctx;
                     ctx.workspace_path = ws;

                     auto listed = tool.execute({{"action", "list"}}, ctx);
                     require(listed.ok(), listed.error());
                     require(listed.value().output.find("alpha") != std::string::npos,
                             "list output should include skill");

                     auto searched = tool.execute({{"action", "search"}, {"query", "alpha"}}, ctx);
                     require(searched.ok(), searched.error());
                     require(searched.value().output.find("alpha") != std::string::npos,
                             "search output should include skill");

                     auto loaded = tool.execute({{"action", "load"}, {"name", "alpha"}}, ctx);
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().output.find("BaseDir: ") != std::string::npos,
                             "load output should include base dir");
                     require(loaded.value().output.find(ws.string()) != std::string::npos,
                             "base dir token should resolve");
                   }});

  tests.push_back({"web_search_output_format", [] {
                     tools::WebSearchTool search;
                     tools::ToolContext ctx;
                     auto result = search.execute({{"query", "ghostclaw"}}, ctx);
                     // Without API keys, the tool uses DuckDuckGo instant answer API.
                     // It may or may not return results depending on network access.
                     // In CI without network, the call may fail entirely.
                     if (result.ok()) {
                       require(!result.value().output.empty(),
                               "web search should return non-empty output");
                       require(result.value().metadata.count("provider") > 0,
                               "provider metadata should be set");
                     }
                     // If it fails (no network), that's also acceptable in unit tests
                   }});

  tests.push_back({"web_fetch_missing_url_fails", [] {
                     tools::WebFetchTool fetch;
                     tools::ToolContext ctx;
                     auto result = fetch.execute({}, ctx);
                     require(!result.ok(), "missing url should fail");
                   }});

  tests.push_back({"browser_domain_allowlist", [] {
                     tools::BrowserTool browser({"example.com"});
                     tools::ToolContext ctx;
                     auto ok = browser.execute(
                         {{"action", "navigate"}, {"url", "https://docs.example.com/page"}}, ctx);
                     require(ok.ok(), ok.error());

                     auto blocked = browser.execute(
                         {{"action", "navigate"}, {"url", "https://evil.com"}}, ctx);
                     require(!blocked.ok(), "disallowed domain should fail");
                   }});

  tests.push_back({"canvas_host_push_eval_snapshot_reset", [] {
                     ghostclaw::canvas::CanvasHost host;

                     auto pushed = host.push("<main>hello</main>");
                     require(pushed.ok(), pushed.error());

                     auto eval = host.eval("appendHtml(\"<p>world</p>\")");
                     require(eval.ok(), eval.error());

                     auto snapshot = host.snapshot();
                     require(snapshot.ok(), snapshot.error());
                     require(snapshot.value().find("<main>hello</main>") != std::string::npos,
                             "snapshot should include pushed html");
                     require(snapshot.value().find("<p>world</p>") != std::string::npos,
                             "snapshot should include eval html changes");
                     require(snapshot.value().find("\"script_count\":1") != std::string::npos,
                             "snapshot should include script count");

                     auto reset = host.reset();
                     require(reset.ok(), reset.error());
                     auto after = host.snapshot();
                     require(after.ok(), after.error());
                     require(after.value().find("\"html\":\"\"") != std::string::npos,
                             "reset should clear html");
                   }});

  tests.push_back({"canvas_tool_push_eval_snapshot_reset", [] {
                     tools::CanvasTool canvas_tool;
                     tools::ToolContext ctx;
                     ctx.session_id = "session-tools-canvas";

                     auto pushed = canvas_tool.execute(
                         {{"action", "push"}, {"html", "<div id='root'>A</div>"}}, ctx);
                     require(pushed.ok(), pushed.error());
                     require(pushed.value().output.find("updated") != std::string::npos,
                             "push should confirm update");

                     auto eval =
                         canvas_tool.execute({{"action", "eval"}, {"js", "appendHtml(\"<span>B</span>\")"}}, ctx);
                     require(eval.ok(), eval.error());

                     auto snap = canvas_tool.execute({{"action", "snapshot"}}, ctx);
                     require(snap.ok(), snap.error());
                     require(snap.value().output.find("<div id='root'>A</div>") != std::string::npos,
                             "snapshot should include pushed html");
                     require(snap.value().output.find("<span>B</span>") != std::string::npos,
                             "snapshot should include eval changes");

                     auto reset = canvas_tool.execute({{"action", "reset"}}, ctx);
                     require(reset.ok(), reset.error());
                     auto after = canvas_tool.execute({{"action", "snapshot"}}, ctx);
                     require(after.ok(), after.error());
                     require(after.value().output.find("\"html\":\"\"") != std::string::npos,
                             "reset should clear canvas html");
                   }});

  tests.push_back({"canvas_tool_react_component_push", [] {
                     tools::CanvasTool canvas_tool;
                     tools::ToolContext ctx;
                     ctx.session_id = "session-tools-canvas-react";

                     auto pushed = canvas_tool.execute(
                         {{"action", "push"},
                          {"component", "React.createElement('h1', null, 'GhostClaw')"},
                          {"props", "{\"mode\":\"demo\"}"}},
                         ctx);
                     require(pushed.ok(), pushed.error());

                     auto snap = canvas_tool.execute({{"action", "snapshot"}}, ctx);
                     require(snap.ok(), snap.error());
                     require(snap.value().output.find("ReactDOM.createRoot") != std::string::npos,
                             "react push should create React scaffold");
                   }});

  tests.push_back({"tool_registry_create_full_registers_canvas", [] {
                     const auto ws = make_temp_dir();
                     auto policy = make_policy(ws);
                     ghostclaw::config::Config config;
                     tools::ToolRegistry registry =
                         tools::ToolRegistry::create_full(policy, nullptr, config);
                     require(registry.get_tool("canvas") != nullptr,
                             "create_full should register canvas tool");
                   }});

  tests.push_back({"tool_executor_parallel_execution", [] {
                     tools::ToolRegistry registry;
                     registry.register_tool(std::make_unique<SleepTool>("slow_a", 250));
                     registry.register_tool(std::make_unique<SleepTool>("slow_b", 250));
                     agent::ToolExecutor executor(registry);
                     tools::ToolContext ctx;

                     const auto start = std::chrono::steady_clock::now();
                     auto results = executor.execute(
                         {
                             {.id = "1", .name = "slow_a", .arguments = {}},
                             {.id = "2", .name = "slow_b", .arguments = {}},
                         },
                         ctx);
                     const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start);
                     require(results.size() == 2, "parallel results size mismatch");
                     require(elapsed.count() < 450, "tools should run in parallel");
                   }});

  tests.push_back({"tool_executor_circuit_breaker", [] {
                     tools::ToolRegistry registry;
                     registry.register_tool(std::make_unique<AlwaysFailTool>());
                     agent::ToolExecutor executor(registry);
                     tools::ToolContext ctx;

                     for (int i = 0; i < 3; ++i) {
                       auto r = executor.execute({{.id = std::to_string(i), .name = "always_fail", .arguments = {}}}, ctx);
                       require(!r[0].result.success, "failure expected");
                     }

                     auto cool = executor.execute({{.id = "x", .name = "always_fail", .arguments = {}}}, ctx);
                     require(!cool[0].result.success, "cooldown execution should fail");
                     require(cool[0].result.output.find("cooldown") != std::string::npos,
                             "cooldown message expected");
                   }});

  tests.push_back({"tool_executor_security_policy_blocks", [] {
                     tools::ToolRegistry registry;
                     registry.register_tool(std::make_unique<DummySafeTool>());

                     auto pipeline = std::make_shared<ghostclaw::security::ToolPolicyPipeline>();
                     pipeline->set_global_policy(
                         ghostclaw::security::ToolPolicy{.allow = {}, .deny = {"dummy_safe"}});

                     agent::ToolExecutor::Dependencies deps;
                     deps.tool_policy = pipeline;
                     agent::ToolExecutor executor(registry, deps);

                     tools::ToolContext ctx;
                     ctx.tool_profile = "full";

                     auto result =
                         executor.execute({{.id = "1", .name = "dummy_safe", .arguments = {}}}, ctx);
                     require(result.size() == 1, "single result expected");
                     require(!result[0].result.success, "policy should block tool");
                     require(result[0].result.output.find("blocked by policy") != std::string::npos,
                             "policy block message expected");
                   }});

  tests.push_back({"tool_executor_security_approval_denies", [] {
                     tools::ToolRegistry registry;
                     registry.register_tool(std::make_unique<SafeShellTool>());

                     ghostclaw::security::ApprovalPolicy policy;
                     policy.security = ghostclaw::security::ExecSecurity::Deny;
                     policy.ask = ghostclaw::security::ExecAsk::Off;

                     const auto temp = make_temp_dir();
                     auto approval = std::make_shared<ghostclaw::security::ApprovalManager>(
                         policy, temp / "approvals.txt", temp / "approvals.sock");

                     agent::ToolExecutor::Dependencies deps;
                     deps.approval = approval;
                     agent::ToolExecutor executor(registry, deps);

                     tools::ToolContext ctx;
                     auto result = executor.execute(
                         {{.id = "1", .name = "shell", .arguments = {{"command", "echo hello"}}}}, ctx);
                     require(result.size() == 1, "single result expected");
                     require(!result[0].result.success, "approval should deny shell execution");
                     require(result[0].result.output.find("denied") != std::string::npos,
                             "approval deny message expected");
                   }});

  tests.push_back({"approval_manager_smart_mode", [] {
                     tools::ApprovalManager approval(tools::ApprovalMode::Smart);
                     DummySafeTool safe;
                     require(!approval.needs_approval(safe, {}), "safe tool should not need approval");

                     AlwaysFailTool unsafe;
                     require(approval.needs_approval(unsafe, {}), "unsafe tool should need approval");
                   }});

  tests.push_back({"approval_manager_dangerous_shell", [] {
                     tools::ApprovalManager approval(tools::ApprovalMode::Smart);
                     SafeShellTool shell;
                     require(approval.needs_approval(shell, {{"command", "rm -rf /"}}),
                             "dangerous shell command should need approval");
                   }});

  tests.push_back({"calendar_tool_create_requires_confirm", [] {
                     ghostclaw::config::Config config;
                     config.calendar.backend = "gog";
                     tools::CalendarTool tool(config);
                     tools::ToolContext ctx;
                     auto result = tool.execute({{"action", "create_event"},
                                                 {"title", "Meeting"},
                                                 {"start", "2026-02-16T14:00:00Z"},
                                                 {"end", "2026-02-16T14:30:00Z"}},
                                                ctx);
                     require(result.ok(), result.error());
                     require(result.value().metadata.contains("requires_confirmation"),
                             "create_event should require confirm");
                   }});

  tests.push_back({"email_tool_send_requires_confirm", [] {
                     ghostclaw::config::Config config;
                     config.email.backend = "gog";
                     tools::EmailTool tool(config);
                     tools::ToolContext ctx;
                     auto result = tool.execute(
                         {{"action", "send"},
                          {"to", "test@example.com"},
                          {"subject", "Hello"},
                          {"body", "World"}},
                         ctx);
                     require(result.ok(), result.error());
                     require(result.value().metadata.contains("requires_confirmation"),
                             "send should require confirm");
                   }});

  tests.push_back({"message_tool_send_requires_confirm", [] {
                     ghostclaw::config::Config config;
                     tools::MessageTool tool(config);
                     tools::ToolContext ctx;
                     auto result = tool.execute(
                         {{"action", "send"},
                          {"channel", "cli"},
                          {"to", "someone"},
                          {"text", "hello"}},
                         ctx);
                     require(result.ok(), result.error());
                     require(result.value().metadata.contains("requires_confirmation"),
                             "message send should require confirm");
                   }});

  tests.push_back({"reminder_tool_schedule_list_cancel", [] {
                     const auto ws = make_temp_dir();
                     ghostclaw::config::Config config;
                     config.reminders.default_channel = "cli";
                     tools::ReminderTool tool(config);
                     tools::ToolContext ctx;
                     ctx.workspace_path = ws;

                     auto scheduled = tool.execute(
                         {{"action", "schedule"},
                          {"id", "reminder-test"},
                          {"expression", "* * * * *"},
                          {"channel", "cli"},
                          {"to", "user"},
                          {"text", "ping"},
                          {"confirm", "true"}},
                         ctx);
                     require(scheduled.ok(), scheduled.error());

                     auto listed = tool.execute({{"action", "list"}}, ctx);
                     require(listed.ok(), listed.error());
                     require(listed.value().output.find("reminder-test") != std::string::npos,
                             "list should include scheduled reminder");

                     auto cancelled =
                         tool.execute({{"action", "cancel"}, {"id", "reminder-test"}, {"confirm", "true"}}, ctx);
                     require(cancelled.ok(), cancelled.error());
                   }});

  tests.push_back({"plugin_loader_empty_dir", [] {
                     const auto ws = make_temp_dir();
                     tools::plugin::PluginLoader loader(ws / "plugins");
                     auto loaded = loader.load_all();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().empty(), "empty plugin dir should yield zero plugins");
                   }});
}
