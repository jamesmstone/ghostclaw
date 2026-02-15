#pragma once

#include "ghostclaw/providers/compatible.hpp"

namespace ghostclaw::providers {

class OpenAiProvider final : public CompatibleProvider {
public:
  explicit OpenAiProvider(const std::string &api_key,
                          std::shared_ptr<HttpClient> http_client = std::make_shared<CurlHttpClient>());
};

} // namespace ghostclaw::providers
