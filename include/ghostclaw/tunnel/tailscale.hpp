#pragma once

#include "ghostclaw/tunnel/tunnel.hpp"

#include <cstdint>
#include <string>

namespace ghostclaw::tunnel {

class TailscaleTunnel final : public ITunnel {
public:
  explicit TailscaleTunnel(std::string command_path = "tailscale", std::string hostname = "");

  [[nodiscard]] std::string_view name() const override { return "tailscale"; }
  [[nodiscard]] common::Result<std::string> start(const std::string &local_host,
                                                  std::uint16_t local_port) override;
  [[nodiscard]] common::Status stop() override;
  [[nodiscard]] bool health_check() override;
  [[nodiscard]] std::optional<std::string> public_url() const override;

private:
  [[nodiscard]] common::Result<std::string> get_tailscale_hostname() const;

  std::string command_path_;
  std::string configured_hostname_;
  std::uint16_t port_ = 0;
  std::string public_url_;
  bool running_ = false;
};

} // namespace ghostclaw::tunnel
