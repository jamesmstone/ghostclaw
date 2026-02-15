#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace ghostclaw::security {

enum class PairingResultType { Success, Failed, LockedOut };

struct PairingResult {
  PairingResultType type = PairingResultType::Failed;
  std::string bearer_token;
  std::uint64_t retry_after_seconds = 0;
};

std::string generate_pairing_code();

class PairingState {
public:
  PairingState(std::string code, std::uint32_t max_attempts);

  PairingResult verify(const std::string &code);
  [[nodiscard]] const std::vector<std::string> &token_hashes() const;

private:
  std::string generate_bearer_token();
  static std::string sha256_hex(const std::string &text);

  std::optional<std::string> code_;
  std::uint32_t failed_attempts_ = 0;
  std::uint32_t max_attempts_ = 5;
  std::optional<std::chrono::steady_clock::time_point> locked_until_;
  std::vector<std::string> token_hashes_;
};

[[nodiscard]] bool constant_time_equals(const std::string &a, const std::string &b);

} // namespace ghostclaw::security
