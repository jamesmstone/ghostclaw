#include "test_framework.hpp"

#include "ghostclaw/security/policy.hpp"
#include "tests/helpers/test_helpers.hpp"

#include <filesystem>

void register_security_integration_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;

  tests.push_back({"security_integration_blocks_escape_and_disallowed_commands", [] {
                     ghostclaw::testing::TempWorkspace workspace;
                     auto config = ghostclaw::testing::temp_config(workspace);
                     config.autonomy.workspace_only = true;
                     config.autonomy.allowed_commands = {"ls", "cat"};

                     auto policy_result = ghostclaw::security::SecurityPolicy::from_config(config);
                     require(policy_result.ok(), policy_result.error());
                     auto policy = policy_result.value();
                     policy.workspace_dir = workspace.path();

                     require(policy.is_command_allowed("ls -la"), "ls should be allowed");
                     require(!policy.is_command_allowed("rm -rf /"), "rm should be blocked");

                     const auto safe = ghostclaw::security::validate_path("notes.txt", policy);
                     require(safe.ok(), safe.error());

                     const auto escape =
                         ghostclaw::security::validate_path("../../../etc/passwd", policy);
                     require(!escape.ok(), "path escape should be rejected");
                   }});
}
