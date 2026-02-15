#include "ghostclaw/browser/chrome.hpp"

#include "ghostclaw/common/fs.hpp"

namespace ghostclaw::browser {

common::Result<std::vector<std::string>>
build_chrome_launch_args(const ChromeLaunchOptions &options) {
  if (options.profile.browser_executable.empty()) {
    return common::Result<std::vector<std::string>>::failure(
        "browser executable is required");
  }
  if (options.profile.devtools_port == 0) {
    return common::Result<std::vector<std::string>>::failure(
        "devtools port must be non-zero");
  }
  if (options.profile.user_data_dir.empty()) {
    return common::Result<std::vector<std::string>>::failure(
        "user data dir is required");
  }

  std::vector<std::string> args;
  args.push_back(options.profile.browser_executable.string());
  args.push_back("--remote-debugging-port=" + std::to_string(options.profile.devtools_port));
  args.push_back("--user-data-dir=" + options.profile.user_data_dir.string());
  args.push_back("--disable-background-networking");
  args.push_back("--disable-sync");
  args.push_back("--disable-component-update");
  args.push_back("--disable-features=OptimizationHints,Translate");
  args.push_back("--metrics-recording-only");
  args.push_back("--password-store=basic");
  if (options.no_first_run) {
    args.push_back("--no-first-run");
  }
  if (options.no_default_browser_check) {
    args.push_back("--no-default-browser-check");
  }
  if (options.disable_gpu) {
    args.push_back("--disable-gpu");
  }
  if (options.headless) {
    args.push_back("--headless=new");
  }
  args.push_back(common::trim(options.start_url).empty() ? "about:blank" : options.start_url);
  return common::Result<std::vector<std::string>>::success(std::move(args));
}

common::Result<std::string> build_devtools_ws_url(const std::uint16_t port,
                                                  const std::string &target_path) {
  if (port == 0) {
    return common::Result<std::string>::failure("port must be non-zero");
  }
  const std::string path = common::trim(target_path).empty() ? "/devtools/browser" : target_path;
  if (!path.starts_with('/')) {
    return common::Result<std::string>::failure("target path must start with '/'");
  }
  return common::Result<std::string>::success("ws://127.0.0.1:" + std::to_string(port) + path);
}

} // namespace ghostclaw::browser
