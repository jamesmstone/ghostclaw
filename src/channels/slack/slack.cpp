#include "ghostclaw/channels/slack/slack.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"

#include <sstream>

namespace ghostclaw::channels::slack {

namespace {

std::string json_escape(const std::string &value) { return common::json_escape(value); }

} // namespace

SlackChannelPlugin::SlackChannelPlugin(std::shared_ptr<providers::HttpClient> http_client)
    : http_client_(std::move(http_client)) {}

SlackChannelPlugin::~SlackChannelPlugin() { stop(); }

std::string_view SlackChannelPlugin::id() const { return "slack"; }

ChannelCapabilities SlackChannelPlugin::capabilities() const {
  ChannelCapabilities caps;
  caps.reply = true;
  caps.reactions = true;
  caps.threads = true;
  caps.media = true;
  caps.native_commands = true;
  return caps;
}

common::Status SlackChannelPlugin::start(const ChannelConfig &config) {
  if (running_.load()) {
    return common::Status::success();
  }
  if (http_client_ == nullptr) {
    return common::Status::error("slack http client unavailable");
  }

  const auto token_it = config.settings.find("bot_token");
  if (token_it == config.settings.end() || common::trim(token_it->second).empty()) {
    return common::Status::error("slack bot_token is required");
  }

  std::lock_guard<std::mutex> lock(mutex_);
  bot_token_ = common::trim(token_it->second);
  const auto channel_it = config.settings.find("channel_id");
  default_channel_id_ =
      (channel_it != config.settings.end()) ? common::trim(channel_it->second) : "";
  healthy_.store(true);
  running_.store(true);
  return common::Status::success();
}

void SlackChannelPlugin::stop() { running_.store(false); }

common::Status SlackChannelPlugin::send_text(const std::string &recipient,
                                             const std::string &text) {
  if (!running_.load()) {
    return common::Status::error("slack plugin is not running");
  }
  const std::string payload = common::trim(text);
  if (payload.empty()) {
    return common::Status::error("slack text is required");
  }

  std::string token;
  std::string channel;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    token = bot_token_;
    channel = common::trim(recipient).empty() ? default_channel_id_ : common::trim(recipient);
  }
  if (token.empty()) {
    return common::Status::error("slack bot token missing");
  }
  if (channel.empty()) {
    return common::Status::error("slack recipient channel is required");
  }

  std::ostringstream body;
  body << "{";
  body << "\"channel\":\"" << json_escape(channel) << "\",";
  body << "\"text\":\"" << json_escape(payload) << "\"";
  body << "}";

  const auto response = http_client_->post_json(
      "https://slack.com/api/chat.postMessage",
      {{"Authorization", "Bearer " + token}, {"Content-Type", "application/json"}},
      body.str(),
      15000);
  return check_response(response, "slack chat.postMessage");
}

common::Status SlackChannelPlugin::send_media(const std::string &recipient,
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

void SlackChannelPlugin::on_message(PluginMessageCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  message_callback_ = std::move(callback);
}

void SlackChannelPlugin::on_reaction(PluginReactionCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  reaction_callback_ = std::move(callback);
}

bool SlackChannelPlugin::health_check() { return !running_.load() || healthy_.load(); }

common::Status SlackChannelPlugin::check_response(const providers::HttpResponse &response,
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
  if (response.body.find("\"ok\":true") == std::string::npos &&
      response.body.find("\"ok\": true") == std::string::npos) {
    return common::Status::error(std::string(operation) + " response missing ok=true");
  }
  return common::Status::success();
}

} // namespace ghostclaw::channels::slack
