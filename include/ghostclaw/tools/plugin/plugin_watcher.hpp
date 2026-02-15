#pragma once

#include "ghostclaw/common/result.hpp"

namespace ghostclaw::tools::plugin {

class PluginWatcher {
public:
  [[nodiscard]] common::Status refresh_once();
};

} // namespace ghostclaw::tools::plugin
