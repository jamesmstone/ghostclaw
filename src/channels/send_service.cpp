#include "ghostclaw/channels/send_service.hpp"

#include "ghostclaw/channels/channel_manager.hpp"
#include "ghostclaw/common/fs.hpp"

namespace ghostclaw::channels {

SendService::SendService(const config::Config &config) : config_(config) {}

common::Status SendService::send(const SendMessageRequest &request) const {
  const std::string channel = common::to_lower(common::trim(request.channel));
  const std::string recipient = common::trim(request.recipient);
  const std::string text = common::trim(request.text);
  if (channel.empty()) {
    return common::Status::error("channel is required");
  }
  if (recipient.empty()) {
    return common::Status::error("recipient is required");
  }
  if (text.empty()) {
    return common::Status::error("text is required");
  }

  auto manager = create_channel_manager(config_, {.send_only = true});
  auto started = manager->start_all([](const ChannelMessage &) {});
  if (!started.ok()) {
    return common::Status::error("failed to start channels for send: " + started.error());
  }

  auto *target = manager->get_channel(channel);
  if (target == nullptr) {
    manager->stop_all();
    return common::Status::error("channel not configured: " + channel);
  }

  const auto sent = target->send(recipient, text);
  manager->stop_all();
  return sent;
}

} // namespace ghostclaw::channels
