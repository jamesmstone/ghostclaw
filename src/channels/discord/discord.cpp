#include "ghostclaw/channels/discord/discord.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"

#include <sstream>

namespace ghostclaw::channels::discord {

namespace {

std::string json_escape(const std::string &value) { return common::json_escape(value); }

} // namespace

DiscordChannelPlugin::DiscordChannelPlugin(std::shared_ptr<providers::HttpClient> http_client)
    : http_client_(std::move(http_client)) {}

DiscordChannelPlugin::~DiscordChannelPlugin() { stop(); }

std::string_view DiscordChannelPlugin::id() const { return "discord"; }

ChannelCapabilities DiscordChannelPlugin::capabilities() const {
  ChannelCapabilities caps;
  caps.reply = true;
  caps.reactions = true;
  caps.threads = true;
  caps.media = true;
  caps.native_commands = true;
  return caps;
}

common::Status DiscordChannelPlugin::start(const ChannelConfig &config) {
  if (running_.load()) {
    return common::Status::success();
  }
  if (http_client_ == nullptr) {
    return common::Status::error("discord http client unavailable");
  }

  const auto token_it = config.settings.find("bot_token");
  if (token_it == config.settings.end() || common::trim(token_it->second).empty()) {
    return common::Status::error("discord bot_token is required");
  }

  std::lock_guard<std::mutex> lock(mutex_);
  bot_token_ = common::trim(token_it->second);

  auto channel_it = config.settings.find("channel_id");
  if (channel_it != config.settings.end() && !common::trim(channel_it->second).empty()) {
    default_channel_id_ = common::trim(channel_it->second);
  } else {
    auto guild_it = config.settings.find("guild_id");
    default_channel_id_ =
        (guild_it != config.settings.end()) ? common::trim(guild_it->second) : "";
  }

  allowed_users_.clear();
  const auto users_it = config.settings.find("allowed_users");
  if (users_it != config.settings.end()) {
    allowed_users_ = parse_allowlist(users_it->second);
  }

  healthy_.store(true);
  running_.store(true);
  return common::Status::success();
}

void DiscordChannelPlugin::stop() { running_.store(false); }

common::Status DiscordChannelPlugin::send_text(const std::string &recipient,
                                               const std::string &text) {
  if (!running_.load()) {
    return common::Status::error("discord plugin is not running");
  }
  const std::string payload = common::trim(text);
  if (payload.empty()) {
    return common::Status::error("discord text is required");
  }

  std::string token;
  std::string channel_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    token = bot_token_;
    channel_id = common::trim(recipient).empty() ? default_channel_id_ : common::trim(recipient);
  }
  if (token.empty()) {
    return common::Status::error("discord bot token missing");
  }
  if (channel_id.empty()) {
    return common::Status::error("discord recipient channel id is required");
  }

  std::ostringstream body;
  body << "{";
  body << "\"content\":\"" << json_escape(payload) << "\"";
  body << "}";

  const auto response = http_client_->post_json(
      "https://discord.com/api/v10/channels/" + channel_id + "/messages",
      {{"Authorization", "Bot " + token}, {"Content-Type", "application/json"}},
      body.str(),
      15000);
  return check_response(response, "discord send message");
}

common::Status DiscordChannelPlugin::send_media(const std::string &recipient,
                                                const MediaMessage &media) {
  std::string text = media.url;
  if (!common::trim(media.caption).empty()) {
    if (!text.empty()) {
      text += "\n";
    }
    text += media.caption;
  }
  return send_text(recipient, text);
}

void DiscordChannelPlugin::on_message(PluginMessageCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  message_callback_ = std::move(callback);
}

void DiscordChannelPlugin::on_reaction(PluginReactionCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  reaction_callback_ = std::move(callback);
}

bool DiscordChannelPlugin::health_check() { return !running_.load() || healthy_.load(); }

common::Status DiscordChannelPlugin::check_response(const providers::HttpResponse &response,
                                                    const std::string_view operation) const {
  if (response.timeout) {
    return common::Status::error(std::string(operation) + " timeout");
  }
  if (response.network_error) {
    return common::Status::error(std::string(operation) + " network error: " +
                                 response.network_error_message);
  }
  if (response.status < 200 || response.status >= 300) {
    std::string body = common::trim(response.body);
    if (body.size() > 200) {
      body.resize(200);
    }
    return common::Status::error(std::string(operation) + " failed status=" +
                                 std::to_string(response.status) + " body=" + body);
  }
  return common::Status::success();
}

std::vector<std::string> DiscordChannelPlugin::parse_allowlist(const std::string &raw) {
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

} // namespace ghostclaw::channels::discord
