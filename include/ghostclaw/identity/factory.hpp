#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/identity/identity.hpp"

#include <filesystem>

namespace ghostclaw::identity {

[[nodiscard]] common::Result<Identity> load_identity(const config::IdentityConfig &config,
                                                     const std::filesystem::path &workspace);

} // namespace ghostclaw::identity
