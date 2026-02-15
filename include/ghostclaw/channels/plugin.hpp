#pragma once

#include "ghostclaw/common/result.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ghostclaw::channels {

struct ChannelCapabilities {
  bool polls = false;
  bool reactions = false;
  bool edit = false;
  bool unsend = false;
  bool reply = false;
  bool threads = false;
  bool media = false;
  bool native_commands = false;
};

struct ChannelConfig {
  std::string id;
  std::unordered_map<std::string, std::string> settings;
};

struct MediaMessage {
  std::string url;
  std::string mime_type;
  std::string caption;
};

struct PluginMessage {
  std::string id;
  std::string sender;
  std::string recipient;
  std::string content;
  std::string channel;
  std::uint64_t timestamp = 0;
  std::optional<std::string> reply_to;
  std::unordered_map<std::string, std::string> metadata;
};

struct ReactionEvent {
  std::string message_id;
  std::string sender;
  std::string emoji;
};

using PluginMessageCallback = std::function<void(const PluginMessage &)>;
using PluginReactionCallback = std::function<void(const ReactionEvent &)>;

class IChannelPlugin {
public:
  virtual ~IChannelPlugin() = default;

  [[nodiscard]] virtual std::string_view id() const = 0;
  [[nodiscard]] virtual ChannelCapabilities capabilities() const = 0;

  [[nodiscard]] virtual common::Status start(const ChannelConfig &config) = 0;
  virtual void stop() = 0;

  [[nodiscard]] virtual common::Status send_text(const std::string &recipient,
                                                 const std::string &text) = 0;
  [[nodiscard]] virtual common::Status send_media(const std::string &recipient,
                                                  const MediaMessage &media) = 0;
  [[nodiscard]] virtual common::Status send_reaction(const std::string &message_id,
                                                     const std::string &emoji) {
    (void)message_id;
    (void)emoji;
    return common::Status::error("channel plugin does not support reactions");
  }

  virtual void on_message(PluginMessageCallback callback) = 0;
  virtual void on_reaction(PluginReactionCallback callback) = 0;

  [[nodiscard]] virtual bool health_check() = 0;
};

} // namespace ghostclaw::channels
