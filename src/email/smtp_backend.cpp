#include "ghostclaw/email/backend.hpp"

#include "ghostclaw/common/fs.hpp"

#include <curl/curl.h>

#include <cstring>
#include <sstream>

namespace ghostclaw::email {

namespace {

struct UploadPayload {
  std::string data;
  std::size_t offset = 0;
};

std::size_t upload_callback(char *buffer, std::size_t size, std::size_t nitems, void *userdata) {
  auto *payload = static_cast<UploadPayload *>(userdata);
  const std::size_t capacity = size * nitems;
  if (payload == nullptr || capacity == 0 || payload->offset >= payload->data.size()) {
    return 0;
  }

  const std::size_t remaining = payload->data.size() - payload->offset;
  const std::size_t to_copy = std::min(remaining, capacity);
  std::memcpy(buffer, payload->data.data() + payload->offset, to_copy);
  payload->offset += to_copy;
  return to_copy;
}

std::string format_email_payload(const EmailMessage &message, const std::string &from) {
  std::ostringstream out;
  out << "To: " << message.to << "\r\n";
  out << "From: " << from << "\r\n";
  out << "Subject: " << message.subject << "\r\n";
  out << "MIME-Version: 1.0\r\n";
  out << "Content-Type: text/plain; charset=utf-8\r\n";
  out << "\r\n";
  out << message.body << "\r\n";
  return out.str();
}

class SmtpEmailBackend final : public IEmailBackend {
public:
  explicit SmtpEmailBackend(config::EmailConfig email_config)
      : email_config_(std::move(email_config)) {}

  [[nodiscard]] std::string_view name() const override { return "smtp"; }

  [[nodiscard]] common::Result<std::vector<EmailAccount>> list_accounts() override {
    std::vector<EmailAccount> out;
    if (email_config_.smtp.has_value()) {
      const auto &smtp = *email_config_.smtp;
      if (!common::trim(smtp.username).empty()) {
        out.push_back(EmailAccount{.id = smtp.username, .label = "SMTP: " + smtp.username});
      }
    }
    if (!common::trim(email_config_.default_account).empty()) {
      out.push_back(
          EmailAccount{.id = email_config_.default_account, .label = email_config_.default_account});
    }
    return common::Result<std::vector<EmailAccount>>::success(std::move(out));
  }

  [[nodiscard]] common::Result<std::string> draft(const EmailMessage &message) override {
    if (common::trim(message.to).empty()) {
      return common::Result<std::string>::failure("to is required");
    }
    std::ostringstream out;
    out << "Draft email\n";
    out << "To: " << message.to << "\n";
    out << "Subject: " << message.subject << "\n";
    out << "From: " << resolve_from_account(message) << "\n";
    out << "Body:\n" << message.body;
    return common::Result<std::string>::success(out.str());
  }

  [[nodiscard]] common::Status send(const EmailMessage &message) override {
    if (common::trim(message.to).empty()) {
      return common::Status::error("to is required");
    }
    if (common::trim(message.subject).empty()) {
      return common::Status::error("subject is required");
    }
    if (!email_config_.smtp.has_value()) {
      return common::Status::error(
          "smtp backend requires [email.smtp] configuration (host/port/username/password)");
    }

    const auto &smtp = *email_config_.smtp;
    if (common::trim(smtp.host).empty()) {
      return common::Status::error("smtp host is required");
    }
    if (common::trim(smtp.username).empty() || common::trim(smtp.password).empty()) {
      return common::Status::error("smtp username/password are required");
    }

    const std::string from = resolve_from_account(message);
    std::string url = smtp.tls ? "smtps://" : "smtp://";
    url += smtp.host + ":" + std::to_string(smtp.port);

    CURL *curl = curl_easy_init();
    if (curl == nullptr) {
      return common::Status::error("failed to initialize curl for smtp");
    }

    UploadPayload payload;
    payload.data = format_email_payload(message, from);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, smtp.username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, smtp.password.c_str());
    curl_easy_setopt(curl, CURLOPT_USE_SSL, smtp.tls ? CURLUSESSL_ALL : CURLUSESSL_NONE);
    const std::string mail_from = "<" + from + ">";
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mail_from.c_str());
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, upload_callback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &payload);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);

    struct curl_slist *recipients = nullptr;
    recipients = curl_slist_append(recipients, ("<" + message.to + ">").c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    const auto code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
      return common::Status::error("smtp send failed: " + std::string(curl_easy_strerror(code)));
    }
    if (status >= 400) {
      return common::Status::error("smtp server rejected message with status " +
                                   std::to_string(status));
    }
    return common::Status::success();
  }

private:
  [[nodiscard]] std::string resolve_from_account(const EmailMessage &message) const {
    if (!common::trim(message.from_account).empty()) {
      return common::trim(message.from_account);
    }
    if (!common::trim(email_config_.default_account).empty()) {
      return common::trim(email_config_.default_account);
    }
    if (email_config_.smtp.has_value()) {
      return common::trim(email_config_.smtp->username);
    }
    return "";
  }

  config::EmailConfig email_config_;
};

} // namespace

std::unique_ptr<IEmailBackend> make_smtp_email_backend(const config::Config &config) {
  return std::make_unique<SmtpEmailBackend>(config.email);
}

} // namespace ghostclaw::email
