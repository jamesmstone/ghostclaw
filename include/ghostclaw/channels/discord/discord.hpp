#pragma once

#include "ghostclaw/channels/plugin.hpp"
#include "ghostclaw/providers/traits.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ghostclaw::channels::discord {

class DiscordChannelPlugin final : public IChannelPlugin {
public:
  explicit DiscordChannelPlugin(
      std::shared_ptr<providers::HttpClient> http_client = std::make_shared<providers::CurlHttpClient>());
  ~DiscordChannelPlugin() override;

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
  [[nodiscard]] common::Status check_response(const providers::HttpResponse &response,
                                              std::string_view operation) const;
  static std::vector<std::string> parse_allowlist(const std::string &raw);

  std::shared_ptr<providers::HttpClient> http_client_;
  std::atomic<bool> running_{false};
  std::atomic<bool> healthy_{true};

  mutable std::mutex mutex_;
  std::string bot_token_;
  std::string default_channel_id_;
  std::vector<std::string> allowed_users_;
  PluginMessageCallback message_callback_;
  PluginReactionCallback reaction_callback_;
};

} // namespace ghostclaw::channels::discord
