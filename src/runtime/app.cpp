#include "ghostclaw/runtime/app.hpp"

#include "ghostclaw/config/config.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/observability/factory.hpp"
#include "ghostclaw/observability/global.hpp"
#include "ghostclaw/providers/factory.hpp"
#include "ghostclaw/security/policy.hpp"
#include "ghostclaw/tools/tool_registry.hpp"

namespace ghostclaw::runtime {

RuntimeContext::RuntimeContext(config::Config config) : config_(std::move(config)) {}

common::Result<RuntimeContext> RuntimeContext::from_disk() {
  auto loaded = config::load_config();
  if (!loaded.ok()) {
    return common::Result<RuntimeContext>::failure(loaded.error());
  }
  return common::Result<RuntimeContext>::success(RuntimeContext(std::move(loaded.value())));
}

const config::Config &RuntimeContext::config() const { return config_; }

config::Config &RuntimeContext::mutable_config() { return config_; }

common::Result<std::shared_ptr<agent::AgentEngine>> RuntimeContext::create_agent_engine() {
  observability::set_global_observer(observability::create_observer(config_));

  auto workspace = config::workspace_dir();
  if (!workspace.ok()) {
    return common::Result<std::shared_ptr<agent::AgentEngine>>::failure(workspace.error());
  }

  auto provider = providers::create_reliable_provider(
      config_.default_provider, config_.api_key, config_.reliability);
  if (!provider.ok()) {
    return common::Result<std::shared_ptr<agent::AgentEngine>>::failure(provider.error());
  }

  auto memory = memory::create_memory(config_, workspace.value());
  if (memory == nullptr) {
    return common::Result<std::shared_ptr<agent::AgentEngine>>::failure(
        "failed to create memory backend");
  }

  auto policy = security::SecurityPolicy::from_config(config_);
  if (!policy.ok()) {
    return common::Result<std::shared_ptr<agent::AgentEngine>>::failure(policy.error());
  }
  auto policy_ptr = std::make_shared<security::SecurityPolicy>(std::move(policy.value()));

  auto registry = tools::ToolRegistry::create_full(policy_ptr, memory.get(), config_);

  auto engine = std::make_shared<agent::AgentEngine>(
      config_, provider.value(), std::move(memory), std::move(registry), workspace.value());

  return common::Result<std::shared_ptr<agent::AgentEngine>>::success(std::move(engine));
}

} // namespace ghostclaw::runtime
