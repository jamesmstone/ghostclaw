#include "ghostclaw/tools/builtin/file_write.hpp"

#include <filesystem>
#include <fstream>

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

FileWriteTool::FileWriteTool(std::shared_ptr<security::SecurityPolicy> policy)
    : policy_(std::move(policy)) {}

std::string_view FileWriteTool::name() const { return "file_write"; }

std::string_view FileWriteTool::description() const { return "Write content to a file atomically"; }

std::string FileWriteTool::parameters_schema() const {
  return R"({"type":"object","required":["path","content"],"properties":{"path":{"type":"string"},"content":{"type":"string"}}})";
}

common::Result<ToolResult> FileWriteTool::execute(const ToolArgs &args, const ToolContext &ctx) {
  if (!policy_) {
    return common::Result<ToolResult>::failure("security policy unavailable");
  }

  if (policy_->autonomy == security::AutonomyLevel::ReadOnly) {
    return common::Result<ToolResult>::failure("ReadOnly autonomy does not permit writes");
  }

  auto path_arg = required_arg(args, "path");
  if (!path_arg.ok()) {
    return common::Result<ToolResult>::failure(path_arg.error());
  }
  auto content_arg = required_arg(args, "content");
  if (!content_arg.ok()) {
    return common::Result<ToolResult>::failure(content_arg.error());
  }

  const auto effective_policy = scoped_policy(*policy_, ctx);
  auto validated = security::validate_path(path_arg.value(), effective_policy);
  if (!validated.ok()) {
    return common::Result<ToolResult>::failure(validated.error());
  }

  std::error_code ec;
  std::filesystem::create_directories(validated.value().parent_path(), ec);
  if (ec) {
    return common::Result<ToolResult>::failure("Failed to create parent directory");
  }

  const auto temp_path = validated.value().string() + ".tmp";
  std::ofstream out(temp_path, std::ios::trunc);
  if (!out) {
    return common::Result<ToolResult>::failure("Failed to open temporary file");
  }
  out << content_arg.value();
  out.close();

  std::filesystem::rename(temp_path, validated.value(), ec);
  if (ec) {
    return common::Result<ToolResult>::failure("Failed to atomically replace file");
  }

  policy_->record_action();

  ToolResult result;
  result.output = "File written: " + validated.value().string();
  return common::Result<ToolResult>::success(std::move(result));
}

bool FileWriteTool::is_safe() const { return false; }

std::string_view FileWriteTool::group() const { return "fs"; }

} // namespace ghostclaw::tools
