#pragma once

#include "ghostclaw/security/policy.hpp"
#include "ghostclaw/tools/tool.hpp"

#include <memory>

namespace ghostclaw::tools {

class FileWriteTool final : public ITool {
public:
  explicit FileWriteTool(std::shared_ptr<security::SecurityPolicy> policy);

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] std::string_view description() const override;
  [[nodiscard]] std::string parameters_schema() const override;
  [[nodiscard]] common::Result<ToolResult> execute(const ToolArgs &args,
                                                   const ToolContext &ctx) override;

  [[nodiscard]] bool is_safe() const override;
  [[nodiscard]] std::string_view group() const override;

private:
  std::shared_ptr<security::SecurityPolicy> policy_;
};

} // namespace ghostclaw::tools
