#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ghostclaw::email {

struct EmailAccount {
  std::string id;
  std::string label;
};

struct EmailMessage {
  std::string to;
  std::string subject;
  std::string body;
  std::string from_account;
};

class IEmailBackend {
public:
  virtual ~IEmailBackend() = default;

  [[nodiscard]] virtual std::string_view name() const = 0;
  [[nodiscard]] virtual common::Result<std::vector<EmailAccount>> list_accounts() = 0;
  [[nodiscard]] virtual common::Result<std::string> draft(const EmailMessage &message) = 0;
  [[nodiscard]] virtual common::Status send(const EmailMessage &message) = 0;
};

[[nodiscard]] std::unique_ptr<IEmailBackend> make_email_backend(const config::Config &config);

} // namespace ghostclaw::email
