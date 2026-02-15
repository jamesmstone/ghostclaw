#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"

#include <array>
#include <filesystem>
#include <string>

namespace ghostclaw::security {

using SecretKey = std::array<unsigned char, 32>;

[[nodiscard]] SecretKey generate_key();
[[nodiscard]] common::Result<std::filesystem::path> key_path();
[[nodiscard]] common::Result<SecretKey> load_or_create_key();

[[nodiscard]] common::Result<std::string> encrypt_secret(const SecretKey &key,
                                                         const std::string &plaintext);
[[nodiscard]] common::Result<std::string> decrypt_secret(const SecretKey &key,
                                                         const std::string &ciphertext);

void encrypt_config_secrets(config::Config &config, const SecretKey &key);
void decrypt_config_secrets(config::Config &config, const SecretKey &key);

} // namespace ghostclaw::security
