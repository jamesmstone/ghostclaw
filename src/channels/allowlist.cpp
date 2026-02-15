#include "ghostclaw/channels/allowlist.hpp"

#include "ghostclaw/common/fs.hpp"

namespace ghostclaw::channels {

bool check_allowlist(std::string_view sender, const std::vector<std::string> &allowlist) {
  if (allowlist.empty()) {
    return false;
  }

  const std::string normalized_sender = common::to_lower(std::string(sender));
  for (const auto &entry : allowlist) {
    if (entry == "*") {
      return true;
    }
    if (common::to_lower(entry) == normalized_sender) {
      return true;
    }
  }

  return false;
}

} // namespace ghostclaw::channels
