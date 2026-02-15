#include "ghostclaw/security/policy.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/config/config.hpp"

#include <algorithm>

namespace ghostclaw::security {

const std::array<const char *, 10> SYSTEM_FORBIDDEN_PATHS = {
    "/etc", "/var", "/root", "/proc", "/sys",
    "/dev", "~/.ssh", "~/.gnupg", "~/.aws", "~/.config/ghostclaw/secrets.key"};

namespace {

std::string base_command(const std::string &command) {
  const std::string trimmed = common::trim(command);
  if (trimmed.empty()) {
    return "";
  }

  const auto space = trimmed.find(' ');
  std::string first = space == std::string::npos ? trimmed : trimmed.substr(0, space);

  const auto slash = first.find_last_of('/');
  if (slash != std::string::npos) {
    first = first.substr(slash + 1);
  }

  return first;
}

bool path_matches_forbidden(const std::filesystem::path &candidate,
                            const std::vector<std::string> &forbidden_paths) {
  for (const auto &entry : forbidden_paths) {
    const auto expanded = std::filesystem::path(common::expand_path(entry));
    std::error_code ec;
    auto canonical_forbidden = std::filesystem::weakly_canonical(expanded, ec);
    if (ec) {
      canonical_forbidden = expanded;
    }

    if (common::is_subpath(candidate, canonical_forbidden)) {
      return true;
    }
  }
  return false;
}

} // namespace

common::Result<AutonomyLevel> autonomy_level_from_string(const std::string &value) {
  const std::string normalized = common::to_lower(common::trim(value));
  if (normalized == "readonly") {
    return common::Result<AutonomyLevel>::success(AutonomyLevel::ReadOnly);
  }
  if (normalized == "supervised") {
    return common::Result<AutonomyLevel>::success(AutonomyLevel::Supervised);
  }
  if (normalized == "full") {
    return common::Result<AutonomyLevel>::success(AutonomyLevel::Full);
  }
  return common::Result<AutonomyLevel>::failure("Invalid autonomy level: " + value);
}

SecurityPolicy::SecurityPolicy() {
  if (auto workspace = config::workspace_dir(); workspace.ok()) {
    workspace_dir = workspace.value();
  }
}

common::Result<SecurityPolicy> SecurityPolicy::from_config(const config::Config &config) {
  SecurityPolicy policy;

  const auto level_result = autonomy_level_from_string(config.autonomy.level);
  if (!level_result.ok()) {
    return common::Result<SecurityPolicy>::failure(level_result.error());
  }

  policy.autonomy = level_result.value();
  policy.workspace_only = config.autonomy.workspace_only;
  policy.allowed_commands = config.autonomy.allowed_commands;
  policy.forbidden_paths = config.autonomy.forbidden_paths;
  for (const char *system_path : SYSTEM_FORBIDDEN_PATHS) {
    const auto found = std::find(policy.forbidden_paths.begin(), policy.forbidden_paths.end(),
                                 std::string(system_path));
    if (found == policy.forbidden_paths.end()) {
      policy.forbidden_paths.emplace_back(system_path);
    }
  }
  policy.max_actions_per_hour = config.autonomy.max_actions_per_hour;
  policy.max_cost_per_day_cents = config.autonomy.max_cost_per_day_cents;
  policy.tracker = ActionTracker(config.autonomy.max_actions_per_hour);

  const auto ws = config::workspace_dir();
  if (!ws.ok()) {
    return common::Result<SecurityPolicy>::failure(ws.error());
  }
  policy.workspace_dir = ws.value();

  return common::Result<SecurityPolicy>::success(std::move(policy));
}

bool SecurityPolicy::is_command_allowed(const std::string &cmd) const {
  if (common::trim(cmd).empty()) {
    return false;
  }

  const std::string base = base_command(cmd);
  if (base.empty()) {
    return false;
  }

  return std::find(allowed_commands.begin(), allowed_commands.end(), base) !=
         allowed_commands.end();
}

bool SecurityPolicy::is_path_allowed(const std::filesystem::path &path) const {
  if (path.string().find('\0') != std::string::npos) {
    return false;
  }

  const auto validated = validate_path(path.string(), *this);
  return validated.ok();
}

bool SecurityPolicy::check_rate_limit() { return tracker.check(); }

void SecurityPolicy::record_action() { tracker.record(); }

common::Result<std::filesystem::path> validate_path(const std::string &path,
                                                    const SecurityPolicy &policy) {
  if (path.find('\0') != std::string::npos) {
    return common::Result<std::filesystem::path>::failure("Path contains null byte");
  }

  std::filesystem::path expanded(common::expand_path(path));
  if (expanded.empty()) {
    expanded = ".";
  }

  std::filesystem::path candidate = expanded;
  if (expanded.is_relative()) {
    candidate = policy.workspace_dir / expanded;
  }

  std::error_code ec;
  const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, ec);
  if (ec) {
    return common::Result<std::filesystem::path>::failure("Path canonicalization failed: " +
                                                           ec.message());
  }

  const auto canonical_workspace = std::filesystem::weakly_canonical(policy.workspace_dir, ec);
  if (ec) {
    return common::Result<std::filesystem::path>::failure("Workspace canonicalization failed: " +
                                                           ec.message());
  }

  if (policy.workspace_only && !common::is_subpath(canonical_candidate, canonical_workspace)) {
    return common::Result<std::filesystem::path>::failure("Path escapes workspace");
  }

  const bool inside_workspace = common::is_subpath(canonical_candidate, canonical_workspace);
  if (!inside_workspace && path_matches_forbidden(canonical_candidate, policy.forbidden_paths)) {
    return common::Result<std::filesystem::path>::failure("Path is forbidden by policy");
  }

  if (!inside_workspace && policy.workspace_only) {
    return common::Result<std::filesystem::path>::failure("Symlink escape detected");
  }

  return common::Result<std::filesystem::path>::success(canonical_candidate);
}

} // namespace ghostclaw::security
