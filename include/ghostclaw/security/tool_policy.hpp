#pragma once

#include "ghostclaw/common/result.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ghostclaw::security {

enum class ToolProfile { Minimal, Coding, Messaging, Full };

struct ToolPolicy {
  std::vector<std::string> allow;
  std::vector<std::string> deny;

  [[nodiscard]] bool empty() const;
};

struct ToolPolicyPipelineStep {
  std::optional<ToolPolicy> policy;
  std::string label;
};

struct ToolPolicyRequest {
  std::string tool_name;
  ToolProfile profile = ToolProfile::Full;
  std::string provider;
  std::string agent_id;
  std::string channel_id;
  std::string group_id;
};

struct ToolPolicyDecision {
  bool allowed = true;
  std::string blocked_by;
  std::string reason;
  std::vector<std::string> trace;
};

class ToolPolicyPipeline {
public:
  ToolPolicyPipeline();

  void set_profile_policy(ToolProfile profile, ToolPolicy policy);
  void set_provider_profile_policy(const std::string &provider, ToolProfile profile, ToolPolicy policy);
  void set_global_policy(ToolPolicy policy);
  void clear_global_policy();
  void set_global_provider_policy(const std::string &provider, ToolPolicy policy);
  void set_agent_policy(const std::string &agent_id, ToolPolicy policy);
  void set_agent_provider_policy(const std::string &agent_id, const std::string &provider,
                                 ToolPolicy policy);
  void set_group_policy(const std::string &channel_id, const std::string &group_id,
                        ToolPolicy policy);

  [[nodiscard]] ToolPolicyDecision evaluate_tool(const ToolPolicyRequest &request) const;
  [[nodiscard]] ToolPolicyDecision evaluate_tool(std::string_view tool_name,
                                                 const ToolPolicyRequest &request) const;
  [[nodiscard]] std::vector<std::string> filter_tools(const std::vector<std::string> &tool_names,
                                                      const ToolPolicyRequest &request) const;

  [[nodiscard]] std::vector<ToolPolicyPipelineStep>
  build_default_pipeline_steps(const ToolPolicyRequest &request) const;

  [[nodiscard]] static std::vector<std::string> expand_entries(const std::vector<std::string> &entries);
  [[nodiscard]] static std::vector<std::string> expand_group(std::string_view group_name);
  [[nodiscard]] static std::string normalize_tool_name(std::string_view name);
  [[nodiscard]] static common::Result<ToolProfile> profile_from_string(const std::string &value);
  [[nodiscard]] static std::string profile_to_string(ToolProfile profile);
  [[nodiscard]] static ToolPolicy default_profile_policy(ToolProfile profile);

private:
  [[nodiscard]] static std::string make_group_key(std::string_view channel_id,
                                                  std::string_view group_id);
  [[nodiscard]] static bool matches_pattern(std::string_view name, std::string_view pattern);
  [[nodiscard]] static std::string normalize_pattern(std::string_view pattern);

  std::map<ToolProfile, ToolPolicy> profile_policies_;
  std::unordered_map<std::string, std::map<ToolProfile, ToolPolicy>> provider_profile_policies_;

  std::optional<ToolPolicy> global_policy_;
  std::unordered_map<std::string, ToolPolicy> global_provider_policies_;
  std::unordered_map<std::string, ToolPolicy> agent_policies_;
  std::unordered_map<std::string, std::unordered_map<std::string, ToolPolicy>>
      agent_provider_policies_;
  std::unordered_map<std::string, ToolPolicy> group_policies_;
};

} // namespace ghostclaw::security
