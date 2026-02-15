#pragma once

#include "ghostclaw/channels/plugin.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace ghostclaw::channels::imessage {

class IMessageChannelPlugin final : public IChannelPlugin {
public:
  IMessageChannelPlugin() = default;
  ~IMessageChannelPlugin() override;

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
  [[nodiscard]] bool is_allowed_contact(const std::string &contact) const;
  static std::vector<std::string> parse_allowlist(const std::string &raw);
  static std::string apple_script_escape(const std::string &value);
  static bool parse_bool_setting(const std::string &value, bool fallback);

  std::atomic<bool> running_{false};
  std::atomic<bool> healthy_{true};

  mutable std::mutex mutex_;
  std::vector<std::string> allowed_contacts_;
  bool dry_run_ = false;
  PluginMessageCallback message_callback_;
  PluginReactionCallback reaction_callback_;
};

} // namespace ghostclaw::channels::imessage
