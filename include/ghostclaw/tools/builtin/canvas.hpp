#pragma once

#include "ghostclaw/canvas/host.hpp"
#include "ghostclaw/tools/tool.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ghostclaw::tools {

class CanvasTool final : public ITool {
public:
  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] std::string_view description() const override;
  [[nodiscard]] std::string parameters_schema() const override;
  [[nodiscard]] common::Result<ToolResult> execute(const ToolArgs &args,
                                                   const ToolContext &ctx) override;

  [[nodiscard]] bool is_safe() const override;
  [[nodiscard]] std::string_view group() const override;

private:
  canvas::CanvasHost &host_for_session(const ToolContext &ctx);

  std::mutex hosts_mutex_;
  std::unordered_map<std::string, std::unique_ptr<canvas::CanvasHost>> hosts_;
};

} // namespace ghostclaw::tools
