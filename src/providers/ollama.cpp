#include "ghostclaw/providers/ollama.hpp"

namespace ghostclaw::providers {

OllamaProvider::OllamaProvider(std::shared_ptr<HttpClient> http_client)
    : CompatibleProvider("ollama", "http://localhost:11434/v1", "", std::move(http_client), false) {}

} // namespace ghostclaw::providers
