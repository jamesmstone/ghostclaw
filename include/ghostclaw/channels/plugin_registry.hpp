#pragma once

#include "ghostclaw/channels/plugin.hpp"
#include "ghostclaw/common/result.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ghostclaw::channels {

using ChannelPluginFactory = std::function<std::unique_ptr<IChannelPlugin>()>;

class ChannelPluginRegistry {
public:
  [[nodiscard]] common::Status register_factory(std::string id, ChannelPluginFactory factory);
  [[nodiscard]] std::unique_ptr<IChannelPlugin> create(std::string_view id) const;
  [[nodiscard]] bool contains(std::string_view id) const;
  [[nodiscard]] std::vector<std::string> list() const;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, ChannelPluginFactory> factories_;
};

} // namespace ghostclaw::channels
