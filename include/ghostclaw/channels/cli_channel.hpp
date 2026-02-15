#pragma once

#include "ghostclaw/channels/channel.hpp"

#include <mutex>
#include <thread>

namespace ghostclaw::channels {

class CliChannel final : public IChannel {
public:
  CliChannel() = default;
  ~CliChannel() override;

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] common::Status start() override;
  void stop() override;
  [[nodiscard]] common::Status send(const std::string &recipient,
                                    const std::string &message) override;
  void on_message(MessageCallback callback) override;
  [[nodiscard]] bool health_check() override;

  [[nodiscard]] bool supports_streaming() const override;
  [[nodiscard]] common::Status stream_chunk(const std::string &session_id,
                                            std::string_view text) override;

  void inject_for_test(const ChannelMessage &message);

private:
  std::atomic<bool> running_{false};
  std::thread input_thread_;
  std::mutex callback_mutex_;
  MessageCallback callback_;
};

} // namespace ghostclaw::channels
