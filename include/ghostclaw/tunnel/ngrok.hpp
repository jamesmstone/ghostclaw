#pragma once

#include "ghostclaw/tunnel/process.hpp"
#include "ghostclaw/tunnel/tunnel.hpp"

#include <string>

namespace ghostclaw::tunnel {

class NgrokTunnel final : public ITunnel {
public:
  explicit NgrokTunnel(std::string auth_token = "", std::string command_path = "ngrok",
                       std::string api_base = "http://127.0.0.1:4040");

  [[nodiscard]] std::string_view name() const override { return "ngrok"; }
  [[nodiscard]] common::Result<std::string> start(const std::string &local_host,
                                                  std::uint16_t local_port) override;
  [[nodiscard]] common::Status stop() override;
  [[nodiscard]] bool health_check() override;
  [[nodiscard]] std::optional<std::string> public_url() const override;

private:
  [[nodiscard]] common::Result<std::string> fetch_url_from_api() const;

  std::string auth_token_;
  std::string command_path_;
  std::string api_base_;
  SharedProcess process_;
};

} // namespace ghostclaw::tunnel
