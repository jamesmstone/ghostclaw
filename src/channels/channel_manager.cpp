#include "ghostclaw/channels/channel_manager.hpp"

#include "ghostclaw/channels/cli_plugin.hpp"
#include "ghostclaw/channels/discord/discord.hpp"
#include "ghostclaw/channels/imessage/imessage.hpp"
#include "ghostclaw/channels/plugin_adapter.hpp"
#include "ghostclaw/channels/signal/signal.hpp"
#include "ghostclaw/channels/slack/slack.hpp"
#include "ghostclaw/channels/telegram/telegram.hpp"
#include "ghostclaw/channels/whatsapp/whatsapp.hpp"
#include "ghostclaw/common/fs.hpp"

#include <sstream>

namespace ghostclaw::channels {

ChannelManager::ChannelManager(const config::Config &config) : config_(config) {}

ChannelManager::~ChannelManager() { stop_all(); }

void ChannelManager::add_channel(std::unique_ptr<IChannel> channel) {
  if (channel != nullptr) {
    channels_.push_back(std::move(channel));
  }
}

common::Status ChannelManager::register_plugin(std::string id, ChannelPluginFactory factory) {
  return plugin_registry_.register_factory(std::move(id), std::move(factory));
}

common::Status ChannelManager::add_plugin(std::string_view id, ChannelConfig config) {
  auto plugin = plugin_registry_.create(id);
  if (plugin == nullptr) {
    return common::Status::error("unknown channel plugin: " + std::string(id));
  }
  if (config.id.empty()) {
    config.id = std::string(id);
  }
  return add_plugin(std::move(plugin), std::move(config));
}

common::Status ChannelManager::add_plugin(std::unique_ptr<IChannelPlugin> plugin,
                                          ChannelConfig config) {
  if (plugin == nullptr) {
    return common::Status::error("plugin is required");
  }
  if (config.id.empty()) {
    config.id = std::string(plugin->id());
  }
  add_channel(std::make_unique<PluginChannelAdapter>(std::move(plugin), std::move(config)));
  return common::Status::success();
}

common::Status ChannelManager::start_all(MessageCallback callback) {
  if (running_) {
    return common::Status::success();
  }
  running_ = true;

  supervisors_.clear();
  supervisors_.reserve(channels_.size());

  for (auto &channel : channels_) {
    auto supervisor = std::make_unique<ChannelSupervisor>(*channel, callback);
    supervisor->start();
    supervisors_.push_back(std::move(supervisor));
  }

  return common::Status::success();
}

void ChannelManager::stop_all() {
  if (!running_) {
    return;
  }
  for (auto &supervisor : supervisors_) {
    supervisor->stop();
  }
  supervisors_.clear();
  running_ = false;
}

IChannel *ChannelManager::get_channel(std::string_view name) const {
  const std::string needle = common::to_lower(std::string(name));
  for (const auto &channel : channels_) {
    if (common::to_lower(std::string(channel->name())) == needle) {
      return channel.get();
    }
  }
  return nullptr;
}

std::vector<std::string> ChannelManager::list_channels() const {
  std::vector<std::string> out;
  out.reserve(channels_.size());
  for (const auto &channel : channels_) {
    out.push_back(std::string(channel->name()));
  }
  return out;
}

std::vector<std::string> ChannelManager::list_plugins() const { return plugin_registry_.list(); }

std::unique_ptr<ChannelManager> create_channel_manager(const config::Config &config,
                                                       const ChannelManagerCreateOptions options) {
  auto manager = std::make_unique<ChannelManager>(config);
  (void)manager->register_plugin("cli", []() { return std::make_unique<CliChannelPlugin>(); });
  (void)manager->register_plugin("discord", []() {
    return std::make_unique<discord::DiscordChannelPlugin>();
  });
  (void)manager->register_plugin("slack", []() {
    return std::make_unique<slack::SlackChannelPlugin>();
  });
  (void)manager->register_plugin("whatsapp", []() {
    return std::make_unique<whatsapp::WhatsAppChannelPlugin>();
  });
  (void)manager->register_plugin("signal", []() {
    return std::make_unique<signal::SignalChannelPlugin>();
  });
  (void)manager->register_plugin("imessage", []() {
    return std::make_unique<imessage::IMessageChannelPlugin>();
  });
  (void)manager->register_plugin("telegram", []() {
    return std::make_unique<telegram::TelegramChannelPlugin>();
  });
  (void)manager->add_plugin("cli", {.id = "cli"});

  if (config.channels.discord.has_value() &&
      !common::trim(config.channels.discord->bot_token).empty()) {
    ChannelConfig discord_config;
    discord_config.id = "discord";
    discord_config.settings["bot_token"] = config.channels.discord->bot_token;
    if (!common::trim(config.channels.discord->guild_id).empty()) {
      discord_config.settings["guild_id"] = config.channels.discord->guild_id;
      discord_config.settings["channel_id"] = config.channels.discord->guild_id;
    }
    if (!config.channels.discord->allowed_users.empty()) {
      std::ostringstream users;
      for (std::size_t i = 0; i < config.channels.discord->allowed_users.size(); ++i) {
        if (i > 0) {
          users << ",";
        }
        users << config.channels.discord->allowed_users[i];
      }
      discord_config.settings["allowed_users"] = users.str();
    }
    (void)manager->add_plugin("discord", std::move(discord_config));
  }

  if (config.channels.slack.has_value() &&
      !common::trim(config.channels.slack->bot_token).empty()) {
    ChannelConfig slack_config;
    slack_config.id = "slack";
    slack_config.settings["bot_token"] = config.channels.slack->bot_token;
    if (!common::trim(config.channels.slack->channel_id).empty()) {
      slack_config.settings["channel_id"] = config.channels.slack->channel_id;
    }
    if (!config.channels.slack->allowed_users.empty()) {
      std::ostringstream users;
      for (std::size_t i = 0; i < config.channels.slack->allowed_users.size(); ++i) {
        if (i > 0) {
          users << ",";
        }
        users << config.channels.slack->allowed_users[i];
      }
      slack_config.settings["allowed_users"] = users.str();
    }
    (void)manager->add_plugin("slack", std::move(slack_config));
  }

  if (config.channels.telegram.has_value() &&
      !common::trim(config.channels.telegram->bot_token).empty()) {
    ChannelConfig telegram_config;
    telegram_config.id = "telegram";
    telegram_config.settings["bot_token"] = config.channels.telegram->bot_token;
    if (options.send_only) {
      telegram_config.settings["polling_enabled"] = "false";
    }
    if (!config.channels.telegram->allowed_users.empty()) {
      std::ostringstream users;
      for (std::size_t i = 0; i < config.channels.telegram->allowed_users.size(); ++i) {
        if (i > 0) {
          users << ",";
        }
        users << config.channels.telegram->allowed_users[i];
      }
      telegram_config.settings["allowed_users"] = users.str();
    }
    (void)manager->add_plugin("telegram", std::move(telegram_config));
  }

  if (config.channels.whatsapp.has_value() &&
      !common::trim(config.channels.whatsapp->access_token).empty() &&
      !common::trim(config.channels.whatsapp->phone_number_id).empty()) {
    ChannelConfig whatsapp_config;
    whatsapp_config.id = "whatsapp";
    whatsapp_config.settings["access_token"] = config.channels.whatsapp->access_token;
    whatsapp_config.settings["phone_number_id"] = config.channels.whatsapp->phone_number_id;
    if (!config.channels.whatsapp->allowed_numbers.empty()) {
      std::ostringstream numbers;
      for (std::size_t i = 0; i < config.channels.whatsapp->allowed_numbers.size(); ++i) {
        if (i > 0) {
          numbers << ",";
        }
        numbers << config.channels.whatsapp->allowed_numbers[i];
      }
      whatsapp_config.settings["allowed_numbers"] = numbers.str();
    }
    (void)manager->add_plugin("whatsapp", std::move(whatsapp_config));
  }

  if (config.channels.imessage.has_value()) {
    ChannelConfig imessage_config;
    imessage_config.id = "imessage";
    if (!config.channels.imessage->allowed_contacts.empty()) {
      std::ostringstream contacts;
      for (std::size_t i = 0; i < config.channels.imessage->allowed_contacts.size(); ++i) {
        if (i > 0) {
          contacts << ",";
        }
        contacts << config.channels.imessage->allowed_contacts[i];
      }
      imessage_config.settings["allowed_contacts"] = contacts.str();
    }
    imessage_config.settings["dry_run"] = "true";
    (void)manager->add_plugin("imessage", std::move(imessage_config));
  }
  return manager;
}

} // namespace ghostclaw::channels
