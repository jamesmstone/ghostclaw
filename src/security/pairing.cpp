#include "ghostclaw/security/pairing.hpp"

#include <iomanip>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sstream>

namespace ghostclaw::security {

namespace {

constexpr auto LOCKOUT_DURATION = std::chrono::minutes(5);

std::string random_hex(std::size_t bytes) {
  std::vector<unsigned char> data(bytes);
  RAND_bytes(data.data(), static_cast<int>(data.size()));

  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const auto byte : data) {
    stream << std::setw(2) << static_cast<int>(byte);
  }
  return stream.str();
}

std::string sha256_hex_impl(const std::string &text) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(text.data()), text.size(), digest);

  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (unsigned char c : digest) {
    stream << std::setw(2) << static_cast<int>(c);
  }
  return stream.str();
}

} // namespace

std::string generate_pairing_code() {
  std::uint32_t value = 0;
  RAND_bytes(reinterpret_cast<unsigned char *>(&value), sizeof(value));
  value %= 1'000'000;

  std::ostringstream stream;
  stream << std::setw(6) << std::setfill('0') << value;
  return stream.str();
}

PairingState::PairingState(std::string code, const std::uint32_t max_attempts)
    : code_(std::move(code)), max_attempts_(max_attempts) {}

PairingResult PairingState::verify(const std::string &code) {
  const auto now = std::chrono::steady_clock::now();
  if (locked_until_.has_value() && now < *locked_until_) {
    return PairingResult{.type = PairingResultType::LockedOut,
                         .retry_after_seconds = static_cast<std::uint64_t>(
                             std::chrono::duration_cast<std::chrono::seconds>(*locked_until_ - now)
                                 .count())};
  }

  if (code_.has_value() && constant_time_equals(*code_, code)) {
    failed_attempts_ = 0;
    locked_until_.reset();

    std::string token = generate_bearer_token();
    token_hashes_.push_back(sha256_hex(token));
    code_.reset();

    return PairingResult{.type = PairingResultType::Success, .bearer_token = std::move(token)};
  }

  ++failed_attempts_;
  if (failed_attempts_ >= max_attempts_) {
    locked_until_ = now + LOCKOUT_DURATION;
    return PairingResult{.type = PairingResultType::LockedOut,
                         .retry_after_seconds = static_cast<std::uint64_t>(
                             std::chrono::duration_cast<std::chrono::seconds>(LOCKOUT_DURATION)
                                 .count())};
  }

  return PairingResult{.type = PairingResultType::Failed};
}

const std::vector<std::string> &PairingState::token_hashes() const { return token_hashes_; }

std::string PairingState::generate_bearer_token() { return random_hex(16); }

std::string PairingState::sha256_hex(const std::string &text) {
  return sha256_hex_impl(text);
}

bool constant_time_equals(const std::string &a, const std::string &b) {
  const auto hash_a = sha256_hex_impl(a);
  const auto hash_b = sha256_hex_impl(b);

  const std::size_t max_size = std::max(hash_a.size(), hash_b.size());
  unsigned char diff = static_cast<unsigned char>(hash_a.size() ^ hash_b.size());

  for (std::size_t i = 0; i < max_size; ++i) {
    const unsigned char lhs = i < hash_a.size() ? static_cast<unsigned char>(hash_a[i]) : 0;
    const unsigned char rhs = i < hash_b.size() ? static_cast<unsigned char>(hash_b[i]) : 0;
    diff |= static_cast<unsigned char>(lhs ^ rhs);
  }

  return diff == 0;
}

} // namespace ghostclaw::security
