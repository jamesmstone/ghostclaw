#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"

#include <string>

namespace ghostclaw::channels {

struct SendMessageRequest {
  std::string channel;
  std::string recipient;
  std::string text;
};

class SendService {
public:
  explicit SendService(const config::Config &config);

  [[nodiscard]] common::Status send(const SendMessageRequest &request) const;

private:
  const config::Config &config_;
};

} // namespace ghostclaw::channels
