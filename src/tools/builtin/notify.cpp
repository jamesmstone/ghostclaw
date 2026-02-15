#include "ghostclaw/tools/builtin/notify.hpp"

#include "ghostclaw/common/fs.hpp"

#include <cstdlib>
#include <sstream>

namespace ghostclaw::tools {

namespace {

std::string shell_single_quote(const std::string &value) {
  std::string out = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out.push_back(ch);
    }
  }
  out.push_back('\'');
  return out;
}

bool command_exists(const std::string &binary) {
  const std::string command = "command -v " + shell_single_quote(binary) + " >/dev/null 2>&1";
  return std::system(command.c_str()) == 0;
}

bool parse_bool(const ToolArgs &args, const std::string &key, const bool fallback = false) {
  const auto it = args.find(key);
  if (it == args.end()) {
    return fallback;
  }
  const std::string value = common::to_lower(common::trim(it->second));
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    return false;
  }
  return fallback;
}

std::string escape_applescript_string(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char ch : value) {
    if (ch == '"' || ch == '\\') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

} // namespace

std::string_view NotifyTool::name() const { return "notify"; }

std::string_view NotifyTool::description() const {
  return "Send a host notification (macOS Notification Center / notify-send)";
}

std::string NotifyTool::parameters_schema() const {
  return R"({"type":"object","properties":{"title":{"type":"string"},"body":{"type":"string"},"silent_fail":{"type":"string"}}})";
}

common::Result<ToolResult> NotifyTool::execute(const ToolArgs &args, const ToolContext &) {
  const std::string title = args.contains("title") ? common::trim(args.at("title")) : "GhostClaw";
  const std::string body = args.contains("body") ? common::trim(args.at("body")) : "";
  if (title.empty() && body.empty()) {
    return common::Result<ToolResult>::failure("title or body is required");
  }

  bool delivered = false;
  std::string backend = "none";
  std::string error;

#if defined(__APPLE__)
  if (command_exists("osascript")) {
    std::ostringstream script;
    script << "display notification \"" << escape_applescript_string(body) << "\" with title \""
           << escape_applescript_string(title.empty() ? "GhostClaw" : title) << "\"";
    const std::string command = "osascript -e " + shell_single_quote(script.str());
    delivered = std::system(command.c_str()) == 0;
    backend = "osascript";
    if (!delivered) {
      error = "osascript command failed";
    }
  }
#elif !defined(_WIN32)
  if (command_exists("notify-send")) {
    const std::string command =
        "notify-send " + shell_single_quote(title.empty() ? "GhostClaw" : title) + " " +
        shell_single_quote(body);
    delivered = std::system(command.c_str()) == 0;
    backend = "notify-send";
    if (!delivered) {
      error = "notify-send command failed";
    }
  }
#endif

  const bool silent_fail = parse_bool(args, "silent_fail", true);
  if (!delivered && !silent_fail) {
    return common::Result<ToolResult>::failure(
        error.empty() ? "notification backend unavailable" : error);
  }

  ToolResult out;
  out.success = true;
  out.output = delivered ? "notification delivered" : "notification queued";
  out.metadata["backend"] = backend;
  if (!error.empty()) {
    out.metadata["delivery_error"] = error;
  }
  return common::Result<ToolResult>::success(std::move(out));
}

bool NotifyTool::is_safe() const { return false; }

std::string_view NotifyTool::group() const { return "messaging"; }

} // namespace ghostclaw::tools
