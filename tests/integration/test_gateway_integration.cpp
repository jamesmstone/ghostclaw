#include "test_framework.hpp"

#include "ghostclaw/agent/engine.hpp"
#include "ghostclaw/gateway/server.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/tools/tool_registry.hpp"
#include "tests/helpers/test_helpers.hpp"

#include <cstring>
#include <memory>
#include <optional>
#include <string>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

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

#ifndef _WIN32
std::string http_get_localhost(std::uint16_t port, const std::string &path) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return "";
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  if (connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    close(sock);
    return "";
  }

  const std::string request = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  send(sock, request.data(), request.size(), 0);

  std::string response;
  char buffer[1024] = {0};
  while (true) {
    const ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
    if (n <= 0) {
      break;
    }
    response.append(buffer, static_cast<std::size_t>(n));
  }

  close(sock);
  return response;
}
#endif

} // namespace

void register_gateway_integration_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;

  tests.push_back({"gateway_integration_health_endpoint", [] {
#ifdef _WIN32
                     require(true, "gateway integration socket test skipped on Windows");
#else
                     ghostclaw::testing::TempWorkspace workspace;
                     auto config = ghostclaw::testing::temp_config(workspace);
                     config.gateway.require_pairing = false;

                     auto provider = std::make_shared<ghostclaw::testing::MockProvider>();
                     provider->set_response("ok");
                     ghostclaw::tools::ToolRegistry registry;
                     auto memory = std::make_unique<NullMemory>();
                     auto engine = std::make_shared<ghostclaw::agent::AgentEngine>(
                         config, provider, std::move(memory), std::move(registry), workspace.path());

                     ghostclaw::gateway::GatewayServer server(config, engine);
                     ghostclaw::gateway::GatewayOptions options;
                     options.host = "127.0.0.1";
                     options.port = 0;
                     auto started = server.start(options);
                     require(started.ok(), started.error());

                     const auto response = http_get_localhost(server.port(), "/health");
                     require(response.find("200 OK") != std::string::npos, "expected 200 response");
                     require(response.find("\"status\":\"ok\"") != std::string::npos,
                             "expected health json payload");

                     server.stop();
#endif
                   }});
}
