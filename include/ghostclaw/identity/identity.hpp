#pragma once

#include "ghostclaw/common/fs.hpp"

#include <string>
#include <string_view>

namespace ghostclaw::identity {

enum class IdentityFormat {
  OpenClaw,
  Aieos,
};

[[nodiscard]] IdentityFormat parse_identity_format(std::string_view str);
[[nodiscard]] std::string_view format_to_string(IdentityFormat format);

struct Identity {
  std::string name;
  std::string personality;
  std::string directives;
  std::string user_context;
  std::string raw_system_prompt;
};

} // namespace ghostclaw::identity
