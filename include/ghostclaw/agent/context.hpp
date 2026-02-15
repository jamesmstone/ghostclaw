#pragma once

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/tools/tool.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ghostclaw::agent {

class ContextBuilder {
public:
  explicit ContextBuilder(std::filesystem::path workspace,
                          config::IdentityConfig identity_config = {});

  [[nodiscard]] std::string build_system_prompt(const std::vector<tools::ToolSpec> &tools,
                                                const std::vector<std::string> &skills);

private:
  [[nodiscard]] std::string read_workspace_file(const std::string &filename,
                                                std::size_t max_size = 20 * 1024) const;
  [[nodiscard]] std::string format_tools(const std::vector<tools::ToolSpec> &tools) const;
  [[nodiscard]] std::string format_skills(const std::vector<std::string> &skills) const;
  [[nodiscard]] std::string safety_guardrails() const;
  [[nodiscard]] std::string runtime_metadata() const;

  std::filesystem::path workspace_;
  config::IdentityConfig identity_config_;
};

} // namespace ghostclaw::agent
