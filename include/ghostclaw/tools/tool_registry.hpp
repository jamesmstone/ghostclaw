#pragma once

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/security/policy.hpp"
#include "ghostclaw/tools/tool.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace ghostclaw::tools {

class ToolRegistry {
public:
  ToolRegistry() = default;

  void register_tool(std::unique_ptr<ITool> tool);
  [[nodiscard]] ITool *get_tool(std::string_view name) const;
  [[nodiscard]] std::vector<ToolSpec> all_specs() const;
  [[nodiscard]] std::vector<ITool *> all_tools() const;

  [[nodiscard]] static ToolRegistry create_default(std::shared_ptr<security::SecurityPolicy> policy);
  [[nodiscard]] static ToolRegistry create_full(std::shared_ptr<security::SecurityPolicy> policy,
                                                memory::IMemory *memory,
                                                const config::Config &config);

private:
  std::vector<std::unique_ptr<ITool>> tools_;
  std::unordered_map<std::string, ITool *> by_name_;
};

} // namespace ghostclaw::tools
