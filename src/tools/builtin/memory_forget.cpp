#include "ghostclaw/tools/builtin/memory_forget.hpp"

namespace ghostclaw::tools {

MemoryForgetTool::MemoryForgetTool(memory::IMemory *memory) : memory_(memory) {}

std::string_view MemoryForgetTool::name() const { return "memory_forget"; }

std::string_view MemoryForgetTool::description() const { return "Delete a memory entry by key"; }

std::string MemoryForgetTool::parameters_schema() const {
  return R"({"type":"object","required":["key"],"properties":{"key":{"type":"string"}}})";
}

common::Result<ToolResult> MemoryForgetTool::execute(const ToolArgs &args, const ToolContext &) {
  if (memory_ == nullptr) {
    return common::Result<ToolResult>::failure("memory backend unavailable");
  }

  const auto key_it = args.find("key");
  if (key_it == args.end()) {
    return common::Result<ToolResult>::failure("Missing key");
  }

  auto forgotten = memory_->forget(key_it->second);
  if (!forgotten.ok()) {
    return common::Result<ToolResult>::failure(forgotten.error());
  }

  ToolResult result;
  result.output = forgotten.value() ? "Memory forgotten" : "Memory not found";
  return common::Result<ToolResult>::success(std::move(result));
}

bool MemoryForgetTool::is_safe() const { return false; }

std::string_view MemoryForgetTool::group() const { return "memory"; }

} // namespace ghostclaw::tools
