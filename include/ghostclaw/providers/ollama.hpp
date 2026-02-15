#pragma once

#include "ghostclaw/providers/compatible.hpp"

namespace ghostclaw::providers {

class OllamaProvider final : public CompatibleProvider {
public:
  explicit OllamaProvider(std::shared_ptr<HttpClient> http_client = std::make_shared<CurlHttpClient>());
};

} // namespace ghostclaw::providers
