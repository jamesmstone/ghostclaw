#pragma once

#include "ghostclaw/common/result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ghostclaw::tunnel {

class ITunnel {
public:
  virtual ~ITunnel() = default;

  [[nodiscard]] virtual std::string_view name() const = 0;
  [[nodiscard]] virtual common::Result<std::string> start(const std::string &local_host,
                                                          std::uint16_t local_port) = 0;
  [[nodiscard]] virtual common::Status stop() = 0;
  [[nodiscard]] virtual bool health_check() = 0;
  [[nodiscard]] virtual std::optional<std::string> public_url() const = 0;
};

} // namespace ghostclaw::tunnel
