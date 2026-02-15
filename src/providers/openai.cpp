#include "ghostclaw/providers/openai.hpp"

namespace ghostclaw::providers {

OpenAiProvider::OpenAiProvider(const std::string &api_key, std::shared_ptr<HttpClient> http_client)
    : CompatibleProvider("openai", "https://api.openai.com/v1", api_key, std::move(http_client), true) {}

} // namespace ghostclaw::providers
