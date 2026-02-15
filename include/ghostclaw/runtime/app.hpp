#pragma once

#include "ghostclaw/agent/engine.hpp"
#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"

#include <memory>

namespace ghostclaw::runtime {

class RuntimeContext {
public:
  explicit RuntimeContext(config::Config config);

  [[nodiscard]] static common::Result<RuntimeContext> from_disk();

  [[nodiscard]] const config::Config &config() const;
  [[nodiscard]] config::Config &mutable_config();

  [[nodiscard]] common::Result<std::shared_ptr<agent::AgentEngine>>
  create_agent_engine();

private:
  config::Config config_;
};

} // namespace ghostclaw::runtime
