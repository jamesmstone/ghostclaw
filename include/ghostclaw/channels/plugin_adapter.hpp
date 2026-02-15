#pragma once

#include "ghostclaw/channels/channel.hpp"
#include "ghostclaw/channels/plugin.hpp"

#include <memory>
#include <mutex>
#include <string>

namespace ghostclaw::channels {

class PluginChannelAdapter final : public IChannel {
public:
  PluginChannelAdapter(std::unique_ptr<IChannelPlugin> plugin, ChannelConfig config = {});
  ~PluginChannelAdapter() override;

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] common::Status start() override;
  void stop() override;
  [[nodiscard]] common::Status send(const std::string &recipient,
                                    const std::string &message) override;
  void on_message(MessageCallback callback) override;
  [[nodiscard]] bool health_check() override;

  [[nodiscard]] bool supports_reactions() const override;
  [[nodiscard]] common::Status react(const std::string &message_id,
                                     const std::string &emoji) override;

private:
  [[nodiscard]] ChannelMessage to_channel_message(const PluginMessage &message) const;

  std::unique_ptr<IChannelPlugin> plugin_;
  ChannelConfig config_;
  std::string name_cache_;
  mutable std::mutex callback_mutex_;
  MessageCallback message_callback_;
};

} // namespace ghostclaw::channels
