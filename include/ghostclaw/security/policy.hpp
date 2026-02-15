#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/security/action_tracker.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace ghostclaw::security {

enum class AutonomyLevel { ReadOnly, Supervised, Full };

[[nodiscard]] common::Result<AutonomyLevel> autonomy_level_from_string(const std::string &value);

extern const std::array<const char *, 10> SYSTEM_FORBIDDEN_PATHS;

class SecurityPolicy {
public:
  AutonomyLevel autonomy = AutonomyLevel::Supervised;
  std::filesystem::path workspace_dir;
  bool workspace_only = true;
  std::vector<std::string> allowed_commands;
  std::vector<std::string> forbidden_paths;
  std::uint32_t max_actions_per_hour = 100;
  std::uint32_t max_cost_per_day_cents = 1000;
  ActionTracker tracker{100};

  SecurityPolicy();

  [[nodiscard]] static common::Result<SecurityPolicy> from_config(const config::Config &config);

  [[nodiscard]] bool is_command_allowed(const std::string &cmd) const;
  [[nodiscard]] bool is_path_allowed(const std::filesystem::path &path) const;
  [[nodiscard]] bool check_rate_limit();
  void record_action();
};

[[nodiscard]] common::Result<std::filesystem::path> validate_path(const std::string &path,
                                                                  const SecurityPolicy &policy);

} // namespace ghostclaw::security
