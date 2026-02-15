#pragma once

#include "ghostclaw/tunnel/tunnel.hpp"

namespace ghostclaw::tunnel {

class NoneTunnel final : public ITunnel {
public:
  [[nodiscard]] std::string_view name() const override { return "none"; }

  [[nodiscard]] common::Result<std::string> start(const std::string &, std::uint16_t) override {
    return common::Result<std::string>::failure("no tunnel configured");
  }

  [[nodiscard]] common::Status stop() override { return common::Status::success(); }

  [[nodiscard]] bool health_check() override { return false; }

  [[nodiscard]] std::optional<std::string> public_url() const override { return std::nullopt; }
};

} // namespace ghostclaw::tunnel
