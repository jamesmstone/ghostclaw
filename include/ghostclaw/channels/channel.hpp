#pragma once

#include "ghostclaw/common/result.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ghostclaw::channels {

struct ChannelMessage {
  std::string id;
  std::string sender;
  std::string recipient;  // For reply routing (e.g., chat_id for Telegram)
  std::string content;
  std::string channel;
  std::uint64_t timestamp = 0;
  std::optional<std::string> reply_to;
};

using MessageCallback = std::function<void(const ChannelMessage &)>;

class IChannel {
public:
  virtual ~IChannel() = default;

  [[nodiscard]] virtual std::string_view name() const = 0;
  [[nodiscard]] virtual common::Status start() = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual common::Status send(const std::string &recipient,
                                            const std::string &message) = 0;
  virtual void on_message(MessageCallback callback) = 0;
  [[nodiscard]] virtual bool health_check() = 0;

  [[nodiscard]] virtual bool supports_streaming() const { return false; }
  [[nodiscard]] virtual common::Status stream_start(const std::string &) {
    return common::Status::success();
  }
  [[nodiscard]] virtual common::Status stream_chunk(const std::string &, std::string_view) {
    return common::Status::success();
  }
  [[nodiscard]] virtual common::Status stream_end(const std::string &) {
    return common::Status::success();
  }

  [[nodiscard]] virtual bool supports_reactions() const { return false; }
  [[nodiscard]] virtual common::Status react(const std::string &, const std::string &) {
    return common::Status::success();
  }
};

} // namespace ghostclaw::channels
