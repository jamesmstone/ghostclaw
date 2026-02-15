#pragma once

#include "ghostclaw/common/result.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ghostclaw::security {

enum class ExecSecurity { Deny, Allowlist, Full };
enum class ExecAsk { Off, OnMiss, Always };

struct ApprovalRequest {
  std::string command;
  std::string session_id;
  std::chrono::seconds timeout{120};
};

enum class ApprovalDecision { AllowOnce, AllowAlways, Deny };

struct ApprovalPolicy {
  ExecSecurity security = ExecSecurity::Allowlist;
  ExecAsk ask = ExecAsk::OnMiss;
  std::vector<std::string> allowlist;
};

[[nodiscard]] std::string exec_security_to_string(ExecSecurity value);
[[nodiscard]] std::string exec_ask_to_string(ExecAsk value);
[[nodiscard]] std::string approval_decision_to_string(ApprovalDecision value);

[[nodiscard]] common::Result<ExecSecurity> exec_security_from_string(const std::string &value);
[[nodiscard]] common::Result<ExecAsk> exec_ask_from_string(const std::string &value);
[[nodiscard]] common::Result<ApprovalDecision>
approval_decision_from_string(const std::string &value);

class ApprovalStore {
public:
  explicit ApprovalStore(std::filesystem::path path);

  [[nodiscard]] common::Status load();
  [[nodiscard]] common::Status save() const;

  [[nodiscard]] bool contains(const std::string &command) const;
  [[nodiscard]] std::vector<std::string> entries() const;
  void add(const std::string &command);

private:
  [[nodiscard]] static std::string normalize_command(const std::string &command);

  std::filesystem::path path_;
  mutable std::mutex mutex_;
  std::vector<std::string> entries_;
};

class ApprovalSocketClient {
public:
  explicit ApprovalSocketClient(std::filesystem::path socket_path);

  [[nodiscard]] common::Result<ApprovalDecision> request(const ApprovalRequest &request) const;

private:
  std::filesystem::path socket_path_;
};

class ApprovalSocketServer {
public:
  using Handler = std::function<ApprovalDecision(const ApprovalRequest &)>;

  ApprovalSocketServer(std::filesystem::path socket_path, Handler handler);
  ~ApprovalSocketServer();

  [[nodiscard]] common::Status start();
  void stop();

private:
  void run_loop();

  std::filesystem::path socket_path_;
  Handler handler_;

  std::atomic<bool> running_{false};
  int listen_fd_ = -1;
  std::thread worker_;
};

class ApprovalManager {
public:
  explicit ApprovalManager(
      ApprovalPolicy policy = {},
      std::filesystem::path store_path = "~/.ghostclaw/exec-approval-allowlist.txt",
      std::filesystem::path socket_path = "~/.ghostclaw/exec-approvals.sock");

  void set_policy(ApprovalPolicy policy);
  [[nodiscard]] const ApprovalPolicy &policy() const;

  [[nodiscard]] bool is_allowlisted(const std::string &command) const;
  [[nodiscard]] bool needs_approval(const ApprovalRequest &request) const;

  [[nodiscard]] common::Result<ApprovalDecision> authorize(const ApprovalRequest &request);

private:
  [[nodiscard]] bool matches_allowlist(const std::string &command,
                                       const std::vector<std::string> &allowlist) const;

  ApprovalPolicy policy_;
  ApprovalStore store_;
  ApprovalSocketClient client_;
};

} // namespace ghostclaw::security
