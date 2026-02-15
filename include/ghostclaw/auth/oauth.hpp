#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/providers/traits.hpp"

#include <cstdint>
#include <string>

namespace ghostclaw::auth {

// ── Constants ─────────────────────────────────────────────────────────────────

inline constexpr const char *OPENAI_AUTH_BASE = "https://auth.openai.com";
inline constexpr const char *OPENAI_CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann";
inline constexpr const char *OPENAI_DEVICE_CODE_URL =
    "https://auth.openai.com/api/accounts/deviceauth/usercode";
inline constexpr const char *OPENAI_DEVICE_POLL_URL =
    "https://auth.openai.com/api/accounts/deviceauth/token";
inline constexpr const char *OPENAI_TOKEN_URL =
    "https://auth.openai.com/oauth/token";
inline constexpr const char *OPENAI_DEVICE_VERIFY_URL =
    "https://auth.openai.com/codex/device";
inline constexpr const char *OPENAI_REDIRECT_URI =
    "https://auth.openai.com/deviceauth/callback";

// ── Types ─────────────────────────────────────────────────────────────────────

struct OAuthTokens {
  std::string access_token;
  std::string refresh_token;
  std::string id_token;
  std::int64_t expires_at = 0; // Unix timestamp (seconds)
};

struct DeviceCodeResponse {
  std::string device_auth_id;
  std::string user_code;
  std::uint64_t interval = 5; // seconds between polls
};

struct DeviceAuthSuccess {
  std::string authorization_code;
  std::string code_verifier;
};

// ── Token storage ─────────────────────────────────────────────────────────────

/// Load saved OAuth tokens from ~/.ghostclaw/auth.json.
[[nodiscard]] common::Result<OAuthTokens> load_tokens();

/// Save OAuth tokens to ~/.ghostclaw/auth.json (0600 permissions).
[[nodiscard]] common::Status save_tokens(const OAuthTokens &tokens);

/// Delete ~/.ghostclaw/auth.json.
[[nodiscard]] common::Status delete_tokens();

/// Check if valid (non-expired) tokens exist on disk.
[[nodiscard]] bool has_valid_tokens();

// ── Device code flow ──────────────────────────────────────────────────────────

/// Step 1: Request a device code from OpenAI.
[[nodiscard]] common::Result<DeviceCodeResponse>
request_device_code(providers::HttpClient &http);

/// Step 2: Poll for user authorization (returns empty on pending, filled on success).
[[nodiscard]] common::Result<DeviceAuthSuccess>
poll_for_authorization(providers::HttpClient &http,
                       const std::string &device_auth_id,
                       const std::string &user_code);

/// Step 3: Exchange authorization code for access/refresh tokens.
[[nodiscard]] common::Result<OAuthTokens>
exchange_code(providers::HttpClient &http,
              const std::string &authorization_code,
              const std::string &code_verifier);

/// Full interactive device login flow (prints instructions, polls, saves tokens).
[[nodiscard]] common::Status run_device_login(providers::HttpClient &http);

// ── Token management ──────────────────────────────────────────────────────────

/// Refresh the access token using the stored refresh token.
[[nodiscard]] common::Result<OAuthTokens>
refresh_access_token(providers::HttpClient &http, const std::string &refresh_token);

/// Get a valid access token (loads, auto-refreshes if needed).
[[nodiscard]] common::Result<std::string>
get_valid_access_token(providers::HttpClient &http);

} // namespace ghostclaw::auth
