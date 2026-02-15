#include "test_framework.hpp"

#include "ghostclaw/agent/engine.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/tools/tool_registry.hpp"
#include "tests/helpers/test_helpers.hpp"

#include <memory>
#include <optional>

namespace {

class NullMemory final : public ghostclaw::memory::IMemory {
public:
  [[nodiscard]] std::string_view name() const override { return "null"; }
  [[nodiscard]] ghostclaw::common::Status store(const std::string &, const std::string &,
                                                ghostclaw::memory::MemoryCategory) override {
    return ghostclaw::common::Status::success();
  }
  [[nodiscard]] ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>
  recall(const std::string &, std::size_t) override {
    return ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>::success({});
  }
  [[nodiscard]] ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>
  get(const std::string &) override {
    return ghostclaw::common::Result<std::optional<ghostclaw::memory::MemoryEntry>>::success(std::nullopt);
  }
  [[nodiscard]] ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>
  list(std::optional<ghostclaw::memory::MemoryCategory>) override {
    return ghostclaw::common::Result<std::vector<ghostclaw::memory::MemoryEntry>>::success({});
  }
  [[nodiscard]] ghostclaw::common::Result<bool> forget(const std::string &) override {
    return ghostclaw::common::Result<bool>::success(false);
  }
  [[nodiscard]] ghostclaw::common::Result<std::size_t> count() override {
    return ghostclaw::common::Result<std::size_t>::success(0);
  }
  [[nodiscard]] ghostclaw::common::Status reindex() override {
    return ghostclaw::common::Status::success();
  }
  [[nodiscard]] bool health_check() override { return true; }
  [[nodiscard]] ghostclaw::memory::MemoryStats stats() override { return {}; }
};

} // namespace

void register_agent_integration_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;

  tests.push_back({"agent_integration_full_loop_with_mock_provider", [] {
                     ghostclaw::testing::TempWorkspace workspace;
                     workspace.create_file("SOUL.md", "# Soul\nHelpful.");

                     auto config = ghostclaw::testing::temp_config(workspace);

                     auto provider = std::make_shared<ghostclaw::testing::MockProvider>();
                     provider->set_response("Hello! I'm here to help.");

                     ghostclaw::tools::ToolRegistry registry;
                     auto memory = std::make_unique<NullMemory>();

                     ghostclaw::agent::AgentEngine engine(config, provider, std::move(memory),
                                                          std::move(registry), workspace.path());
                     auto result = engine.run("Hello");
                     require(result.ok(), result.error());
                     require(result.value().content.find("help") != std::string::npos,
                             "agent should return mock response");
                   }});
}
