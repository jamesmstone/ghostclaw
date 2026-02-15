#pragma once

#include "ghostclaw/tools/tool.hpp"

#include <string>

namespace ghostclaw::tools {

struct WebSearchConfig {
  std::string provider = "auto"; // "brave", "duckduckgo", or "auto"
  std::string brave_api_key;     // Brave Search API key (optional)
};

class WebSearchTool final : public ITool {
public:
  explicit WebSearchTool(WebSearchConfig config = {});

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] std::string_view description() const override;
  [[nodiscard]] std::string parameters_schema() const override;
  [[nodiscard]] common::Result<ToolResult> execute(const ToolArgs &args,
                                                   const ToolContext &ctx) override;

  [[nodiscard]] bool is_safe() const override;
  [[nodiscard]] std::string_view group() const override;

private:
  [[nodiscard]] common::Result<ToolResult> search_brave(const std::string &query);
  [[nodiscard]] common::Result<ToolResult> search_duckduckgo(const std::string &query);

  WebSearchConfig config_;
};

} // namespace ghostclaw::tools
