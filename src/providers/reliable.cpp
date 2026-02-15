#include "ghostclaw/providers/reliable.hpp"

#include <chrono>
#include <thread>

namespace ghostclaw::providers {

ReliableProvider::ReliableProvider(std::shared_ptr<Provider> primary,
                                   std::vector<std::shared_ptr<Provider>> fallbacks,
                                   const std::uint32_t max_retries, const std::uint64_t backoff_ms)
    : primary_(std::move(primary)), fallbacks_(std::move(fallbacks)), max_retries_(max_retries),
      backoff_ms_(backoff_ms) {}

common::Result<std::string> ReliableProvider::chat(const std::string &message,
                                                    const std::string &model,
                                                    const double temperature) {
  return chat_with_system(std::nullopt, message, model, temperature);
}

common::Result<std::string>
ReliableProvider::execute_with_provider(const std::shared_ptr<Provider> &provider,
                                        const std::optional<std::string> &system_prompt,
                                        const std::string &message, const std::string &model,
                                        const double temperature) const {
  std::string last_error;

  for (std::uint32_t attempt = 0; attempt <= max_retries_; ++attempt) {
    auto result = provider->chat_with_system(system_prompt, message, model, temperature);
    if (result.ok()) {
      return result;
    }

    last_error = result.error();
    if (attempt < max_retries_) {
      const std::uint64_t delay = backoff_ms_ * (1ULL << attempt);
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
  }

  return common::Result<std::string>::failure(last_error);
}

common::Result<std::string>
ReliableProvider::chat_with_system(const std::optional<std::string> &system_prompt,
                                   const std::string &message, const std::string &model,
                                   const double temperature) {
  auto result = execute_with_provider(primary_, system_prompt, message, model, temperature);
  if (result.ok()) {
    return result;
  }

  std::string last_error = result.error();
  for (const auto &fallback : fallbacks_) {
    result = execute_with_provider(fallback, system_prompt, message, model, temperature);
    if (result.ok()) {
      return result;
    }
    last_error = result.error();
  }

  return common::Result<std::string>::failure(last_error);
}

common::Status ReliableProvider::warmup() {
  if (primary_) {
    const auto status = primary_->warmup();
    if (!status.ok()) {
      return status;
    }
  }

  for (const auto &fallback : fallbacks_) {
    if (fallback) {
      const auto _status = fallback->warmup();
      (void)_status;
    }
  }

  return common::Status::success();
}

std::string ReliableProvider::name() const { return "reliable"; }

} // namespace ghostclaw::providers
