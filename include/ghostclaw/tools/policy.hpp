#pragma once

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ghostclaw::tools {

class ToolPolicy {
public:
  ToolPolicy(const std::vector<std::string> &allow_groups, const std::vector<std::string> &allow_tools,
             const std::vector<std::string> &deny_tools);

  [[nodiscard]] bool is_allowed(std::string_view tool_name) const;
  [[nodiscard]] static std::vector<std::string> expand_group(std::string_view group);

private:
  std::unordered_set<std::string> allowed_;
  std::unordered_set<std::string> denied_;
};

} // namespace ghostclaw::tools
