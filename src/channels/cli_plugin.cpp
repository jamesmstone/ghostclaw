#include "ghostclaw/channels/cli_plugin.hpp"

#include <ctime>

namespace ghostclaw::channels {

CliChannelPlugin::CliChannelPlugin() : channel_(std::make_unique<CliChannel>()) {}

CliChannelPlugin::~CliChannelPlugin() { stop(); }

std::string_view CliChannelPlugin::id() const { return "cli"; }

ChannelCapabilities CliChannelPlugin::capabilities() const {
  ChannelCapabilities caps;
  caps.media = false;
  caps.reactions = false;
  return caps;
}

common::Status CliChannelPlugin::start(const ChannelConfig &config) {
  config_ = config;
  if (channel_ == nullptr) {
    channel_ = std::make_unique<CliChannel>();
  }
  bind_channel_callback();
  return channel_->start();
}

void CliChannelPlugin::stop() {
  if (channel_ != nullptr) {
    channel_->stop();
  }
}

common::Status CliChannelPlugin::send_text(const std::string &recipient, const std::string &text) {
  if (channel_ == nullptr) {
    return common::Status::error("cli channel unavailable");
  }
  return channel_->send(recipient, text);
}

common::Status CliChannelPlugin::send_media(const std::string &, const MediaMessage &) {
  return common::Status::error("cli channel does not support media");
}

void CliChannelPlugin::on_message(PluginMessageCallback callback) {
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    message_callback_ = std::move(callback);
  }
  bind_channel_callback();
}

void CliChannelPlugin::on_reaction(PluginReactionCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  reaction_callback_ = std::move(callback);
}

bool CliChannelPlugin::health_check() {
  return channel_ != nullptr && channel_->health_check();
}

void CliChannelPlugin::bind_channel_callback() {
  if (channel_ == nullptr) {
    return;
  }
  channel_->on_message([this](const ChannelMessage &message) {
    PluginMessageCallback callback_copy;
    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callback_copy = message_callback_;
    }
    if (!callback_copy) {
      return;
    }
    PluginMessage plugin_message;
    plugin_message.id = message.id.empty() ? std::to_string(std::time(nullptr)) : message.id;
    plugin_message.sender = message.sender;
    plugin_message.content = message.content;
    plugin_message.channel = message.channel.empty() ? "cli" : message.channel;
    plugin_message.timestamp = message.timestamp;
    plugin_message.reply_to = message.reply_to;
    callback_copy(plugin_message);
  });
}

} // namespace ghostclaw::channels
