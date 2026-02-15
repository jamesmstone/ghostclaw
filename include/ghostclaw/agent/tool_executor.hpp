#pragma once

#include "ghostclaw/tools/tool_registry.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ghostclaw::security {
class ToolPolicyPipeline;
class ApprovalManager;
} // namespace security

namespace ghostclaw::sandbox {
class SandboxManager;
} // namespace sandbox

namespace ghostclaw::agent {

struct ToolCallRequest {
  std::string id;
  std::string name;
  tools::ToolArgs arguments;
};

struct ToolCallResult {
  std::string id;
  std::string name;
  tools::ToolResult result;
};

class ToolExecutor {
public:
  struct Dependencies {
    std::shared_ptr<security::ToolPolicyPipeline> tool_policy;
    std::shared_ptr<sandbox::SandboxManager> sandbox;
    std::shared_ptr<security::ApprovalManager> approval;
  };

  explicit ToolExecutor(tools::ToolRegistry &registry, Dependencies dependencies = {});

  void set_tool_policy_pipeline(std::shared_ptr<security::ToolPolicyPipeline> tool_policy);
  void set_sandbox_manager(std::shared_ptr<sandbox::SandboxManager> sandbox);
  void set_approval_manager(std::shared_ptr<security::ApprovalManager> approval);

  [[nodiscard]] std::vector<ToolCallResult> execute(const std::vector<ToolCallRequest> &calls,
                                                    const tools::ToolContext &ctx);

private:
  tools::ToolRegistry &registry_;
  std::mutex state_mutex_;
  std::unordered_map<std::string, std::size_t> failure_counts_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> cooldowns_;
  Dependencies dependencies_;
};

} // namespace ghostclaw::agent
