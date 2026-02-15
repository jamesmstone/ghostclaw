#include "ghostclaw/memory/embedder.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/memory/embedder_local.hpp"
#include "ghostclaw/memory/embedder_noop.hpp"
#include "ghostclaw/memory/embedder_openai.hpp"

#include <cstdlib>

namespace ghostclaw::memory {

std::unique_ptr<IEmbedder> create_embedder(const config::Config &config) {
  const std::string provider = common::to_lower(config.memory.embedding_provider);

  if (provider == "noop" || provider == "none") {
    return std::make_unique<NoopEmbedder>(config.memory.embedding_dimensions);
  }

  if (provider == "openai") {
    std::string key;
    if (config.api_key.has_value()) {
      key = *config.api_key;
    } else if (const char *env = std::getenv("GHOSTCLAW_API_KEY"); env != nullptr) {
      key = env;
    }

    if (!key.empty()) {
      return std::make_unique<OpenAiEmbedder>(key, config.memory.embedding_model,
                                              config.memory.embedding_dimensions);
    }
  }

  return std::make_unique<LocalEmbedder>();
}

} // namespace ghostclaw::memory
