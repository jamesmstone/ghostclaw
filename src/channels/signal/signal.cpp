#include "ghostclaw/channels/signal/signal.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"

#include <sstream>

namespace ghostclaw::channels::signal {

namespace {

std::string json_escape(const std::string &value) { return common::json_escape(value); }

} // namespace

SignalChannelPlugin::SignalChannelPlugin(std::shared_ptr<providers::HttpClient> http_client)
    : http_client_(std::move(http_client)) {}

SignalChannelPlugin::~SignalChannelPlugin() { stop(); }

std::string_view SignalChannelPlugin::id() const { return "signal"; }

ChannelCapabilities SignalChannelPlugin::capabilities() const {
  ChannelCapabilities caps;
  caps.reply = true;
  caps.media = true;
  caps.reactions = true;
  return caps;
}

common::Status SignalChannelPlugin::start(const ChannelConfig &config) {
  if (running_.load()) {
    return common::Status::success();
  }
  if (http_client_ == nullptr) {
    return common::Status::error("signal http client unavailable");
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto url_it = config.settings.find("api_url");
  if (url_it != config.settings.end() && !common::trim(url_it->second).empty()) {
    base_url_ = common::trim(url_it->second);
  } else {
    base_url_ = "http://127.0.0.1:8080";
  }

  const auto account_it = config.settings.find("account");
  account_ = (account_it != config.settings.end()) ? common::trim(account_it->second) : "";
  if (account_.empty()) {
    return common::Status::error("signal account is required");
  }

  healthy_.store(true);
  running_.store(true);
  return common::Status::success();
}

void SignalChannelPlugin::stop() { running_.store(false); }

common::Status SignalChannelPlugin::send_text(const std::string &recipient,
                                              const std::string &text) {
  if (!running_.load()) {
    return common::Status::error("signal plugin is not running");
  }
  if (common::trim(recipient).empty()) {
    return common::Status::error("signal recipient is required");
  }
  if (common::trim(text).empty()) {
    return common::Status::error("signal text is required");
  }

  std::string base_url;
  std::string account;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    base_url = base_url_;
    account = account_;
  }

  std::ostringstream body;
  body << "{";
  body << "\"message\":\"" << json_escape(text) << "\",";
  body << "\"number\":\"" << json_escape(account) << "\",";
  body << "\"recipients\":[\"" << json_escape(common::trim(recipient)) << "\"]";
  body << "}";

  const auto response = http_client_->post_json(
      base_url + "/v2/send",
      {{"Content-Type", "application/json"}},
      body.str(),
      15000);
  return check_response(response, "signal send");
}

common::Status SignalChannelPlugin::send_media(const std::string &recipient,
                                               const MediaMessage &media) {
  std::string text;
  if (!common::trim(media.caption).empty()) {
    text = media.caption;
  }
  if (!common::trim(media.url).empty()) {
    if (!text.empty()) {
      text += "\n";
    }
    text += media.url;
  }
  return send_text(recipient, text);
}

void SignalChannelPlugin::on_message(PluginMessageCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  message_callback_ = std::move(callback);
}

void SignalChannelPlugin::on_reaction(PluginReactionCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  reaction_callback_ = std::move(callback);
}

bool SignalChannelPlugin::health_check() { return !running_.load() || healthy_.load(); }

common::Status SignalChannelPlugin::check_response(const providers::HttpResponse &response,
                                                   const std::string_view operation) const {
  if (response.timeout) {
    return common::Status::error(std::string(operation) + " timeout");
  }
  if (response.network_error) {
    return common::Status::error(std::string(operation) + " network error: " +
                                 response.network_error_message);
  }
  if (response.status < 200 || response.status >= 300) {
    std::string body = common::trim(response.body);
    if (body.size() > 200) {
      body.resize(200);
    }
    return common::Status::error(std::string(operation) + " failed status=" +
                                 std::to_string(response.status) + " body=" + body);
  }
  return common::Status::success();
}

} // namespace ghostclaw::channels::signal
