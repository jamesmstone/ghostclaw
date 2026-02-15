#pragma once

#include "ghostclaw/tunnel/process.hpp"
#include "ghostclaw/tunnel/tunnel.hpp"

#include <string>
#include <vector>

namespace ghostclaw::tunnel {

class CustomTunnel final : public ITunnel {
public:
  CustomTunnel(std::string command, std::vector<std::string> args);

  [[nodiscard]] std::string_view name() const override { return "custom"; }
  [[nodiscard]] common::Result<std::string> start(const std::string &local_host,
                                                  std::uint16_t local_port) override;
  [[nodiscard]] common::Status stop() override;
  [[nodiscard]] bool health_check() override;
  [[nodiscard]] std::optional<std::string> public_url() const override;

private:
  [[nodiscard]] static std::string substitute_placeholders(const std::string &input,
                                                           const std::string &host,
                                                           std::uint16_t port);

  std::string command_;
  std::vector<std::string> args_;
  SharedProcess process_;
};

} // namespace ghostclaw::tunnel
