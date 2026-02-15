#include "ghostclaw/email/backend.hpp"

#include "ghostclaw/common/fs.hpp"

#include <array>
#include <cstdio>
#include <sstream>

namespace ghostclaw::email {

namespace {

std::string escape_applescript_string(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char ch : value) {
    if (ch == '"' || ch == '\\') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

std::string shell_single_quote(const std::string &value) {
  std::string out = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out.push_back(ch);
    }
  }
  out.push_back('\'');
  return out;
}

common::Result<std::string> run_capture_command(const std::string &command) {
  std::array<char, 4096> buffer{};
  std::string output;
  FILE *pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return common::Result<std::string>::failure("failed to launch command");
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }
  const int rc = pclose(pipe);
  if (rc != 0) {
    return common::Result<std::string>::failure("command failed with exit code " +
                                                std::to_string(rc));
  }
  return common::Result<std::string>::success(output);
}

std::vector<std::string> split_accounts(const std::string &raw) {
  std::vector<std::string> out;
  std::stringstream stream(raw);
  std::string token;
  while (std::getline(stream, token, ',')) {
    token = common::trim(token);
    if (!token.empty()) {
      out.push_back(token);
    }
  }
  return out;
}

class MailAppEmailBackend final : public IEmailBackend {
public:
  explicit MailAppEmailBackend(std::string default_account)
      : default_account_(std::move(default_account)) {}

  [[nodiscard]] std::string_view name() const override { return "mailapp"; }

  [[nodiscard]] common::Result<std::vector<EmailAccount>> list_accounts() override {
#if defined(__APPLE__)
    const auto listed = run_capture_command(
        "osascript -e 'tell application \"Mail\" to get name of every account'");
    if (!listed.ok()) {
      return common::Result<std::vector<EmailAccount>>::failure("failed to list Mail accounts: " +
                                                                listed.error());
    }

    std::vector<EmailAccount> out;
    const auto names = split_accounts(listed.value());
    for (const auto &name : names) {
      out.push_back(EmailAccount{.id = name, .label = name});
    }
    if (out.empty() && !common::trim(default_account_).empty()) {
      out.push_back(EmailAccount{.id = default_account_, .label = default_account_});
    }
    return common::Result<std::vector<EmailAccount>>::success(std::move(out));
#else
    return common::Result<std::vector<EmailAccount>>::failure(
        "capability_unavailable: Mail.app backend only works on macOS");
#endif
  }

  [[nodiscard]] common::Result<std::string> draft(const EmailMessage &message) override {
    if (common::trim(message.to).empty()) {
      return common::Result<std::string>::failure("to is required");
    }
    std::ostringstream out;
    out << "Draft email\n";
    out << "To: " << message.to << "\n";
    out << "Subject: " << message.subject << "\n";
    if (!common::trim(message.from_account).empty()) {
      out << "From: " << message.from_account << "\n";
    } else if (!common::trim(default_account_).empty()) {
      out << "From: " << default_account_ << "\n";
    }
    out << "Body:\n" << message.body;
    return common::Result<std::string>::success(out.str());
  }

  [[nodiscard]] common::Status send(const EmailMessage &message) override {
#if defined(__APPLE__)
    if (common::trim(message.to).empty()) {
      return common::Status::error("to is required");
    }
    if (common::trim(message.subject).empty()) {
      return common::Status::error("subject is required");
    }

    const std::string escaped_to = escape_applescript_string(message.to);
    const std::string escaped_subject = escape_applescript_string(message.subject);
    const std::string escaped_body = escape_applescript_string(message.body);
    std::string sender = common::trim(message.from_account);
    if (sender.empty()) {
      sender = common::trim(default_account_);
    }

    std::ostringstream script;
    script << "tell application \"Mail\"\n";
    script << "set newMessage to make new outgoing message with properties "
              "{subject:\""
           << escaped_subject << "\", content:\"" << escaped_body << "\" & return & return}\n";
    script << "tell newMessage\n";
    script << "make new to recipient at end of to recipients with properties {address:\""
           << escaped_to << "\"}\n";
    if (!sender.empty()) {
      script << "set sender to \"" << escape_applescript_string(sender) << "\"\n";
    }
    script << "send\n";
    script << "end tell\n";
    script << "end tell";

    const std::string command = "osascript -e " + shell_single_quote(script.str());
    const auto sent = run_capture_command(command);
    if (!sent.ok()) {
      return common::Status::error("failed to send via Mail.app: " + sent.error());
    }
    return common::Status::success();
#else
    (void)message;
    return common::Status::error("capability_unavailable: Mail.app backend only works on macOS");
#endif
  }

private:
  std::string default_account_;
};

} // namespace

std::unique_ptr<IEmailBackend> make_mailapp_email_backend(const config::Config &config) {
  return std::make_unique<MailAppEmailBackend>(config.email.default_account);
}

} // namespace ghostclaw::email
