#include "ghostclaw/tools/builtin/file_read.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ghostclaw::tools {

namespace {

common::Result<std::string> required_arg(const ToolArgs &args, const std::string &name) {
  const auto it = args.find(name);
  if (it == args.end() || it->second.empty()) {
    return common::Result<std::string>::failure("Missing argument: " + name);
  }
  return common::Result<std::string>::success(it->second);
}

bool is_binary_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }

  std::array<char, 8192> buffer{};
  in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  const auto count = in.gcount();
  for (std::streamsize i = 0; i < count; ++i) {
    if (buffer[static_cast<std::size_t>(i)] == '\0') {
      return true;
    }
  }
  return false;
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

FileReadTool::FileReadTool(std::shared_ptr<security::SecurityPolicy> policy) : policy_(std::move(policy)) {}

std::string_view FileReadTool::name() const { return "file_read"; }

std::string_view FileReadTool::description() const { return "Read a UTF-8 text file"; }

std::string FileReadTool::parameters_schema() const {
  return R"({"type":"object","required":["path"],"properties":{"path":{"type":"string"}}})";
}

common::Result<ToolResult> FileReadTool::execute(const ToolArgs &args, const ToolContext &ctx) {
  if (!policy_) {
    return common::Result<ToolResult>::failure("security policy unavailable");
  }

  auto path_arg = required_arg(args, "path");
  if (!path_arg.ok()) {
    return common::Result<ToolResult>::failure(path_arg.error());
  }

  const auto effective_policy = scoped_policy(*policy_, ctx);
  auto validated = security::validate_path(path_arg.value(), effective_policy);
  if (!validated.ok()) {
    return common::Result<ToolResult>::failure(validated.error());
  }

  if (is_binary_file(validated.value())) {
    return common::Result<ToolResult>::failure("Binary file read is not allowed");
  }

  std::ifstream in(validated.value());
  if (!in) {
    return common::Result<ToolResult>::failure("Failed to open file");
  }

  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string content = buffer.str();

  ToolResult result;
  if (content.size() > 20 * 1024) {
    content.resize(20 * 1024);
    result.truncated = true;
  }
  result.output = std::move(content);
  return common::Result<ToolResult>::success(std::move(result));
}

bool FileReadTool::is_safe() const { return true; }

std::string_view FileReadTool::group() const { return "fs"; }

} // namespace ghostclaw::tools
