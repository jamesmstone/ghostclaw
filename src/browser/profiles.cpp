#include "ghostclaw/browser/profiles.hpp"

#include "ghostclaw/common/fs.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <sstream>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ghostclaw::browser {

namespace {

constexpr std::uint16_t kMinPort = 18800;
constexpr std::uint16_t kMaxPort = 18899;

std::vector<BrowserInstallation> browser_candidates() {
  std::vector<BrowserInstallation> candidates;
#ifdef __APPLE__
  candidates.push_back(
      {.kind = BrowserKind::Chrome,
       .id = "chrome",
       .display_name = "Google Chrome",
       .executable = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"});
  candidates.push_back(
      {.kind = BrowserKind::Brave,
       .id = "brave",
       .display_name = "Brave",
       .executable = "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser"});
  candidates.push_back({.kind = BrowserKind::Edge,
                        .id = "edge",
                        .display_name = "Microsoft Edge",
                        .executable = "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"});
  candidates.push_back({.kind = BrowserKind::Chromium,
                        .id = "chromium",
                        .display_name = "Chromium",
                        .executable = "/Applications/Chromium.app/Contents/MacOS/Chromium"});
#elif defined(_WIN32)
  candidates.push_back({.kind = BrowserKind::Chrome,
                        .id = "chrome",
                        .display_name = "Google Chrome",
                        .executable = "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe"});
  candidates.push_back(
      {.kind = BrowserKind::Edge,
       .id = "edge",
       .display_name = "Microsoft Edge",
       .executable = "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe"});
  candidates.push_back({.kind = BrowserKind::Brave,
                        .id = "brave",
                        .display_name = "Brave",
                        .executable = "C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe"});
  candidates.push_back({.kind = BrowserKind::Chromium,
                        .id = "chromium",
                        .display_name = "Chromium",
                        .executable = "C:\\Program Files\\Chromium\\Application\\chrome.exe"});
#else
  candidates.push_back(
      {.kind = BrowserKind::Chrome, .id = "chrome", .display_name = "Google Chrome", .executable = "/usr/bin/google-chrome"});
  candidates.push_back(
      {.kind = BrowserKind::Chromium, .id = "chromium", .display_name = "Chromium", .executable = "/usr/bin/chromium"});
  candidates.push_back(
      {.kind = BrowserKind::Brave, .id = "brave", .display_name = "Brave", .executable = "/usr/bin/brave-browser"});
  candidates.push_back(
      {.kind = BrowserKind::Edge, .id = "edge", .display_name = "Microsoft Edge", .executable = "/usr/bin/microsoft-edge"});
#endif
  return candidates;
}

#ifndef _WIN32
bool is_port_available(const std::uint16_t port) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  int reuse = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const int rc = bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  close(fd);
  return rc == 0;
}
#else
bool is_port_available(const std::uint16_t) { return false; }
#endif

} // namespace

BrowserProfileManager::BrowserProfileManager(
    std::filesystem::path root_dir,
    std::optional<std::vector<BrowserInstallation>> injected_installations)
    : injected_installations_(std::move(injected_installations)) {
  if (root_dir.empty()) {
    root_dir = std::filesystem::temp_directory_path() / "ghostclaw-browser";
  }
  root_dir_ = std::move(root_dir);
  (void)common::ensure_dir(root_dir_);
}

common::Result<std::vector<BrowserInstallation>> BrowserProfileManager::detect_browsers() const {
  auto candidates = injected_installations_.has_value() ? *injected_installations_
                                                        : browser_candidates();
  if (injected_installations_.has_value()) {
    return common::Result<std::vector<BrowserInstallation>>::success(std::move(candidates));
  }
  for (auto &candidate : candidates) {
    std::error_code ec;
    candidate.available = std::filesystem::exists(candidate.executable, ec) && !ec;
  }
  return common::Result<std::vector<BrowserInstallation>>::success(std::move(candidates));
}

common::Result<BrowserProfile>
BrowserProfileManager::acquire_profile(const std::string &session_name,
                                       const std::optional<std::string> &preferred_browser_id) {
  const std::string trimmed_session = common::trim(session_name);
  if (trimmed_session.empty()) {
    return common::Result<BrowserProfile>::failure("session_name is required");
  }

  auto detected = detect_browsers();
  if (!detected.ok()) {
    return common::Result<BrowserProfile>::failure(detected.error());
  }
  auto chosen = select_browser(detected.value(), preferred_browser_id);
  if (!chosen.ok()) {
    return common::Result<BrowserProfile>::failure(chosen.error());
  }
  auto port = find_available_port();
  if (!port.ok()) {
    return common::Result<BrowserProfile>::failure(port.error());
  }

  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  const std::string base = sanitize_path_component(trimmed_session);
  const std::string profile_id = base + "-" + std::to_string(timestamp);

  BrowserProfile profile;
  profile.profile_id = profile_id;
  profile.session_name = trimmed_session;
  profile.browser_kind = chosen.value().kind;
  profile.browser_executable = chosen.value().executable;
  profile.devtools_port = port.value();
  profile.color_hex = pick_color_hex(trimmed_session);
  profile.user_data_dir = root_dir_ / profile_id;

  auto ensured = common::ensure_dir(profile.user_data_dir);
  if (!ensured.ok()) {
    return common::Result<BrowserProfile>::failure(ensured.error());
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_profiles_[profile.profile_id] = profile;
  }
  return common::Result<BrowserProfile>::success(profile);
}

common::Status BrowserProfileManager::release_profile(const std::string &profile_id) {
  const std::string id = common::trim(profile_id);
  if (id.empty()) {
    return common::Status::error("profile_id is required");
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = active_profiles_.find(id);
  if (it == active_profiles_.end()) {
    return common::Status::error("profile not found");
  }
  active_profiles_.erase(it);
  return common::Status::success();
}

std::vector<BrowserProfile> BrowserProfileManager::list_active_profiles() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<BrowserProfile> out;
  out.reserve(active_profiles_.size());
  for (const auto &[id, profile] : active_profiles_) {
    (void)id;
    out.push_back(profile);
  }
  std::sort(out.begin(), out.end(),
            [](const BrowserProfile &a, const BrowserProfile &b) { return a.profile_id < b.profile_id; });
  return out;
}

std::string BrowserProfileManager::normalize_id(std::string value) {
  value = common::to_lower(common::trim(std::move(value)));
  return value;
}

std::string BrowserProfileManager::sanitize_path_component(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  for (const char ch : value) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
    out.push_back(ok ? ch : '-');
  }
  if (out.empty()) {
    return "session";
  }
  return out;
}

std::string BrowserProfileManager::pick_color_hex(const std::string &session_name) {
  constexpr std::array<std::string_view, 12> palette{
      "#D7263D", "#F46036", "#2E294E", "#1B998B", "#C5D86D", "#F4D35E",
      "#0D3B66", "#FA8B60", "#5F0F40", "#9A031E", "#0F4C5C", "#33658A"};
  std::size_t hash = 1469598103934665603ULL;
  for (const char ch : session_name) {
    hash ^= static_cast<std::size_t>(static_cast<unsigned char>(ch));
    hash *= 1099511628211ULL;
  }
  return std::string(palette[hash % palette.size()]);
}

common::Result<std::uint16_t> BrowserProfileManager::find_available_port() const {
  std::vector<std::uint16_t> reserved;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    reserved.reserve(active_profiles_.size());
    for (const auto &[id, profile] : active_profiles_) {
      (void)id;
      reserved.push_back(profile.devtools_port);
    }
  }

  for (std::uint16_t port = kMinPort; port <= kMaxPort; ++port) {
    if (std::find(reserved.begin(), reserved.end(), port) != reserved.end()) {
      continue;
    }
    if (is_port_available(port)) {
      return common::Result<std::uint16_t>::success(port);
    }
  }
  return common::Result<std::uint16_t>::failure(
      "no available browser debug ports in range 18800-18899");
}

common::Result<BrowserInstallation>
BrowserProfileManager::select_browser(
    const std::vector<BrowserInstallation> &detected,
    const std::optional<std::string> &preferred_browser_id) const {
  if (preferred_browser_id.has_value() && !common::trim(*preferred_browser_id).empty()) {
    const std::string wanted = normalize_id(*preferred_browser_id);
    for (const auto &candidate : detected) {
      if (normalize_id(candidate.id) == wanted && candidate.available) {
        return common::Result<BrowserInstallation>::success(candidate);
      }
    }
    return common::Result<BrowserInstallation>::failure("preferred browser not available: " +
                                                        wanted);
  }

  for (const auto &candidate : detected) {
    if (candidate.available) {
      return common::Result<BrowserInstallation>::success(candidate);
    }
  }
  return common::Result<BrowserInstallation>::failure(
      "no supported Chromium-based browser found");
}

std::string browser_kind_to_string(const BrowserKind kind) {
  switch (kind) {
  case BrowserKind::Chrome:
    return "chrome";
  case BrowserKind::Chromium:
    return "chromium";
  case BrowserKind::Brave:
    return "brave";
  case BrowserKind::Edge:
    return "edge";
  case BrowserKind::Unknown:
    break;
  }
  return "unknown";
}

} // namespace ghostclaw::browser
