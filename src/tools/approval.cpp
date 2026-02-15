#include "ghostclaw/tools/approval.hpp"

#include "ghostclaw/common/fs.hpp"

namespace ghostclaw::tools {

ApprovalManager::ApprovalManager(const ApprovalMode mode) : mode_(mode) {}

bool ApprovalManager::needs_approval(const ITool &tool, const ToolArgs &args) const {
  if (mode_ == ApprovalMode::Never) {
    return false;
  }
  if (mode_ == ApprovalMode::Always) {
    return true;
  }

  if (!tool.is_safe()) {
    return true;
  }

  if (tool.name() == "shell") {
    const auto it = args.find("command");
    if (it != args.end()) {
      const std::string cmd = common::to_lower(it->second);
      if (cmd.find("rm -rf") != std::string::npos || cmd.find("chmod 777") != std::string::npos ||
          cmd.find("mkfs") != std::string::npos) {
        return true;
      }
    }
  }

  return false;
}

bool ApprovalManager::request_approval(const ITool &tool, const ToolArgs &args) const {
  // Non-interactive default: only grant if no approval needed.
  return !needs_approval(tool, args);
}

} // namespace ghostclaw::tools
