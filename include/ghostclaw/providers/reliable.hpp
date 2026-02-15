#pragma once

#include "ghostclaw/providers/traits.hpp"
#include "ghostclaw/config/schema.hpp"

#include <memory>
#include <vector>

namespace ghostclaw::providers {

class ReliableProvider final : public Provider {
public:
  ReliableProvider(std::shared_ptr<Provider> primary, std::vector<std::shared_ptr<Provider>> fallbacks,
                   std::uint32_t max_retries, std::uint64_t backoff_ms);

  [[nodiscard]] common::Result<std::string>
  chat(const std::string &message, const std::string &model, double temperature) override;

  [[nodiscard]] common::Result<std::string>
  chat_with_system(const std::optional<std::string> &system_prompt, const std::string &message,
                   const std::string &model, double temperature) override;

  [[nodiscard]] common::Status warmup() override;
  [[nodiscard]] std::string name() const override;

private:
  [[nodiscard]] common::Result<std::string>
  execute_with_provider(const std::shared_ptr<Provider> &provider,
                        const std::optional<std::string> &system_prompt, const std::string &message,
                        const std::string &model, double temperature) const;

  std::shared_ptr<Provider> primary_;
  std::vector<std::shared_ptr<Provider>> fallbacks_;
  std::uint32_t max_retries_;
  std::uint64_t backoff_ms_;
};

} // namespace ghostclaw::providers
