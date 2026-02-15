#pragma once

#include "ghostclaw/tools/tool.hpp"

namespace ghostclaw::tools {

enum class ApprovalMode { Always, Never, Smart };

class ApprovalManager {
public:
  explicit ApprovalManager(ApprovalMode mode);

  [[nodiscard]] bool needs_approval(const ITool &tool, const ToolArgs &args) const;
  [[nodiscard]] bool request_approval(const ITool &tool, const ToolArgs &args) const;

private:
  ApprovalMode mode_;
};

} // namespace ghostclaw::tools
