#include "ghostclaw/identity/factory.hpp"

#include "ghostclaw/identity/aieos.hpp"
#include "ghostclaw/identity/openclaw.hpp"

namespace ghostclaw::identity {

common::Result<Identity> load_identity(const config::IdentityConfig &config,
                                       const std::filesystem::path &workspace) {
  const auto format = parse_identity_format(config.format);
  switch (format) {
  case IdentityFormat::OpenClaw:
    return OpenClawLoader::load(workspace);
  case IdentityFormat::Aieos:
    if (config.aieos_path.has_value()) {
      return AieosLoader::load_from_file(*config.aieos_path);
    }
    if (config.aieos_inline.has_value()) {
      return AieosLoader::load_from_string(*config.aieos_inline);
    }
    return common::Result<Identity>::failure(
        "aieos format requires identity.aieos_path or identity.aieos_inline");
  }
  return common::Result<Identity>::failure("unknown identity format");
}

} // namespace ghostclaw::identity
