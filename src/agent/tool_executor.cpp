#include "ghostclaw/agent/tool_executor.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/sandbox/sandbox.hpp"
#include "ghostclaw/security/approval.hpp"
#include "ghostclaw/security/tool_policy.hpp"

#include <future>

namespace ghostclaw::agent {

namespace {

bool is_dangerous_tool(const tools::ITool &tool) {
  const std::string group = common::to_lower(std::string(tool.group()));
  const std::string name = common::to_lower(std::string(tool.name()));
  if (!tool.is_safe()) {
    return true;
  }
  return group == "runtime" || name == "shell" || name == "exec" || name == "process";
}

std::string approval_command_for_call(const ToolCallRequest &call, const tools::ITool &tool) {
  const auto command_it = call.arguments.find("command");
  if (command_it != call.arguments.end() && !common::trim(command_it->second).empty()) {
    return command_it->second;
  }
  return std::string(tool.name());
}

} // namespace

ToolExecutor::ToolExecutor(tools::ToolRegistry &registry, Dependencies dependencies)
    : registry_(registry), dependencies_(std::move(dependencies)) {}

void ToolExecutor::set_tool_policy_pipeline(std::shared_ptr<security::ToolPolicyPipeline> tool_policy) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  dependencies_.tool_policy = std::move(tool_policy);
}

void ToolExecutor::set_sandbox_manager(std::shared_ptr<sandbox::SandboxManager> sandbox) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  dependencies_.sandbox = std::move(sandbox);
}

void ToolExecutor::set_approval_manager(std::shared_ptr<security::ApprovalManager> approval) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  dependencies_.approval = std::move(approval);
}

std::vector<ToolCallResult> ToolExecutor::execute(const std::vector<ToolCallRequest> &calls,
                                                  const tools::ToolContext &ctx) {
  std::vector<std::future<ToolCallResult>> futures;
  futures.reserve(calls.size());

  const auto now = std::chrono::steady_clock::now();

  for (const auto &call : calls) {
    futures.push_back(std::async(std::launch::async, [this, call, ctx, now]() {
      ToolCallResult out;
      out.id = call.id;
      out.name = call.name;

      Dependencies deps;
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        deps = dependencies_;
      }

      if (deps.tool_policy) {
        security::ToolPolicyRequest policy_request;
        policy_request.tool_name = call.name;
        policy_request.provider = ctx.provider;
        policy_request.agent_id = ctx.agent_id;
        policy_request.channel_id = ctx.channel_id;
        policy_request.group_id = ctx.group_id;
        const auto profile = security::ToolPolicyPipeline::profile_from_string(ctx.tool_profile);
        if (profile.ok()) {
          policy_request.profile = profile.value();
        }

        const auto decision = deps.tool_policy->evaluate_tool(policy_request);
        if (!decision.allowed) {
          out.result.success = false;
          out.result.output =
              "Tool blocked by policy (" + decision.blocked_by + "): " + decision.reason;
          return out;
        }
      }

      tools::ITool *tool = registry_.get_tool(call.name);
      if (tool == nullptr) {
        out.result.success = false;
        out.result.output = "Unknown tool: " + call.name;
        return out;
      }

      if (ctx.sandbox_enabled && deps.sandbox) {
        sandbox::SandboxRequest request;
        request.session_id = ctx.session_id;
        request.main_session_id = ctx.main_session_id;
        request.agent_id = ctx.agent_id;
        request.workspace_dir = ctx.workspace_path;
        request.agent_workspace_dir = ctx.workspace_path;

        auto runtime = deps.sandbox->resolve_runtime(request);
        if (!runtime.ok()) {
          out.result.success = false;
          out.result.output = "Sandbox resolve failed: " + runtime.error();
          return out;
        }

        if (runtime.value().enabled) {
          if (!deps.sandbox->is_tool_allowed(call.name)) {
            out.result.success = false;
            out.result.output = "Tool blocked by sandbox policy: " + call.name;
            return out;
          }

          auto ensured = deps.sandbox->ensure_runtime(request);
          if (!ensured.ok()) {
            out.result.success = false;
            out.result.output = "Sandbox setup failed: " + ensured.error();
            return out;
          }
        }
      }

      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        const auto it = cooldowns_.find(call.name);
        if (it != cooldowns_.end() && now < it->second) {
          out.result.success = false;
          out.result.output = "Tool in cooldown: " + call.name;
          return out;
        }
      }

      if (deps.approval && is_dangerous_tool(*tool)) {
        security::ApprovalRequest request;
        request.command = approval_command_for_call(call, *tool);
        request.session_id = ctx.session_id;
        request.timeout = std::chrono::seconds(120);

        auto decision = deps.approval->authorize(request);
        if (!decision.ok()) {
          out.result.success = false;
          out.result.output = "Approval check failed: " + decision.error();
          return out;
        }

        if (decision.value() == security::ApprovalDecision::Deny) {
          out.result.success = false;
          out.result.output = "Tool execution denied by approval policy";
          return out;
        }
      }

      auto result = tool->execute(call.arguments, ctx);
      if (result.ok()) {
        out.result = result.value();
        std::lock_guard<std::mutex> lock(state_mutex_);
        failure_counts_[call.name] = 0;
      } else {
        out.result.success = false;
        out.result.output = result.error();
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto &count = failure_counts_[call.name];
        ++count;
        if (count >= 3U) {
          cooldowns_[call.name] = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        }
      }

      return out;
    }));
  }

  std::vector<ToolCallResult> results;
  results.reserve(calls.size());
  for (auto &future : futures) {
    results.push_back(future.get());
  }
  return results;
}

} // namespace ghostclaw::agent
