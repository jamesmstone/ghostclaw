#pragma once

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/tools/tool.hpp"

namespace ghostclaw::tools {

class EmailTool final : public ITool {
public:
  explicit EmailTool(config::Config config);

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] std::string_view description() const override;
  [[nodiscard]] std::string parameters_schema() const override;
  [[nodiscard]] common::Result<ToolResult> execute(const ToolArgs &args,
                                                   const ToolContext &ctx) override;

  [[nodiscard]] bool is_safe() const override;
  [[nodiscard]] std::string_view group() const override;

private:
  config::Config config_;
};

} // namespace ghostclaw::tools
