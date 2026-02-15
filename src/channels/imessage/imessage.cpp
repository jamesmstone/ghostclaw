#include "ghostclaw/channels/imessage/imessage.hpp"

#include "ghostclaw/channels/allowlist.hpp"
#include "ghostclaw/common/fs.hpp"

#include <cstdlib>
#include <sstream>

namespace ghostclaw::channels::imessage {

IMessageChannelPlugin::~IMessageChannelPlugin() { stop(); }

std::string_view IMessageChannelPlugin::id() const { return "imessage"; }

ChannelCapabilities IMessageChannelPlugin::capabilities() const {
  ChannelCapabilities caps;
  caps.reply = true;
  caps.media = true;
  return caps;
}

common::Status IMessageChannelPlugin::start(const ChannelConfig &config) {
  if (running_.load()) {
    return common::Status::success();
  }

  std::lock_guard<std::mutex> lock(mutex_);
  allowed_contacts_.clear();
  const auto allow_it = config.settings.find("allowed_contacts");
  if (allow_it != config.settings.end()) {
    allowed_contacts_ = parse_allowlist(allow_it->second);
  }
  const auto dry_it = config.settings.find("dry_run");
  dry_run_ = (dry_it != config.settings.end()) ? parse_bool_setting(dry_it->second, false) : false;

  healthy_.store(true);
  running_.store(true);
  return common::Status::success();
}

void IMessageChannelPlugin::stop() { running_.store(false); }

common::Status IMessageChannelPlugin::send_text(const std::string &recipient,
                                                const std::string &text) {
  if (!running_.load()) {
    return common::Status::error("imessage plugin is not running");
  }
  const std::string to = common::trim(recipient);
  if (to.empty()) {
    return common::Status::error("imessage recipient is required");
  }
  if (!is_allowed_contact(to)) {
    return common::Status::error("imessage recipient blocked by allowlist");
  }
  if (common::trim(text).empty()) {
    return common::Status::error("imessage text is required");
  }

  bool dry_run = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    dry_run = dry_run_;
  }
  if (dry_run) {
    return common::Status::success();
  }

#ifdef __APPLE__
  const std::string escaped_message = apple_script_escape(text);
  const std::string escaped_recipient = apple_script_escape(to);
  const std::string script = "tell application \"Messages\" to send \"" + escaped_message +
                             "\" to buddy \"" + escaped_recipient +
                             "\" of service \"E:iMessage\"";
  const std::string command = "osascript -e '" + script + "'";
  const int rc = std::system(command.c_str());
  if (rc != 0) {
    return common::Status::error("osascript send failed with exit code " +
                                 std::to_string(rc));
  }
  return common::Status::success();
#else
  return common::Status::error("imessage is only available on macOS");
#endif
}

common::Status IMessageChannelPlugin::send_media(const std::string &recipient,
                                                 const MediaMessage &media) {
  std::string content;
  if (!common::trim(media.caption).empty()) {
    content = media.caption;
  }
  if (!common::trim(media.url).empty()) {
    if (!content.empty()) {
      content += "\n";
    }
    content += media.url;
  }
  return send_text(recipient, content);
}

void IMessageChannelPlugin::on_message(PluginMessageCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  message_callback_ = std::move(callback);
}

void IMessageChannelPlugin::on_reaction(PluginReactionCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  reaction_callback_ = std::move(callback);
}

bool IMessageChannelPlugin::health_check() { return !running_.load() || healthy_.load(); }

bool IMessageChannelPlugin::is_allowed_contact(const std::string &contact) const {
  std::vector<std::string> allowlist;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    allowlist = allowed_contacts_;
  }
  if (allowlist.empty()) {
    return true;
  }
  return check_allowlist(common::to_lower(common::trim(contact)), allowlist);
}

std::vector<std::string> IMessageChannelPlugin::parse_allowlist(const std::string &raw) {
  std::vector<std::string> out;
  std::stringstream stream(raw);
  std::string token;
  while (std::getline(stream, token, ',')) {
    token = common::to_lower(common::trim(token));
    if (!token.empty()) {
      out.push_back(token);
    }
  }
  return out;
}

std::string IMessageChannelPlugin::apple_script_escape(const std::string &value) {
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

bool IMessageChannelPlugin::parse_bool_setting(const std::string &value, const bool fallback) {
  const std::string normalized = common::to_lower(common::trim(value));
  if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
    return true;
  }
  if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") {
    return false;
  }
  return fallback;
}

} // namespace ghostclaw::channels::imessage
