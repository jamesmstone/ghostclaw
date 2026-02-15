#pragma once

#include "ghostclaw/channels/plugin.hpp"
#include "ghostclaw/providers/traits.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ghostclaw::channels::telegram {

class TelegramChannelPlugin final : public IChannelPlugin {
public:
  struct IncomingMessage {
    std::uint64_t update_id = 0;
    std::string message_id;
    std::string chat_id;
    std::string sender;
    std::string sender_username;
    std::string sender_id;
    std::string text;
    std::uint64_t timestamp = 0;
  };

  explicit TelegramChannelPlugin(
      std::shared_ptr<providers::HttpClient> http_client = std::make_shared<providers::CurlHttpClient>());
  ~TelegramChannelPlugin() override;

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
  [[nodiscard]] common::Status poll_once();
  void run_loop();
  [[nodiscard]] common::Status parse_and_dispatch_updates(const std::string &response_body);
  [[nodiscard]] bool is_allowed_sender(const IncomingMessage &message) const;
  [[nodiscard]] common::Status check_api_response(const providers::HttpResponse &response,
                                                  std::string_view operation) const;

  static std::vector<std::string> parse_allowlist(const std::string &raw);
  static bool parse_bool_setting(const std::string &value, bool fallback);
  static std::uint64_t parse_u64_setting(const std::string &value, std::uint64_t fallback);

  std::shared_ptr<providers::HttpClient> http_client_;
  std::atomic<bool> running_{false};
  std::atomic<bool> healthy_{true};
  std::thread worker_;

  mutable std::mutex callback_mutex_;
  PluginMessageCallback message_callback_;
  PluginReactionCallback reaction_callback_;

  mutable std::mutex state_mutex_;
  ChannelConfig config_;
  std::string base_url_;
  std::vector<std::string> allowed_users_;
  std::uint64_t next_update_offset_ = 0;
  std::uint64_t poll_timeout_seconds_ = 2;
  std::chrono::milliseconds idle_sleep_{std::chrono::milliseconds(150)};
  bool polling_enabled_ = true;
  std::string last_error_;
};

} // namespace ghostclaw::channels::telegram
