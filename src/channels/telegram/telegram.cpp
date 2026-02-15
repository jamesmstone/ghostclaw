#include "ghostclaw/channels/telegram/telegram.hpp"

#include "ghostclaw/channels/allowlist.hpp"
#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <sstream>
#include <system_error>
#include <thread>
#include <unordered_map>

namespace ghostclaw::channels::telegram {

namespace {

std::string json_escape(const std::string &value) { return common::json_escape(value); }

std::string trim_copy(const std::string &value) { return common::trim(value); }

std::string normalize_allowlist_sender(std::string value) {
  value = common::to_lower(common::trim(std::move(value)));
  if (!value.empty() && value.front() == '@') {
    value.erase(value.begin());
  }
  return value;
}


std::string find_json_string_field(const std::string &json, const std::string &field) {
  return common::json_get_string(json, field);
}

std::string find_json_number_field(const std::string &json, const std::string &field) {
  return common::json_get_number(json, field);
}

std::string find_json_object_field(const std::string &json, const std::string &field) {
  return common::json_get_object(json, field);
}

std::string find_json_array_field(const std::string &json, const std::string &field) {
  return common::json_get_array(json, field);
}

std::vector<std::string> split_top_level_objects(const std::string &array_json) {
  return common::json_split_top_level_objects(array_json);
}

common::Result<TelegramChannelPlugin::IncomingMessage>
parse_update(const std::string &update_json) {
  TelegramChannelPlugin::IncomingMessage message;
  const std::string update_id = find_json_number_field(update_json, "update_id");
  if (update_id.empty()) {
    return common::Result<TelegramChannelPlugin::IncomingMessage>::failure(
        "missing update_id");
  }
  try {
    message.update_id = static_cast<std::uint64_t>(std::stoull(update_id));
  } catch (...) {
    return common::Result<TelegramChannelPlugin::IncomingMessage>::failure(
        "invalid update_id");
  }

  std::string message_obj = find_json_object_field(update_json, "message");
  if (message_obj.empty()) {
    message_obj = find_json_object_field(update_json, "edited_message");
  }
  if (message_obj.empty()) {
    return common::Result<TelegramChannelPlugin::IncomingMessage>::failure(
        "update has no message");
  }

  message.message_id = find_json_number_field(message_obj, "message_id");
  std::string text = find_json_string_field(message_obj, "text");
  if (text.empty()) {
    text = find_json_string_field(message_obj, "caption");
  }
  message.text = text;

  const std::string chat_obj = find_json_object_field(message_obj, "chat");
  if (!chat_obj.empty()) {
    message.chat_id = find_json_number_field(chat_obj, "id");
    if (message.chat_id.empty()) {
      message.chat_id = find_json_string_field(chat_obj, "id");
    }
  }
  if (message.chat_id.empty()) {
    return common::Result<TelegramChannelPlugin::IncomingMessage>::failure(
        "message chat id missing");
  }

  const std::string from_obj = find_json_object_field(message_obj, "from");
  if (!from_obj.empty()) {
    message.sender_username = find_json_string_field(from_obj, "username");
    message.sender_id = find_json_number_field(from_obj, "id");
    if (message.sender_id.empty()) {
      message.sender_id = find_json_string_field(from_obj, "id");
    }
    if (!message.sender_username.empty()) {
      message.sender = message.sender_username;
    } else {
      message.sender = find_json_string_field(from_obj, "first_name");
      if (message.sender.empty()) {
        message.sender = message.sender_id;
      }
    }
  }

  const std::string timestamp = find_json_number_field(message_obj, "date");
  if (!timestamp.empty()) {
    try {
      message.timestamp = static_cast<std::uint64_t>(std::stoull(timestamp));
    } catch (...) {
      message.timestamp = 0;
    }
  }

  return common::Result<TelegramChannelPlugin::IncomingMessage>::success(std::move(message));
}

} // namespace

TelegramChannelPlugin::TelegramChannelPlugin(std::shared_ptr<providers::HttpClient> http_client)
    : http_client_(std::move(http_client)) {}

TelegramChannelPlugin::~TelegramChannelPlugin() { stop(); }

std::string_view TelegramChannelPlugin::id() const { return "telegram"; }

ChannelCapabilities TelegramChannelPlugin::capabilities() const {
  ChannelCapabilities caps;
  caps.reply = true;
  caps.media = true;
  caps.native_commands = true;
  return caps;
}

common::Status TelegramChannelPlugin::start(const ChannelConfig &config) {
  if (running_.load()) {
    return common::Status::success();
  }
  if (http_client_ == nullptr) {
    return common::Status::error("telegram http client unavailable");
  }

  const auto token_it = config.settings.find("bot_token");
  if (token_it == config.settings.end() || trim_copy(token_it->second).empty()) {
    return common::Status::error("telegram bot_token is required");
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    config_ = config;
    const std::string token = trim_copy(token_it->second);
    base_url_ = "https://api.telegram.org/bot" + token;
    next_update_offset_ = 0;

    allowed_users_.clear();
    const auto allowed_it = config.settings.find("allowed_users");
    if (allowed_it != config.settings.end()) {
      allowed_users_ = parse_allowlist(allowed_it->second);
    }

    poll_timeout_seconds_ = 2;
    const auto timeout_it = config.settings.find("poll_timeout_seconds");
    if (timeout_it != config.settings.end()) {
      poll_timeout_seconds_ = parse_u64_setting(timeout_it->second, 2);
    }

    idle_sleep_ = std::chrono::milliseconds(150);
    const auto sleep_it = config.settings.find("idle_sleep_ms");
    if (sleep_it != config.settings.end()) {
      idle_sleep_ =
          std::chrono::milliseconds(parse_u64_setting(sleep_it->second, 150));
    }

    polling_enabled_ = true;
    const auto polling_it = config.settings.find("polling_enabled");
    if (polling_it != config.settings.end()) {
      polling_enabled_ = parse_bool_setting(polling_it->second, true);
    }
  }

  healthy_.store(true);
  running_.store(true);
  if (polling_enabled_) {
    worker_ = std::thread([this]() { run_loop(); });
  }
  return common::Status::success();
}

void TelegramChannelPlugin::stop() {
  running_.store(false);
  if (worker_.joinable()) {
    try {
      worker_.join();
    } catch (const std::system_error &err) {
      std::cerr << "[telegram] join failed: " << err.what() << "\n";
    }
  }
}

common::Status TelegramChannelPlugin::send_text(const std::string &recipient,
                                                const std::string &text) {
  if (trim_copy(recipient).empty()) {
    return common::Status::error("recipient is required");
  }
  if (trim_copy(text).empty()) {
    return common::Status::error("text is required");
  }

  std::string base_url;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    base_url = base_url_;
  }
  if (base_url.empty()) {
    return common::Status::error("telegram plugin not configured");
  }

  std::ostringstream body;
  body << "{";
  body << "\"chat_id\":\"" << json_escape(trim_copy(recipient)) << "\",";
  body << "\"text\":\"" << json_escape(text) << "\"";
  body << "}";

  const auto response = http_client_->post_json(
      base_url + "/sendMessage",
      {{"Content-Type", "application/json"}},
      body.str(),
      15000);
  return check_api_response(response, "sendMessage");
}

common::Status TelegramChannelPlugin::send_media(const std::string &recipient,
                                                 const MediaMessage &media) {
  if (trim_copy(recipient).empty()) {
    return common::Status::error("recipient is required");
  }
  if (trim_copy(media.url).empty()) {
    return common::Status::error("media url is required");
  }

  std::string base_url;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    base_url = base_url_;
  }
  if (base_url.empty()) {
    return common::Status::error("telegram plugin not configured");
  }

  const bool is_image = common::starts_with(common::to_lower(media.mime_type), "image/");
  const std::string endpoint = is_image ? "sendPhoto" : "sendDocument";
  const std::string field = is_image ? "photo" : "document";

  std::ostringstream body;
  body << "{";
  body << "\"chat_id\":\"" << json_escape(trim_copy(recipient)) << "\",";
  body << "\"" << field << "\":\"" << json_escape(media.url) << "\"";
  if (!trim_copy(media.caption).empty()) {
    body << ",\"caption\":\"" << json_escape(media.caption) << "\"";
  }
  body << "}";

  const auto response = http_client_->post_json(
      base_url + "/" + endpoint,
      {{"Content-Type", "application/json"}},
      body.str(),
      20000);
  return check_api_response(response, endpoint);
}

void TelegramChannelPlugin::on_message(PluginMessageCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  message_callback_ = std::move(callback);
}

void TelegramChannelPlugin::on_reaction(PluginReactionCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  reaction_callback_ = std::move(callback);
}

bool TelegramChannelPlugin::health_check() {
  if (!running_.load()) {
    return true;
  }
  return healthy_.load();
}

void TelegramChannelPlugin::run_loop() {
  while (running_.load()) {
    const auto status = poll_once();
    if (!status.ok()) {
      healthy_.store(false);
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = status.error();
      }
      std::cerr << "[telegram] poll error: " << status.error() << "\n";
      std::this_thread::sleep_for(std::chrono::milliseconds(350));
    } else {
      healthy_.store(true);
      std::this_thread::sleep_for(idle_sleep_);
    }
  }
}

common::Status TelegramChannelPlugin::poll_once() {
  std::string base_url;
  std::uint64_t offset = 0;
  std::uint64_t timeout_seconds = 0;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    base_url = base_url_;
    offset = next_update_offset_;
    timeout_seconds = poll_timeout_seconds_;
  }
  if (base_url.empty()) {
    return common::Status::error("telegram plugin not configured");
  }

  std::ostringstream body;
  body << "{";
  body << "\"offset\":" << offset << ",";
  body << "\"timeout\":" << timeout_seconds << ",";
  body << "\"allowed_updates\":[\"message\",\"edited_message\"]";
  body << "}";

  const auto response = http_client_->post_json(
      base_url + "/getUpdates",
      {{"Content-Type", "application/json"}},
      body.str(),
      (timeout_seconds + 2) * 1000);
  const auto status = check_api_response(response, "getUpdates");
  if (!status.ok()) {
    return status;
  }
  return parse_and_dispatch_updates(response.body);
}

common::Status TelegramChannelPlugin::parse_and_dispatch_updates(const std::string &response_body) {
  const std::string result_array = find_json_array_field(response_body, "result");
  if (result_array.empty()) {
    return common::Status::error("telegram getUpdates response missing result array");
  }

  const auto updates = split_top_level_objects(result_array);
  std::size_t dispatched = 0;
  std::uint64_t max_seen_update = 0;
  for (const auto &update_json : updates) {
    const auto parsed = parse_update(update_json);
    if (!parsed.ok()) {
      std::cerr << "[telegram] skip update parse error: " << parsed.error() << "\n";
      continue;
    }
    const auto &incoming = parsed.value();
    max_seen_update = std::max(max_seen_update, incoming.update_id);
    if (trim_copy(incoming.text).empty()) {
      std::cerr << "[telegram] skip empty text update_id=" << incoming.update_id << "\n";
      continue;
    }
    if (!is_allowed_sender(incoming)) {
      std::cerr << "[telegram] skip unauthorized sender update_id=" << incoming.update_id
                << " sender_id=" << incoming.sender_id << " username=" << incoming.sender_username
                << "\n";
      continue;
    }

    PluginMessageCallback callback_copy;
    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callback_copy = message_callback_;
    }
    if (!callback_copy) {
      std::cerr << "[telegram] skip update without callback update_id=" << incoming.update_id << "\n";
      continue;
    }

    PluginMessage message;
    message.id = incoming.message_id.empty() ? std::to_string(incoming.update_id)
                                             : incoming.message_id;
    message.sender = incoming.sender;
    message.recipient = incoming.chat_id;
    message.content = incoming.text;
    message.channel = "telegram";
    message.timestamp = incoming.timestamp;
    message.metadata["chat_id"] = incoming.chat_id;
    message.metadata["update_id"] = std::to_string(incoming.update_id);
    if (!incoming.sender_username.empty()) {
      message.metadata["username"] = incoming.sender_username;
    }
    if (!incoming.sender_id.empty()) {
      message.metadata["sender_id"] = incoming.sender_id;
    }
    callback_copy(message);
    ++dispatched;
  }

  if (max_seen_update > 0) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    next_update_offset_ = std::max(next_update_offset_, max_seen_update + 1);
  }
  if (!updates.empty()) {
    std::cerr << "[telegram] polled updates=" << updates.size() << " dispatched=" << dispatched
              << " next_offset=" << next_update_offset_ << "\n";
  }
  return common::Status::success();
}

bool TelegramChannelPlugin::is_allowed_sender(const IncomingMessage &message) const {
  std::vector<std::string> allowlist;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    allowlist = allowed_users_;
  }
  if (allowlist.empty()) {
    return true;
  }

  if (!message.sender_username.empty()) {
    if (check_allowlist(normalize_allowlist_sender(message.sender_username), allowlist)) {
      return true;
    }
  }
  if (!message.sender_id.empty()) {
    if (check_allowlist(message.sender_id, allowlist)) {
      return true;
    }
  }
  if (!message.sender.empty()) {
    if (check_allowlist(normalize_allowlist_sender(message.sender), allowlist)) {
      return true;
    }
  }
  return false;
}

common::Status TelegramChannelPlugin::check_api_response(
    const providers::HttpResponse &response, const std::string_view operation) const {
  if (response.timeout) {
    return common::Status::error(std::string(operation) + " timeout");
  }
  if (response.network_error) {
    return common::Status::error(std::string(operation) + " network error: " +
                                 response.network_error_message);
  }
  if (response.status >= 400) {
    std::string snippet = trim_copy(response.body);
    if (snippet.size() > 240) {
      snippet.resize(240);
    }
    return common::Status::error(std::string(operation) + " failed status=" +
                                 std::to_string(response.status) + " body=" + snippet);
  }
  if (response.status < 200 || response.status >= 300) {
    return common::Status::error(std::string(operation) + " unexpected status=" +
                                 std::to_string(response.status));
  }
  if (response.body.find("\"ok\":true") == std::string::npos &&
      response.body.find("\"ok\": true") == std::string::npos) {
    return common::Status::error(std::string(operation) + " response missing ok=true");
  }
  return common::Status::success();
}

std::vector<std::string> TelegramChannelPlugin::parse_allowlist(const std::string &raw) {
  std::vector<std::string> out;
  std::stringstream stream(raw);
  std::string token;
  while (std::getline(stream, token, ',')) {
    token = normalize_allowlist_sender(token);
    if (!token.empty()) {
      out.push_back(token);
    }
  }
  return out;
}

bool TelegramChannelPlugin::parse_bool_setting(const std::string &value, const bool fallback) {
  const std::string normalized = common::to_lower(trim_copy(value));
  if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
    return true;
  }
  if (normalized == "false" || normalized == "0" || normalized == "no" ||
      normalized == "off") {
    return false;
  }
  return fallback;
}

std::uint64_t TelegramChannelPlugin::parse_u64_setting(const std::string &value,
                                                       const std::uint64_t fallback) {
  try {
    return static_cast<std::uint64_t>(std::stoull(trim_copy(value)));
  } catch (...) {
    return fallback;
  }
}

} // namespace ghostclaw::channels::telegram
