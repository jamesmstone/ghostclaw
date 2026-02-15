#pragma once

#include "ghostclaw/tunnel/process.hpp"
#include "ghostclaw/tunnel/tunnel.hpp"

#include <string>

namespace ghostclaw::tunnel {

class CloudflareTunnel final : public ITunnel {
public:
  explicit CloudflareTunnel(std::string command_path = "cloudflared");

  [[nodiscard]] std::string_view name() const override { return "cloudflare"; }
  [[nodiscard]] common::Result<std::string> start(const std::string &local_host,
                                                  std::uint16_t local_port) override;
  [[nodiscard]] common::Status stop() override;
  [[nodiscard]] bool health_check() override;
  [[nodiscard]] std::optional<std::string> public_url() const override;

private:
  [[nodiscard]] std::optional<std::string> extract_url_from_output(const std::string &output) const;

  std::string command_path_;
  SharedProcess process_;
};

} // namespace ghostclaw::tunnel
