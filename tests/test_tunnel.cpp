#include "test_framework.hpp"

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/tunnel/custom.hpp"
#include "ghostclaw/tunnel/factory.hpp"
#include "ghostclaw/tunnel/none.hpp"

#include <filesystem>
#include <memory>

void register_tunnel_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace tn = ghostclaw::tunnel;

  tests.push_back({"tunnel_none_has_no_public_url", [] {
                     tn::NoneTunnel tunnel;
                     require(!tunnel.public_url().has_value(), "none tunnel must not expose URL");
                     require(!tunnel.health_check(), "none tunnel health should be false");
                   }});

  tests.push_back({"tunnel_factory_selects_provider", [] {
                     ghostclaw::config::TunnelConfig cfg;
                     cfg.provider = "custom";
                     ghostclaw::config::CustomTunnelConfig custom;
                     custom.command = "/bin/sh";
                     custom.args = {"-c", "echo https://factory.test; sleep 2"};
                     cfg.custom = custom;

                     auto tunnel = tn::create_tunnel(cfg);
                     require(tunnel != nullptr, "factory should return tunnel");
                     require(tunnel->name() == "custom", "factory should create custom tunnel");

                     cfg.provider = "none";
                     tunnel = tn::create_tunnel(cfg);
                     require(tunnel->name() == "none", "factory should create none tunnel");
                   }});

  tests.push_back({"tunnel_custom_spawns_and_stops", [] {
                     tn::CustomTunnel tunnel("/bin/sh",
                                             {"-c", "echo https://unit.test:{port}; sleep 5"});
                     auto started = tunnel.start("127.0.0.1", 18765);
                     require(started.ok(), started.error());
                     require(started.value().find("https://unit.test:18765") != std::string::npos,
                             "custom tunnel should substitute port");
                     require(tunnel.health_check(), "custom tunnel should be healthy after start");
                     auto stopped = tunnel.stop();
                     require(stopped.ok(), stopped.error());
                     require(!tunnel.health_check(), "custom tunnel should stop cleanly");
                   }});
}
