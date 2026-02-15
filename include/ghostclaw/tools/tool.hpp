#pragma once

#include "ghostclaw/common/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ghostclaw::tools {

using ToolArgs = std::unordered_map<std::string, std::string>;

struct ToolResult {
  std::string output;
  bool success = true;
  bool truncated = false;
  std::unordered_map<std::string, std::string> metadata;
};

struct ToolSpec {
  std::string name;
  std::string description;
  std::string parameters_json;
  bool safe = false;
  std::string group;
};

struct ToolContext {
  std::filesystem::path workspace_path;
  std::string session_id;
  std::string agent_id;
  std::string main_session_id = "main";
  std::string provider;
  std::string tool_profile = "full";
  std::string channel_id;
  std::string group_id;
  bool sandbox_enabled = true;
};

class ITool {
public:
  virtual ~ITool() = default;

  [[nodiscard]] virtual std::string_view name() const = 0;
  [[nodiscard]] virtual std::string_view description() const = 0;
  [[nodiscard]] virtual std::string parameters_schema() const = 0;
  [[nodiscard]] virtual common::Result<ToolResult> execute(const ToolArgs &args,
                                                           const ToolContext &ctx) = 0;

  [[nodiscard]] virtual bool is_safe() const = 0;
  [[nodiscard]] virtual std::uint32_t timeout_ms() const { return 60'000; }
  [[nodiscard]] virtual std::string_view group() const = 0;

  [[nodiscard]] ToolSpec spec() const;
};

} // namespace ghostclaw::tools
