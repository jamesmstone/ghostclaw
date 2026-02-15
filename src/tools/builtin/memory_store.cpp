#include "ghostclaw/tools/builtin/memory_store.hpp"

namespace ghostclaw::tools {

MemoryStoreTool::MemoryStoreTool(memory::IMemory *memory) : memory_(memory) {}

std::string_view MemoryStoreTool::name() const { return "memory_store"; }

std::string_view MemoryStoreTool::description() const { return "Store a new memory entry"; }

std::string MemoryStoreTool::parameters_schema() const {
  return R"({"type":"object","required":["key","content"],"properties":{"key":{"type":"string"},"content":{"type":"string"},"category":{"type":"string"}}})";
}

common::Result<ToolResult> MemoryStoreTool::execute(const ToolArgs &args, const ToolContext &) {
  if (memory_ == nullptr) {
    return common::Result<ToolResult>::failure("memory backend unavailable");
  }

  const auto key_it = args.find("key");
  const auto content_it = args.find("content");
  if (key_it == args.end() || content_it == args.end()) {
    return common::Result<ToolResult>::failure("Missing key/content");
  }

  const auto category_it = args.find("category");
  const auto category = category_it == args.end() ? memory::MemoryCategory::Core
                                                   : memory::category_from_string(category_it->second);

  auto status = memory_->store(key_it->second, content_it->second, category);
  if (!status.ok()) {
    return common::Result<ToolResult>::failure(status.error());
  }

  ToolResult result;
  result.output = "Stored memory: " + key_it->second;
  return common::Result<ToolResult>::success(std::move(result));
}

bool MemoryStoreTool::is_safe() const { return false; }

std::string_view MemoryStoreTool::group() const { return "memory"; }

} // namespace ghostclaw::tools
