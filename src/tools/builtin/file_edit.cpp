#include "ghostclaw/tools/builtin/file_edit.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace ghostclaw::tools {

namespace {

common::Result<std::string> required_arg(const ToolArgs &args, const std::string &name) {
  const auto it = args.find(name);
  if (it == args.end()) {
    return common::Result<std::string>::failure("Missing argument: " + name);
  }
  return common::Result<std::string>::success(it->second);
}

security::SecurityPolicy scoped_policy(const security::SecurityPolicy &policy,
                                       const tools::ToolContext &ctx) {
  security::SecurityPolicy copy = policy;
  if (!ctx.workspace_path.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(ctx.workspace_path, ec);
    auto canonical = std::filesystem::weakly_canonical(ctx.workspace_path, ec);
    copy.workspace_dir = ec ? ctx.workspace_path : canonical;
  }
  return copy;
}

} // namespace

FileEditTool::FileEditTool(std::shared_ptr<security::SecurityPolicy> policy) : policy_(std::move(policy)) {}

std::string_view FileEditTool::name() const { return "file_edit"; }

std::string_view FileEditTool::description() const {
  return "Replace a unique substring in a text file";
}

std::string FileEditTool::parameters_schema() const {
  return R"({"type":"object","required":["path","old_string","new_string"],"properties":{"path":{"type":"string"},"old_string":{"type":"string"},"new_string":{"type":"string"}}})";
}

common::Result<ToolResult> FileEditTool::execute(const ToolArgs &args, const ToolContext &ctx) {
  if (!policy_) {
    return common::Result<ToolResult>::failure("security policy unavailable");
  }

  if (policy_->autonomy == security::AutonomyLevel::ReadOnly) {
    return common::Result<ToolResult>::failure("ReadOnly autonomy does not permit edits");
  }

  auto path_arg = required_arg(args, "path");
  auto old_arg = required_arg(args, "old_string");
  auto new_arg = required_arg(args, "new_string");
  if (!path_arg.ok() || !old_arg.ok() || !new_arg.ok()) {
    return common::Result<ToolResult>::failure("Missing required arguments");
  }

  const auto effective_policy = scoped_policy(*policy_, ctx);
  auto validated = security::validate_path(path_arg.value(), effective_policy);
  if (!validated.ok()) {
    return common::Result<ToolResult>::failure(validated.error());
  }

  std::ifstream in(validated.value());
  if (!in) {
    return common::Result<ToolResult>::failure("Failed to read target file");
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string content = buffer.str();

  const auto first = content.find(old_arg.value());
  if (first == std::string::npos) {
    return common::Result<ToolResult>::failure("old_string not found");
  }

  const auto second = content.find(old_arg.value(), first + old_arg.value().size());
  if (second != std::string::npos) {
    return common::Result<ToolResult>::failure("old_string must be unique");
  }

  content.replace(first, old_arg.value().size(), new_arg.value());

  const auto temp_path = validated.value().string() + ".tmp";
  std::ofstream out(temp_path, std::ios::trunc);
  if (!out) {
    return common::Result<ToolResult>::failure("Failed to write temporary file");
  }
  out << content;
  out.close();

  std::error_code ec;
  std::filesystem::rename(temp_path, validated.value(), ec);
  if (ec) {
    return common::Result<ToolResult>::failure("Failed to replace file");
  }

  policy_->record_action();

  ToolResult result;
  result.output = "File edited: " + validated.value().string();
  return common::Result<ToolResult>::success(std::move(result));
}

bool FileEditTool::is_safe() const { return false; }

std::string_view FileEditTool::group() const { return "fs"; }

} // namespace ghostclaw::tools
