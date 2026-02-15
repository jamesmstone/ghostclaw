#pragma once

#include <string_view>
#include <vector>

namespace ghostclaw::channels {

[[nodiscard]] bool check_allowlist(std::string_view sender,
                                   const std::vector<std::string> &allowlist);

} // namespace ghostclaw::channels
