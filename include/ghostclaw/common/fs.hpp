#pragma once

#include "ghostclaw/common/result.hpp"
#include <filesystem>
#include <string>

namespace ghostclaw::common {

[[nodiscard]] std::string trim(const std::string &input);
[[nodiscard]] bool starts_with(const std::string &value, const std::string &prefix);
[[nodiscard]] std::string to_lower(std::string value);
[[nodiscard]] Result<std::filesystem::path> home_dir();
[[nodiscard]] Result<std::filesystem::path> ensure_dir(const std::filesystem::path &path);
[[nodiscard]] std::string expand_path(std::string value);
[[nodiscard]] bool is_subpath(const std::filesystem::path &candidate,
                             const std::filesystem::path &parent);

} // namespace ghostclaw::common
