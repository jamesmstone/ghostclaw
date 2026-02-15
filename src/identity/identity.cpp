#include "ghostclaw/identity/identity.hpp"

#include "ghostclaw/common/fs.hpp"

namespace ghostclaw::identity {

IdentityFormat parse_identity_format(const std::string_view str) {
  const std::string lower = common::to_lower(common::trim(std::string(str)));
  if (lower == "aieos") {
    return IdentityFormat::Aieos;
  }
  return IdentityFormat::OpenClaw;
}

std::string_view format_to_string(const IdentityFormat format) {
  switch (format) {
  case IdentityFormat::OpenClaw:
    return "openclaw";
  case IdentityFormat::Aieos:
    return "aieos";
  }
  return "openclaw";
}

} // namespace ghostclaw::identity
