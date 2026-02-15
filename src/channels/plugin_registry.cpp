#include "ghostclaw/channels/plugin_registry.hpp"

#include "ghostclaw/common/fs.hpp"

#include <algorithm>

namespace ghostclaw::channels {

common::Status ChannelPluginRegistry::register_factory(std::string id, ChannelPluginFactory factory) {
  const std::string normalized = common::to_lower(common::trim(id));
  if (normalized.empty()) {
    return common::Status::error("plugin id is required");
  }
  if (!factory) {
    return common::Status::error("plugin factory is required");
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (factories_.contains(normalized)) {
    return common::Status::error("plugin already registered: " + normalized);
  }
  factories_.insert_or_assign(normalized, std::move(factory));
  return common::Status::success();
}

std::unique_ptr<IChannelPlugin> ChannelPluginRegistry::create(std::string_view id) const {
  const std::string normalized = common::to_lower(common::trim(std::string(id)));
  if (normalized.empty()) {
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = factories_.find(normalized);
  if (it == factories_.end()) {
    return nullptr;
  }
  return it->second();
}

bool ChannelPluginRegistry::contains(std::string_view id) const {
  const std::string normalized = common::to_lower(common::trim(std::string(id)));
  std::lock_guard<std::mutex> lock(mutex_);
  return factories_.contains(normalized);
}

std::vector<std::string> ChannelPluginRegistry::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> out;
  out.reserve(factories_.size());
  for (const auto &[id, factory] : factories_) {
    (void)factory;
    out.push_back(id);
  }
  std::sort(out.begin(), out.end());
  return out;
}

} // namespace ghostclaw::channels
