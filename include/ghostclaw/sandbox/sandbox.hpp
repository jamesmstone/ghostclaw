#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/sandbox/docker.hpp"
#include "ghostclaw/security/tool_policy.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ghostclaw::sandbox {

struct SandboxConfig {
  enum class Mode { Off, NonMain, All };
  enum class Scope { Session, Agent, Shared };
  enum class WorkspaceAccess { None, ReadOnly, ReadWrite };

  Mode mode = Mode::Off;
  Scope scope = Scope::Session;
  WorkspaceAccess workspace_access = WorkspaceAccess::ReadOnly;

  std::string image = "ghostclaw-sandbox:bookworm-slim";
  std::string container_prefix = "ghostclaw-sbx-";
  std::string workdir = "/workspace";
  bool read_only_root = true;
  std::vector<std::string> tmpfs = {"/tmp", "/var/tmp", "/run"};
  std::string network_mode = "none";
  std::vector<std::string> cap_drop = {"ALL"};
  std::vector<std::string> dns;
  std::vector<std::string> extra_hosts;
  std::vector<std::string> binds;
  std::vector<std::string> tool_allow = {
      "group:fs",
      "group:runtime",
      "group:sessions",
      "group:web",
  };
  std::vector<std::string> tool_deny = {
      "group:ui",
      "group:automation",
      "group:messaging",
  };
  std::vector<std::pair<std::string, std::string>> env = {{"LANG", "C.UTF-8"}};

  std::optional<std::uint32_t> pids_limit;
  std::optional<std::string> memory_limit;
  std::optional<std::string> memory_swap_limit;
  std::optional<double> cpu_limit;
};

struct SandboxRequest {
  std::string session_id;
  std::string agent_id;
  std::string main_session_id = "main";
  std::filesystem::path workspace_dir;
  std::filesystem::path agent_workspace_dir;
};

struct SandboxRuntime {
  bool enabled = false;
  std::string scope_key;
  std::string container_name;
  std::filesystem::path mounted_workspace_dir;
  std::string container_workdir;
};

[[nodiscard]] std::string sandbox_mode_to_string(SandboxConfig::Mode mode);
[[nodiscard]] std::string sandbox_scope_to_string(SandboxConfig::Scope scope);
[[nodiscard]] std::string workspace_access_to_string(SandboxConfig::WorkspaceAccess access);

[[nodiscard]] std::string resolve_sandbox_scope_key(const SandboxConfig &config,
                                                    const SandboxRequest &request);
[[nodiscard]] std::string resolve_sandbox_container_name(const SandboxConfig &config,
                                                         const SandboxRequest &request);

[[nodiscard]] std::vector<std::string>
build_docker_create_args(const SandboxConfig &config, const SandboxRuntime &runtime,
                         const SandboxRequest &request);

class SandboxManager {
public:
  explicit SandboxManager(SandboxConfig config,
                          std::shared_ptr<IDockerRunner> docker_runner = std::make_shared<DockerCliRunner>());

  [[nodiscard]] const SandboxConfig &config() const;
  void set_config(SandboxConfig config);

  [[nodiscard]] bool should_sandbox(const SandboxRequest &request) const;
  [[nodiscard]] bool is_tool_allowed(std::string_view tool_name) const;

  [[nodiscard]] common::Result<SandboxRuntime> resolve_runtime(const SandboxRequest &request) const;
  [[nodiscard]] common::Result<SandboxRuntime> ensure_runtime(const SandboxRequest &request);
  [[nodiscard]] common::Status stop_runtime(const SandboxRequest &request);
  [[nodiscard]] common::Status remove_runtime(const SandboxRequest &request);

private:
  struct ContainerState {
    bool exists = false;
    bool running = false;
  };

  [[nodiscard]] common::Result<ContainerState>
  inspect_container_state(const std::string &container_name);

  SandboxConfig config_;
  std::shared_ptr<IDockerRunner> docker_runner_;
};

} // namespace ghostclaw::sandbox
