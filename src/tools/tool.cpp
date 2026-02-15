#include "ghostclaw/tools/tool.hpp"

namespace ghostclaw::tools {

ToolSpec ITool::spec() const {
  return ToolSpec{.name = std::string(name()),
                  .description = std::string(description()),
                  .parameters_json = parameters_schema(),
                  .safe = is_safe(),
                  .group = std::string(group())};
}

} // namespace ghostclaw::tools
