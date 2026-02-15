#pragma once

#include "ghostclaw/channels/channel.hpp"
#include "ghostclaw/channels/plugin.hpp"
#include "ghostclaw/channels/plugin_registry.hpp"
#include "ghostclaw/channels/supervisor.hpp"
#include "ghostclaw/config/schema.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ghostclaw::channels {

struct ChannelManagerCreateOptions {
  bool send_only = false;
};

class ChannelManager {
public:
  explicit ChannelManager(const config::Config &config);
  ~ChannelManager();

  void add_channel(std::unique_ptr<IChannel> channel);
  [[nodiscard]] common::Status register_plugin(std::string id, ChannelPluginFactory factory);
  [[nodiscard]] common::Status add_plugin(std::string_view id, ChannelConfig config = {});
  [[nodiscard]] common::Status add_plugin(std::unique_ptr<IChannelPlugin> plugin,
                                          ChannelConfig config = {});
  [[nodiscard]] common::Status start_all(MessageCallback callback);
  void stop_all();

  [[nodiscard]] IChannel *get_channel(std::string_view name) const;
  [[nodiscard]] std::vector<std::string> list_channels() const;
  [[nodiscard]] std::vector<std::string> list_plugins() const;

private:
  const config::Config &config_;
  ChannelPluginRegistry plugin_registry_;
  std::vector<std::unique_ptr<IChannel>> channels_;
  std::vector<std::unique_ptr<ChannelSupervisor>> supervisors_;
  bool running_ = false;
};

[[nodiscard]] std::unique_ptr<ChannelManager>
create_channel_manager(const config::Config &config,
                       ChannelManagerCreateOptions options = {});

} // namespace ghostclaw::channels
