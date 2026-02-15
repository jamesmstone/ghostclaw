#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace ghostclaw::config {

[[nodiscard]] common::Result<std::filesystem::path> config_dir();
[[nodiscard]] common::Result<std::filesystem::path> config_path();
[[nodiscard]] common::Result<std::filesystem::path> workspace_dir();
[[nodiscard]] bool config_exists();
void set_config_path_override(std::optional<std::filesystem::path> path);
void clear_config_path_override();
[[nodiscard]] std::optional<std::filesystem::path> config_path_override();

[[nodiscard]] std::string expand_config_path(const std::string &path);

[[nodiscard]] common::Result<Config> load_config();
[[nodiscard]] common::Status save_config(const Config &config);

[[nodiscard]] common::Result<std::vector<std::string>> validate_config(const Config &config);

void apply_env_overrides(Config &config);

} // namespace ghostclaw::config
