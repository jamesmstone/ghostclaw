#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ghostclaw::onboard {

struct WizardResult {
  bool success = false;
  bool launch_agent = false;
  std::string error;
};

struct WizardOptions {
  bool interactive = false;
  bool channels_only = false;
  bool offer_launch = false;
  std::optional<std::string> api_key;
  std::optional<std::string> provider;
  std::optional<std::string> model;
  std::optional<std::string> memory_backend;
};

struct MenuEntry {
  std::string label;
  std::string value;
};

struct MenuGroup {
  std::string heading;
  std::vector<MenuEntry> entries;
};

/// Display a numbered menu and return the selected value.
[[nodiscard]] std::string prompt_menu(const std::string &title,
                                      const std::vector<MenuGroup> &groups,
                                      const std::string &default_value = "");

/// Simple text prompt with a default fallback.
[[nodiscard]] std::string prompt_value(const std::string &label,
                                       const std::string &fallback);

/// Yes/no prompt. Returns true for "yes", false for "no".
[[nodiscard]] bool prompt_yes_no(const std::string &label, bool default_yes);

/// Run the full onboarding wizard.
[[nodiscard]] WizardResult run_wizard(const WizardOptions &options);

} // namespace ghostclaw::onboard
