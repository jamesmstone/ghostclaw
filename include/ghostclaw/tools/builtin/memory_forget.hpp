#pragma once

#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/tools/tool.hpp"

namespace ghostclaw::tools {

class MemoryForgetTool final : public ITool {
public:
  explicit MemoryForgetTool(memory::IMemory *memory);

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] std::string_view description() const override;
  [[nodiscard]] std::string parameters_schema() const override;
  [[nodiscard]] common::Result<ToolResult> execute(const ToolArgs &args,
                                                   const ToolContext &ctx) override;

  [[nodiscard]] bool is_safe() const override;
  [[nodiscard]] std::string_view group() const override;

private:
  memory::IMemory *memory_;
};

} // namespace ghostclaw::tools
