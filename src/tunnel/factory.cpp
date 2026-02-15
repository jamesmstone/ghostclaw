#include "ghostclaw/tunnel/factory.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/tunnel/cloudflare.hpp"
#include "ghostclaw/tunnel/custom.hpp"
#include "ghostclaw/tunnel/ngrok.hpp"
#include "ghostclaw/tunnel/none.hpp"
#include "ghostclaw/tunnel/tailscale.hpp"

namespace ghostclaw::tunnel {

std::unique_ptr<ITunnel> create_tunnel(const config::TunnelConfig &config) {
  const std::string provider = common::to_lower(common::trim(config.provider));
  if (provider.empty() || provider == "none") {
    return std::make_unique<NoneTunnel>();
  }

  if (provider == "cloudflare") {
    const std::string command = config.cloudflare.has_value() && !config.cloudflare->command_path.empty()
                                    ? config.cloudflare->command_path
                                    : "cloudflared";
    return std::make_unique<CloudflareTunnel>(command);
  }

  if (provider == "ngrok") {
    const std::string token = config.ngrok.has_value() ? config.ngrok->auth_token : "";
    return std::make_unique<NgrokTunnel>(token);
  }

  if (provider == "tailscale") {
    const std::string hostname = config.tailscale.has_value() ? config.tailscale->hostname : "";
    return std::make_unique<TailscaleTunnel>("tailscale", hostname);
  }

  if (provider == "custom") {
    if (config.custom.has_value() && !config.custom->command.empty()) {
      return std::make_unique<CustomTunnel>(config.custom->command, config.custom->args);
    }
    return std::make_unique<NoneTunnel>();
  }

  return std::make_unique<NoneTunnel>();
}

} // namespace ghostclaw::tunnel
