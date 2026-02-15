#include "ghostclaw/auth/oauth.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"
#include "ghostclaw/config/config.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace ghostclaw::auth {

namespace {

constexpr std::uint64_t HTTP_TIMEOUT_MS = 30000;
constexpr int MAX_POLL_ATTEMPTS = 180; // 15 minutes at 5s intervals
constexpr std::int64_t EXPIRY_BUFFER_SECS = 60; // refresh 60s before actual expiry

std::filesystem::path auth_json_path() {
  auto dir = config::config_dir();
  if (!dir.ok()) {
    return {};
  }
  return dir.value() / "auth.json";
}

std::string url_encode_component(const std::string &value) {
  std::ostringstream encoded;
  for (const unsigned char ch : value) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      encoded << static_cast<char>(ch);
    } else {
      encoded << '%';
      encoded << "0123456789ABCDEF"[ch >> 4];
      encoded << "0123456789ABCDEF"[ch & 0x0F];
    }
  }
  return encoded.str();
}

std::int64_t now_unix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void set_file_permissions_0600(const std::filesystem::path &path) {
#ifndef _WIN32
  chmod(path.c_str(), 0600);
#else
  (void)path;
#endif
}

providers::HttpResponse post_json_body(providers::HttpClient &http, const std::string &url,
                                       const std::string &body) {
  std::unordered_map<std::string, std::string> headers = {
      {"Content-Type", "application/json"},
  };
  return http.post_json(url, headers, body, HTTP_TIMEOUT_MS);
}

providers::HttpResponse post_form_body(providers::HttpClient &http, const std::string &url,
                                       const std::string &body) {
  std::unordered_map<std::string, std::string> headers = {
      {"Content-Type", "application/x-www-form-urlencoded"},
  };
  return http.post_json(url, headers, body, HTTP_TIMEOUT_MS);
}

} // namespace

// ── Token storage ─────────────────────────────────────────────────────────────

common::Result<OAuthTokens> load_tokens() {
  const auto path = auth_json_path();
  if (path.empty()) {
    return common::Result<OAuthTokens>::failure("unable to determine config directory");
  }
  if (!std::filesystem::exists(path)) {
    return common::Result<OAuthTokens>::failure("auth.json not found");
  }

  std::ifstream file(path);
  if (!file) {
    return common::Result<OAuthTokens>::failure("unable to open auth.json");
  }

  std::ostringstream buf;
  buf << file.rdbuf();
  const std::string json = buf.str();

  OAuthTokens tokens;
  tokens.access_token = common::json_get_string(json, "access_token");
  tokens.refresh_token = common::json_get_string(json, "refresh_token");
  tokens.id_token = common::json_get_string(json, "id_token");

  const std::string expires_str = common::json_get_number(json, "expires_at");
  if (!expires_str.empty()) {
    try {
      tokens.expires_at = std::stoll(expires_str);
    } catch (...) {
      tokens.expires_at = 0;
    }
  }

  if (tokens.access_token.empty() && tokens.refresh_token.empty()) {
    return common::Result<OAuthTokens>::failure("auth.json contains no tokens");
  }

  return common::Result<OAuthTokens>::success(std::move(tokens));
}

common::Status save_tokens(const OAuthTokens &tokens) {
  const auto path = auth_json_path();
  if (path.empty()) {
    return common::Status::error("unable to determine config directory");
  }

  const std::filesystem::path tmp_path = path.string() + ".tmp";

  std::ofstream file(tmp_path, std::ios::trunc);
  if (!file) {
    return common::Status::error("unable to write auth.json.tmp");
  }

  file << "{\n";
  file << "  \"access_token\": \"" << common::json_escape(tokens.access_token) << "\",\n";
  file << "  \"refresh_token\": \"" << common::json_escape(tokens.refresh_token) << "\",\n";
  file << "  \"id_token\": \"" << common::json_escape(tokens.id_token) << "\",\n";
  file << "  \"expires_at\": " << tokens.expires_at << "\n";
  file << "}\n";

  file.close();
  if (!file) {
    return common::Status::error("failed writing auth.json.tmp");
  }

  set_file_permissions_0600(tmp_path);

  std::error_code ec;
  std::filesystem::rename(tmp_path, path, ec);
  if (ec) {
    return common::Status::error("failed to atomically replace auth.json: " + ec.message());
  }

  return common::Status::success();
}

common::Status delete_tokens() {
  const auto path = auth_json_path();
  if (path.empty()) {
    return common::Status::error("unable to determine config directory");
  }

  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    std::filesystem::remove(path, ec);
    if (ec) {
      return common::Status::error("failed to remove auth.json: " + ec.message());
    }
  }

  return common::Status::success();
}

bool has_valid_tokens() {
  auto tokens = load_tokens();
  if (!tokens.ok()) {
    return false;
  }
  // If we have a refresh token, we can always get a valid access token
  if (!tokens.value().refresh_token.empty()) {
    return true;
  }
  // Otherwise check if access token is still valid
  if (tokens.value().expires_at > 0 && now_unix() >= tokens.value().expires_at) {
    return false;
  }
  return !tokens.value().access_token.empty();
}

// ── Device code flow ──────────────────────────────────────────────────────────

common::Result<DeviceCodeResponse> request_device_code(providers::HttpClient &http) {
  const std::string body =
      "{\"client_id\": \"" + std::string(OPENAI_CLIENT_ID) + "\"}";

  auto response = post_json_body(http, OPENAI_DEVICE_CODE_URL, body);

  if (response.network_error) {
    return common::Result<DeviceCodeResponse>::failure(
        "network error requesting device code: " + response.network_error_message);
  }
  if (response.status != 200) {
    return common::Result<DeviceCodeResponse>::failure(
        "device code request failed (HTTP " + std::to_string(response.status) +
        "): " + response.body);
  }

  DeviceCodeResponse result;
  result.device_auth_id = common::json_get_string(response.body, "device_auth_id");
  result.user_code = common::json_get_string(response.body, "user_code");

  const std::string interval_str = common::json_get_number(response.body, "interval");
  if (!interval_str.empty()) {
    try {
      result.interval = std::stoull(interval_str);
    } catch (...) {
      result.interval = 5;
    }
  }
  // Clamp interval to reasonable range
  if (result.interval < 1) {
    result.interval = 1;
  }
  if (result.interval > 30) {
    result.interval = 30;
  }

  if (result.device_auth_id.empty() || result.user_code.empty()) {
    return common::Result<DeviceCodeResponse>::failure(
        "invalid device code response: missing device_auth_id or user_code");
  }

  return common::Result<DeviceCodeResponse>::success(std::move(result));
}

common::Result<DeviceAuthSuccess>
poll_for_authorization(providers::HttpClient &http,
                       const std::string &device_auth_id,
                       const std::string &user_code) {
  const std::string body =
      "{\"device_auth_id\": \"" + common::json_escape(device_auth_id) +
      "\", \"user_code\": \"" + common::json_escape(user_code) + "\"}";

  auto response = post_json_body(http, OPENAI_DEVICE_POLL_URL, body);

  if (response.network_error) {
    return common::Result<DeviceAuthSuccess>::failure(
        "network error polling authorization: " + response.network_error_message);
  }

  // 403/404 means still pending -- return empty success
  if (response.status == 403 || response.status == 404) {
    return common::Result<DeviceAuthSuccess>::success(DeviceAuthSuccess{});
  }

  if (response.status != 200) {
    return common::Result<DeviceAuthSuccess>::failure(
        "authorization poll failed (HTTP " + std::to_string(response.status) +
        "): " + response.body);
  }

  DeviceAuthSuccess result;
  result.authorization_code = common::json_get_string(response.body, "authorization_code");
  result.code_verifier = common::json_get_string(response.body, "code_verifier");

  if (result.authorization_code.empty()) {
    return common::Result<DeviceAuthSuccess>::failure(
        "authorization response missing authorization_code");
  }

  return common::Result<DeviceAuthSuccess>::success(std::move(result));
}

common::Result<OAuthTokens>
exchange_code(providers::HttpClient &http,
              const std::string &authorization_code,
              const std::string &code_verifier) {
  std::string body = "grant_type=authorization_code";
  body += "&code=" + url_encode_component(authorization_code);
  body += "&redirect_uri=" + url_encode_component(OPENAI_REDIRECT_URI);
  body += "&client_id=" + url_encode_component(OPENAI_CLIENT_ID);
  body += "&code_verifier=" + url_encode_component(code_verifier);

  auto response = post_form_body(http, OPENAI_TOKEN_URL, body);

  if (response.network_error) {
    return common::Result<OAuthTokens>::failure(
        "network error exchanging code: " + response.network_error_message);
  }
  if (response.status != 200) {
    return common::Result<OAuthTokens>::failure(
        "token exchange failed (HTTP " + std::to_string(response.status) +
        "): " + response.body);
  }

  OAuthTokens tokens;
  tokens.access_token = common::json_get_string(response.body, "access_token");
  tokens.refresh_token = common::json_get_string(response.body, "refresh_token");
  tokens.id_token = common::json_get_string(response.body, "id_token");

  const std::string expires_in_str = common::json_get_number(response.body, "expires_in");
  if (!expires_in_str.empty()) {
    try {
      tokens.expires_at = now_unix() + std::stoll(expires_in_str);
    } catch (...) {
      tokens.expires_at = 0;
    }
  }

  if (tokens.access_token.empty()) {
    return common::Result<OAuthTokens>::failure("token exchange returned no access_token");
  }

  return common::Result<OAuthTokens>::success(std::move(tokens));
}

common::Status run_device_login(providers::HttpClient &http) {
  std::cout << "\nRequesting device code from OpenAI...\n";

  auto device_code = request_device_code(http);
  if (!device_code.ok()) {
    return common::Status::error(device_code.error());
  }

  const auto &dc = device_code.value();

  std::cout << "\n";
  std::cout << "  To log in, visit:  " << OPENAI_DEVICE_VERIFY_URL << "\n";
  std::cout << "  Enter this code:   " << dc.user_code << "\n";
  std::cout << "\n";
  std::cout << "Waiting for authorization...\n";

  for (int attempt = 0; attempt < MAX_POLL_ATTEMPTS; ++attempt) {
    std::this_thread::sleep_for(std::chrono::seconds(dc.interval));

    auto poll = poll_for_authorization(http, dc.device_auth_id, dc.user_code);
    if (!poll.ok()) {
      return common::Status::error(poll.error());
    }

    const auto &auth = poll.value();
    if (auth.authorization_code.empty()) {
      // Still pending
      continue;
    }

    // Got authorization -- exchange for tokens
    std::cout << "Authorization received. Exchanging for tokens...\n";

    auto tokens = exchange_code(http, auth.authorization_code, auth.code_verifier);
    if (!tokens.ok()) {
      return common::Status::error(tokens.error());
    }

    auto saved = save_tokens(tokens.value());
    if (!saved.ok()) {
      return common::Status::error("login succeeded but failed to save tokens: " + saved.error());
    }

    std::cout << "Login successful! Tokens saved to " << auth_json_path().string() << "\n";
    return common::Status::success();
  }

  return common::Status::error("login timed out waiting for authorization");
}

// ── Token management ──────────────────────────────────────────────────────────

common::Result<OAuthTokens>
refresh_access_token(providers::HttpClient &http, const std::string &refresh_token) {
  std::string body = "grant_type=refresh_token";
  body += "&refresh_token=" + url_encode_component(refresh_token);
  body += "&client_id=" + url_encode_component(OPENAI_CLIENT_ID);

  auto response = post_form_body(http, OPENAI_TOKEN_URL, body);

  if (response.network_error) {
    return common::Result<OAuthTokens>::failure(
        "network error refreshing token: " + response.network_error_message);
  }
  if (response.status != 200) {
    return common::Result<OAuthTokens>::failure(
        "token refresh failed (HTTP " + std::to_string(response.status) +
        "): " + response.body);
  }

  OAuthTokens tokens;
  tokens.access_token = common::json_get_string(response.body, "access_token");
  tokens.refresh_token = common::json_get_string(response.body, "refresh_token");
  tokens.id_token = common::json_get_string(response.body, "id_token");

  // Keep the old refresh token if new one not provided
  if (tokens.refresh_token.empty()) {
    tokens.refresh_token = refresh_token;
  }

  const std::string expires_in_str = common::json_get_number(response.body, "expires_in");
  if (!expires_in_str.empty()) {
    try {
      tokens.expires_at = now_unix() + std::stoll(expires_in_str);
    } catch (...) {
      tokens.expires_at = 0;
    }
  }

  if (tokens.access_token.empty()) {
    return common::Result<OAuthTokens>::failure("refresh returned no access_token");
  }

  return common::Result<OAuthTokens>::success(std::move(tokens));
}

common::Result<std::string> get_valid_access_token(providers::HttpClient &http) {
  auto loaded = load_tokens();
  if (!loaded.ok()) {
    return common::Result<std::string>::failure(loaded.error());
  }

  auto &tokens = loaded.value();

  // Check if token is still valid (with buffer)
  const bool expired =
      tokens.expires_at > 0 && now_unix() >= (tokens.expires_at - EXPIRY_BUFFER_SECS);

  if (!expired && !tokens.access_token.empty()) {
    return common::Result<std::string>::success(tokens.access_token);
  }

  // Token expired or missing -- try to refresh
  if (tokens.refresh_token.empty()) {
    return common::Result<std::string>::failure(
        "access token expired and no refresh token available");
  }

  auto refreshed = refresh_access_token(http, tokens.refresh_token);
  if (!refreshed.ok()) {
    return common::Result<std::string>::failure(
        "failed to refresh access token: " + refreshed.error());
  }

  auto saved = save_tokens(refreshed.value());
  if (!saved.ok()) {
    // Token refresh succeeded but save failed -- still return the token
    return common::Result<std::string>::success(refreshed.value().access_token);
  }

  return common::Result<std::string>::success(refreshed.value().access_token);
}

} // namespace ghostclaw::auth
