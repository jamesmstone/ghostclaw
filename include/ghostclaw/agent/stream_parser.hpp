#pragma once

#include "ghostclaw/tools/tool.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ghostclaw::agent {

struct ParsedToolCall {
  std::string id;
  std::string name;
  tools::ToolArgs arguments;
};

class StreamParser {
public:
  using ToolCallCallback = std::function<void(const ParsedToolCall &)>;

  explicit StreamParser(ToolCallCallback on_tool_call = nullptr);

  void feed(std::string_view chunk);
  void finish();

  [[nodiscard]] std::string accumulated_content() const;
  [[nodiscard]] std::vector<ParsedToolCall> tool_calls() const;

private:
  void parse_buffer();
  [[nodiscard]] static tools::ToolArgs parse_args_json(const std::string &json);

  ToolCallCallback on_tool_call_;
  std::string buffer_;
  std::string content_;
  std::vector<ParsedToolCall> tool_calls_;
  std::unordered_set<std::string> seen_call_signatures_;
};

} // namespace ghostclaw::agent
