#pragma once

#include "ghostclaw/common/result.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ghostclaw::browser {

enum class BrowserKind {
  Chrome,
  Chromium,
  Brave,
  Edge,
  Unknown,
};

struct BrowserInstallation {
  BrowserKind kind = BrowserKind::Unknown;
  std::string id;
  std::string display_name;
  std::filesystem::path executable;
  bool available = false;
};

struct BrowserProfile {
  std::string profile_id;
  std::string session_name;
  BrowserKind browser_kind = BrowserKind::Unknown;
  std::filesystem::path browser_executable;
  std::filesystem::path user_data_dir;
  std::uint16_t devtools_port = 0;
  std::string color_hex;
};

class BrowserProfileManager {
public:
  explicit BrowserProfileManager(
      std::filesystem::path root_dir = {},
      std::optional<std::vector<BrowserInstallation>> injected_installations = std::nullopt);

  [[nodiscard]] common::Result<std::vector<BrowserInstallation>> detect_browsers() const;
  [[nodiscard]] common::Result<BrowserProfile>
  acquire_profile(const std::string &session_name,
                  const std::optional<std::string> &preferred_browser_id = std::nullopt);
  [[nodiscard]] common::Status release_profile(const std::string &profile_id);
  [[nodiscard]] std::vector<BrowserProfile> list_active_profiles() const;

private:
  [[nodiscard]] static std::string normalize_id(std::string value);
  [[nodiscard]] static std::string sanitize_path_component(const std::string &value);
  [[nodiscard]] static std::string pick_color_hex(const std::string &session_name);
  [[nodiscard]] common::Result<std::uint16_t> find_available_port() const;
  [[nodiscard]] common::Result<BrowserInstallation>
  select_browser(const std::vector<BrowserInstallation> &detected,
                 const std::optional<std::string> &preferred_browser_id) const;

  std::filesystem::path root_dir_;
  std::optional<std::vector<BrowserInstallation>> injected_installations_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, BrowserProfile> active_profiles_;
};

[[nodiscard]] std::string browser_kind_to_string(BrowserKind kind);

} // namespace ghostclaw::browser
