#include "ghostclaw/sandbox/sandbox.hpp"

#include "ghostclaw/common/fs.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace ghostclaw::sandbox {

namespace {

std::string normalize_key(std::string_view value) {
  return common::to_lower(common::trim(std::string(value)));
}

std::string short_hash_hex(std::string_view value) {
  const std::size_t hash = std::hash<std::string>{}(std::string(value));
  std::ostringstream out;
  out << std::hex << std::setw(8) << std::setfill('0') << static_cast<std::uint32_t>(hash & 0xffffffffU);
  return out.str();
}

std::string slugify(std::string_view input) {
  const std::string normalized = normalize_key(input);
  if (normalized.empty()) {
    return "session";
  }

  std::string out;
  out.reserve(normalized.size());
  for (char ch : normalized) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-') {
      out.push_back(ch);
    } else {
      out.push_back('-');
    }
  }

  while (!out.empty() && out.front() == '-') {
    out.erase(out.begin());
  }
  while (!out.empty() && out.back() == '-') {
    out.pop_back();
  }
  if (out.empty()) {
    out = "session";
  }
  if (out.size() > 36) {
    out.resize(36);
  }
  return out;
}

std::string workspace_mount_arg(const std::filesystem::path &host, const std::string &container,
                                const SandboxConfig::WorkspaceAccess access) {
  std::string mount = host.string() + ":" + container;
  if (access == SandboxConfig::WorkspaceAccess::ReadOnly) {
    mount += ":ro";
  }
  return mount;
}

} // namespace

std::string sandbox_mode_to_string(const SandboxConfig::Mode mode) {
  switch (mode) {
  case SandboxConfig::Mode::Off:
    return "off";
  case SandboxConfig::Mode::NonMain:
    return "non-main";
  case SandboxConfig::Mode::All:
    return "all";
  }
  return "off";
}

std::string sandbox_scope_to_string(const SandboxConfig::Scope scope) {
  switch (scope) {
  case SandboxConfig::Scope::Session:
    return "session";
  case SandboxConfig::Scope::Agent:
    return "agent";
  case SandboxConfig::Scope::Shared:
    return "shared";
  }
  return "session";
}

std::string workspace_access_to_string(const SandboxConfig::WorkspaceAccess access) {
  switch (access) {
  case SandboxConfig::WorkspaceAccess::None:
    return "none";
  case SandboxConfig::WorkspaceAccess::ReadOnly:
    return "ro";
  case SandboxConfig::WorkspaceAccess::ReadWrite:
    return "rw";
  }
  return "ro";
}

std::string resolve_sandbox_scope_key(const SandboxConfig &config, const SandboxRequest &request) {
  const std::string session_id = normalize_key(request.session_id);
  const std::string agent_id = normalize_key(request.agent_id);

  switch (config.scope) {
  case SandboxConfig::Scope::Shared:
    return "shared";
  case SandboxConfig::Scope::Agent:
    return "agent:" + (agent_id.empty() ? std::string("main") : agent_id);
  case SandboxConfig::Scope::Session:
    return session_id.empty() ? std::string("session:main") : session_id;
  }
  return session_id;
}

std::string resolve_sandbox_container_name(const SandboxConfig &config,
                                           const SandboxRequest &request) {
  const std::string scope_key = resolve_sandbox_scope_key(config, request);
  const std::string slug = slugify(scope_key);
  std::string name = config.container_prefix + slug + "-" + short_hash_hex(scope_key);
  if (name.size() > 63) {
    name.resize(63);
  }
  return name;
}

std::vector<std::string> build_docker_create_args(const SandboxConfig &config,
                                                   const SandboxRuntime &runtime,
                                                   const SandboxRequest &request) {
  std::vector<std::string> args = {"create", "--name", runtime.container_name};
  args.push_back("--label");
  args.push_back("ghostclaw.sandbox=1");
  args.push_back("--label");
  args.push_back("ghostclaw.scope=" + runtime.scope_key);

  if (config.read_only_root) {
    args.push_back("--read-only");
  }
  for (const auto &entry : config.tmpfs) {
    if (common::trim(entry).empty()) {
      continue;
    }
    args.push_back("--tmpfs");
    args.push_back(entry);
  }

  if (!common::trim(config.network_mode).empty()) {
    args.push_back("--network");
    args.push_back(config.network_mode);
  }

  for (const auto &cap : config.cap_drop) {
    if (common::trim(cap).empty()) {
      continue;
    }
    args.push_back("--cap-drop");
    args.push_back(cap);
  }
  args.push_back("--security-opt");
  args.push_back("no-new-privileges");

  for (const auto &pair : config.env) {
    if (common::trim(pair.first).empty()) {
      continue;
    }
    args.push_back("--env");
    args.push_back(pair.first + "=" + pair.second);
  }

  for (const auto &entry : config.dns) {
    if (common::trim(entry).empty()) {
      continue;
    }
    args.push_back("--dns");
    args.push_back(entry);
  }
  for (const auto &entry : config.extra_hosts) {
    if (common::trim(entry).empty()) {
      continue;
    }
    args.push_back("--add-host");
    args.push_back(entry);
  }

  if (config.pids_limit.has_value() && *config.pids_limit > 0) {
    args.push_back("--pids-limit");
    args.push_back(std::to_string(*config.pids_limit));
  }
  if (config.memory_limit.has_value() && !config.memory_limit->empty()) {
    args.push_back("--memory");
    args.push_back(*config.memory_limit);
  }
  if (config.memory_swap_limit.has_value() && !config.memory_swap_limit->empty()) {
    args.push_back("--memory-swap");
    args.push_back(*config.memory_swap_limit);
  }
  if (config.cpu_limit.has_value() && *config.cpu_limit > 0) {
    args.push_back("--cpus");
    std::ostringstream cpu;
    cpu << std::fixed << std::setprecision(2) << *config.cpu_limit;
    args.push_back(cpu.str());
  }

  for (const auto &bind : config.binds) {
    if (common::trim(bind).empty()) {
      continue;
    }
    args.push_back("-v");
    args.push_back(bind);
  }

  if (config.workspace_access == SandboxConfig::WorkspaceAccess::None) {
    args.push_back("--tmpfs");
    args.push_back(config.workdir);
  } else {
    const auto mounted = runtime.mounted_workspace_dir.empty() ? request.workspace_dir
                                                                : runtime.mounted_workspace_dir;
    args.push_back("-v");
    args.push_back(workspace_mount_arg(mounted, config.workdir, config.workspace_access));

    if (!request.agent_workspace_dir.empty() && request.agent_workspace_dir != mounted) {
      args.push_back("-v");
      args.push_back(workspace_mount_arg(request.agent_workspace_dir, "/agent",
                                         config.workspace_access));
    }
  }

  args.push_back("--workdir");
  args.push_back(config.workdir);
  args.push_back(config.image);
  args.push_back("sleep");
  args.push_back("infinity");
  return args;
}

SandboxManager::SandboxManager(SandboxConfig config, std::shared_ptr<IDockerRunner> docker_runner)
    : config_(std::move(config)), docker_runner_(std::move(docker_runner)) {}

const SandboxConfig &SandboxManager::config() const { return config_; }

void SandboxManager::set_config(SandboxConfig config) { config_ = std::move(config); }

bool SandboxManager::should_sandbox(const SandboxRequest &request) const {
  if (config_.mode == SandboxConfig::Mode::Off) {
    return false;
  }

  if (config_.mode == SandboxConfig::Mode::All) {
    return true;
  }

  const std::string session = normalize_key(request.session_id);
  const std::string main_session = normalize_key(request.main_session_id);
  if (session.empty()) {
    return false;
  }
  if (main_session.empty()) {
    return true;
  }
  return session != main_session;
}

bool SandboxManager::is_tool_allowed(std::string_view tool_name) const {
  security::ToolPolicyPipeline pipeline;
  pipeline.set_global_policy(
      security::ToolPolicy{.allow = config_.tool_allow, .deny = config_.tool_deny});

  security::ToolPolicyRequest request;
  request.tool_name = std::string(tool_name);
  request.profile = security::ToolProfile::Full;

  return pipeline.evaluate_tool(request).allowed;
}

common::Result<SandboxRuntime> SandboxManager::resolve_runtime(const SandboxRequest &request) const {
  SandboxRuntime runtime;
  runtime.enabled = should_sandbox(request);
  runtime.scope_key = resolve_sandbox_scope_key(config_, request);
  runtime.container_name = resolve_sandbox_container_name(config_, request);
  runtime.container_workdir = config_.workdir;

  if (config_.workspace_access == SandboxConfig::WorkspaceAccess::ReadWrite) {
    runtime.mounted_workspace_dir = request.agent_workspace_dir.empty() ? request.workspace_dir
                                                                         : request.agent_workspace_dir;
  } else {
    const auto base = request.workspace_dir.empty() ? request.agent_workspace_dir : request.workspace_dir;
    runtime.mounted_workspace_dir = base / ".ghostclaw-sandbox" / slugify(runtime.scope_key);
  }

  return common::Result<SandboxRuntime>::success(std::move(runtime));
}

common::Result<SandboxRuntime> SandboxManager::ensure_runtime(const SandboxRequest &request) {
  auto runtime_result = resolve_runtime(request);
  if (!runtime_result.ok()) {
    return runtime_result;
  }

  SandboxRuntime runtime = runtime_result.value();
  if (!runtime.enabled) {
    return common::Result<SandboxRuntime>::success(std::move(runtime));
  }

  if (config_.workspace_access != SandboxConfig::WorkspaceAccess::None) {
    std::error_code ec;
    std::filesystem::create_directories(runtime.mounted_workspace_dir, ec);
    if (ec) {
      return common::Result<SandboxRuntime>::failure("failed to create sandbox workspace: " +
                                                     ec.message());
    }
  }

  if (!docker_runner_) {
    return common::Result<SandboxRuntime>::failure("docker runner unavailable");
  }

  auto state = inspect_container_state(runtime.container_name);
  if (!state.ok()) {
    return common::Result<SandboxRuntime>::failure(state.error());
  }

  if (!state.value().exists) {
    const auto args = build_docker_create_args(config_, runtime, request);
    auto create = docker_runner_->run(args);
    if (!create.ok()) {
      return common::Result<SandboxRuntime>::failure(create.error());
    }

    auto start = docker_runner_->run({"start", runtime.container_name});
    if (!start.ok()) {
      return common::Result<SandboxRuntime>::failure(start.error());
    }
  } else if (!state.value().running) {
    auto start = docker_runner_->run({"start", runtime.container_name});
    if (!start.ok()) {
      return common::Result<SandboxRuntime>::failure(start.error());
    }
  }

  return common::Result<SandboxRuntime>::success(std::move(runtime));
}

common::Status SandboxManager::stop_runtime(const SandboxRequest &request) {
  auto runtime = resolve_runtime(request);
  if (!runtime.ok()) {
    return common::Status::error(runtime.error());
  }
  if (!runtime.value().enabled || !docker_runner_) {
    return common::Status::success();
  }

  auto stopped =
      docker_runner_->run({"stop", runtime.value().container_name}, DockerCommandOptions{.allow_failure = true});
  if (!stopped.ok()) {
    return common::Status::error(stopped.error());
  }
  return common::Status::success();
}

common::Status SandboxManager::remove_runtime(const SandboxRequest &request) {
  auto runtime = resolve_runtime(request);
  if (!runtime.ok()) {
    return common::Status::error(runtime.error());
  }
  if (!runtime.value().enabled || !docker_runner_) {
    return common::Status::success();
  }

  auto removed = docker_runner_->run({"rm", "-f", runtime.value().container_name},
                                     DockerCommandOptions{.allow_failure = true});
  if (!removed.ok()) {
    return common::Status::error(removed.error());
  }
  return common::Status::success();
}

common::Result<SandboxManager::ContainerState>
SandboxManager::inspect_container_state(const std::string &container_name) {
  if (!docker_runner_) {
    return common::Result<ContainerState>::failure("docker runner unavailable");
  }

  auto inspect = docker_runner_->run({"inspect", "-f", "{{.State.Running}}", container_name},
                                     DockerCommandOptions{.allow_failure = true});
  if (!inspect.ok()) {
    return common::Result<ContainerState>::failure(inspect.error());
  }

  if (inspect.value().exit_code != 0) {
    return common::Result<ContainerState>::success(ContainerState{.exists = false, .running = false});
  }

  const auto state = normalize_key(inspect.value().stdout_text);
  const bool running = state.find("true") != std::string::npos;
  return common::Result<ContainerState>::success(ContainerState{.exists = true, .running = running});
}

} // namespace ghostclaw::sandbox
