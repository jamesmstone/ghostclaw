#include "ghostclaw/tools/builtin/memory_recall.hpp"

#include <sstream>

namespace ghostclaw::tools {

MemoryRecallTool::MemoryRecallTool(memory::IMemory *memory) : memory_(memory) {}

std::string_view MemoryRecallTool::name() const { return "memory_recall"; }

std::string_view MemoryRecallTool::description() const { return "Recall memories by semantic query"; }

std::string MemoryRecallTool::parameters_schema() const {
  return R"({"type":"object","required":["query"],"properties":{"query":{"type":"string"},"limit":{"type":"string"}}})";
}

common::Result<ToolResult> MemoryRecallTool::execute(const ToolArgs &args, const ToolContext &) {
  if (memory_ == nullptr) {
    return common::Result<ToolResult>::failure("memory backend unavailable");
  }

  const auto query_it = args.find("query");
  if (query_it == args.end()) {
    return common::Result<ToolResult>::failure("Missing query");
  }

  std::size_t limit = 5;
  if (const auto limit_it = args.find("limit"); limit_it != args.end()) {
    try {
      limit = static_cast<std::size_t>(std::stoull(limit_it->second));
    } catch (...) {
    }
  }

  auto recalled = memory_->recall(query_it->second, limit);
  if (!recalled.ok()) {
    return common::Result<ToolResult>::failure(recalled.error());
  }

  std::ostringstream out;
  for (const auto &entry : recalled.value()) {
    out << "- " << entry.key << ": " << entry.content;
    if (entry.score.has_value()) {
      out << " (" << *entry.score << ")";
    }
    out << "\n";
  }

  ToolResult result;
  result.output = out.str();
  return common::Result<ToolResult>::success(std::move(result));
}

bool MemoryRecallTool::is_safe() const { return true; }

std::string_view MemoryRecallTool::group() const { return "memory"; }

} // namespace ghostclaw::tools
