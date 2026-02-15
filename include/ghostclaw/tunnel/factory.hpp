#pragma once

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/tunnel/tunnel.hpp"

#include <memory>

namespace ghostclaw::tunnel {

[[nodiscard]] std::unique_ptr<ITunnel> create_tunnel(const config::TunnelConfig &config);

} // namespace ghostclaw::tunnel
