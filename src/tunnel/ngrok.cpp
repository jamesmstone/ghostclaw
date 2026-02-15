#include "ghostclaw/tunnel/ngrok.hpp"

#include <curl/curl.h>

#include <chrono>
#include <regex>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ghostclaw::tunnel {

namespace {

size_t curl_write(void *contents, size_t size, size_t nmemb, void *userp) {
  auto *buffer = static_cast<std::string *>(userp);
  const size_t bytes = size * nmemb;
  buffer->append(static_cast<const char *>(contents), bytes);
  return bytes;
}

common::Result<std::string> http_get(const std::string &url) {
  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    return common::Result<std::string>::failure("failed to initialize curl");
  }

  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  const CURLcode code = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(curl);

  if (code != CURLE_OK) {
    return common::Result<std::string>::failure(curl_easy_strerror(code));
  }
  if (status < 200 || status >= 300) {
    return common::Result<std::string>::failure("ngrok api returned status " +
                                                std::to_string(status));
  }
  return common::Result<std::string>::success(std::move(response));
}

#ifndef _WIN32
common::Result<TunnelProcess> spawn_process(const std::string &command,
                                            const std::vector<std::string> &args) {
  const pid_t pid = fork();
  if (pid < 0) {
    return common::Result<TunnelProcess>::failure("failed to fork ngrok process");
  }

  if (pid == 0) {
    std::vector<char *> cargs;
    cargs.reserve(args.size() + 2);
    cargs.push_back(const_cast<char *>(command.c_str()));
    for (const auto &arg : args) {
      cargs.push_back(const_cast<char *>(arg.c_str()));
    }
    cargs.push_back(nullptr);

    execvp(command.c_str(), cargs.data());
    _exit(127);
  }

  TunnelProcess process;
  process.pid = pid;
  return common::Result<TunnelProcess>::success(std::move(process));
}
#endif

} // namespace

NgrokTunnel::NgrokTunnel(std::string auth_token, std::string command_path, std::string api_base)
    : auth_token_(std::move(auth_token)), command_path_(std::move(command_path)),
      api_base_(std::move(api_base)) {}

common::Result<std::string> NgrokTunnel::start(const std::string &local_host,
                                                const std::uint16_t local_port) {
  (void)local_host;
  if (process_.is_running()) {
    const auto url = process_.get_url();
    if (url.has_value()) {
      return common::Result<std::string>::success(*url);
    }
  }

#ifdef _WIN32
  (void)local_port;
  return common::Result<std::string>::failure("ngrok tunnel is not implemented on Windows");
#else
  std::vector<std::string> args = {"http", std::to_string(local_port)};
  if (!auth_token_.empty()) {
    args.push_back("--authtoken");
    args.push_back(auth_token_);
  }

  auto spawned = spawn_process(command_path_, args);
  if (!spawned.ok()) {
    return common::Result<std::string>::failure(spawned.error());
  }
  process_.set(spawned.value());

  auto fetched = fetch_url_from_api();
  if (!fetched.ok()) {
    (void)stop();
    return common::Result<std::string>::failure(fetched.error());
  }

  TunnelProcess updated = spawned.value();
  updated.public_url = fetched.value();
  process_.set(std::move(updated));
  return common::Result<std::string>::success(fetched.value());
#endif
}

common::Status NgrokTunnel::stop() {
  process_.terminate();
  return common::Status::success();
}

bool NgrokTunnel::health_check() { return process_.is_running() && process_.get_url().has_value(); }

std::optional<std::string> NgrokTunnel::public_url() const { return process_.get_url(); }

common::Result<std::string> NgrokTunnel::fetch_url_from_api() const {
  const std::regex https_url_pattern(R"re("public_url"\s*:\s*"(https://[^"]+)")re");

  for (int attempt = 0; attempt < 12; ++attempt) {
    const auto response = http_get(api_base_ + "/api/tunnels");
    if (response.ok()) {
      std::smatch match;
      if (std::regex_search(response.value(), match, https_url_pattern) && match.size() > 1) {
        return common::Result<std::string>::success(match[1].str());
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }

  return common::Result<std::string>::failure("failed to get ngrok public URL from API");
}

} // namespace ghostclaw::tunnel
