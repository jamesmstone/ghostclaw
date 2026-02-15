#include "ghostclaw/channels/plugin_adapter.hpp"

#include "ghostclaw/common/fs.hpp"

#include <ctime>

namespace ghostclaw::channels {

PluginChannelAdapter::PluginChannelAdapter(std::unique_ptr<IChannelPlugin> plugin, ChannelConfig config)
    : plugin_(std::move(plugin)), config_(std::move(config)) {
  if (plugin_ != nullptr) {
    name_cache_ = std::string(plugin_->id());
    if (config_.id.empty()) {
      config_.id = name_cache_;
    }
  }
}

PluginChannelAdapter::~PluginChannelAdapter() { stop(); }

std::string_view PluginChannelAdapter::name() const { return name_cache_; }

common::Status PluginChannelAdapter::start() {
  if (plugin_ == nullptr) {
    return common::Status::error("plugin unavailable");
  }

  plugin_->on_message([this](const PluginMessage &message) {
    MessageCallback callback_copy;
    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callback_copy = message_callback_;
    }
    if (callback_copy) {
      callback_copy(to_channel_message(message));
    }
  });
  plugin_->on_reaction([](const ReactionEvent &) {});
  return plugin_->start(config_);
}

void PluginChannelAdapter::stop() {
  if (plugin_ != nullptr) {
    plugin_->stop();
  }
}

common::Status PluginChannelAdapter::send(const std::string &recipient, const std::string &message) {
  if (plugin_ == nullptr) {
    return common::Status::error("plugin unavailable");
  }
  return plugin_->send_text(recipient, message);
}

void PluginChannelAdapter::on_message(MessageCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  message_callback_ = std::move(callback);
}

bool PluginChannelAdapter::health_check() {
  return plugin_ != nullptr && plugin_->health_check();
}

bool PluginChannelAdapter::supports_reactions() const {
  return plugin_ != nullptr && plugin_->capabilities().reactions;
}

common::Status PluginChannelAdapter::react(const std::string &message_id, const std::string &emoji) {
  if (plugin_ == nullptr) {
    return common::Status::error("plugin unavailable");
  }
  if (!supports_reactions()) {
    return common::Status::error("channel does not support reactions");
  }
  return plugin_->send_reaction(message_id, emoji);
}

ChannelMessage PluginChannelAdapter::to_channel_message(const PluginMessage &message) const {
  ChannelMessage out;
  out.id = message.id.empty() ? std::to_string(std::time(nullptr)) : message.id;
  out.sender = message.sender;
  out.recipient = message.recipient;
  out.content = message.content;
  out.channel = message.channel.empty() ? name_cache_ : message.channel;
  out.timestamp = message.timestamp;
  out.reply_to = message.reply_to;
  return out;
}

} // namespace ghostclaw::channels
