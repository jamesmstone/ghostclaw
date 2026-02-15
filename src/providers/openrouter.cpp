#include "ghostclaw/providers/openrouter.hpp"

#include <unordered_map>

namespace ghostclaw::providers {

OpenRouterProvider::OpenRouterProvider(const std::string &api_key,
                                       std::shared_ptr<HttpClient> http_client)
    : CompatibleProvider(
          "openrouter", "https://openrouter.ai/api/v1", api_key, std::move(http_client), true,
          std::unordered_map<std::string, std::string>{{"HTTP-Referer", "https://ghostclaw.dev"},
                                                       {"X-Title", "GhostClaw"}}) {}

} // namespace ghostclaw::providers
