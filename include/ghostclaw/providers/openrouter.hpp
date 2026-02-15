#pragma once

#include "ghostclaw/providers/compatible.hpp"

namespace ghostclaw::providers {

class OpenRouterProvider final : public CompatibleProvider {
public:
  explicit OpenRouterProvider(const std::string &api_key,
                              std::shared_ptr<HttpClient> http_client =
                                  std::make_shared<CurlHttpClient>());
};

} // namespace ghostclaw::providers
