#pragma once

#include "ghostclaw/channels/cli_channel.hpp"
#include "ghostclaw/channels/plugin.hpp"

#include <memory>
#include <mutex>

namespace ghostclaw::channels {

class CliChannelPlugin final : public IChannelPlugin {
public:
  CliChannelPlugin();
  ~CliChannelPlugin() override;

  [[nodiscard]] std::string_view id() const override;
  [[nodiscard]] ChannelCapabilities capabilities() const override;
  [[nodiscard]] common::Status start(const ChannelConfig &config) override;
  void stop() override;

  [[nodiscard]] common::Status send_text(const std::string &recipient,
                                         const std::string &text) override;
  [[nodiscard]] common::Status send_media(const std::string &recipient,
                                          const MediaMessage &media) override;

  void on_message(PluginMessageCallback callback) override;
  void on_reaction(PluginReactionCallback callback) override;

  [[nodiscard]] bool health_check() override;

private:
  void bind_channel_callback();

  std::unique_ptr<CliChannel> channel_;
  ChannelConfig config_;
  std::mutex callback_mutex_;
  PluginMessageCallback message_callback_;
  PluginReactionCallback reaction_callback_;
};

} // namespace ghostclaw::channels
