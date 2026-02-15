#include "ghostclaw/heartbeat/scheduler.hpp"

#include "ghostclaw/channels/send_service.hpp"
#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"

#include <algorithm>
#include <thread>

namespace ghostclaw::heartbeat {

Scheduler::Scheduler(CronStore &store, agent::AgentEngine &agent, SchedulerConfig config,
                     const config::Config *runtime_config)
    : store_(store), agent_(agent), config_(config), runtime_config_(runtime_config) {}

Scheduler::~Scheduler() { stop(); }

void Scheduler::start() {
  if (running_) {
    return;
  }
  running_ = true;
  thread_ = std::thread([this]() { run_loop(); });
}

void Scheduler::stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool Scheduler::is_running() const { return running_; }

void Scheduler::run_loop() {
  while (running_) {
    auto due_jobs = store_.get_due_jobs();
    if (due_jobs.ok()) {
      for (const auto &job : due_jobs.value()) {
        execute_job(job);
      }
    }
    const auto wait_steps = std::max<long long>(1, config_.poll_interval.count() / 100);
    for (long long i = 0; i < wait_steps && running_; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

void Scheduler::execute_job(const CronJob &job) {
  const auto dispatch_payload = parse_channel_dispatch_payload(job.command);
  std::string status = "ok";
  for (std::uint32_t attempt = 0; attempt <= config_.max_retries; ++attempt) {
    if (dispatch_payload.has_value()) {
      auto sent = dispatch_channel_payload(*dispatch_payload);
      if (sent.ok()) {
        status = "ok";
        break;
      }
      status = sent.error();
    } else {
      auto result = agent_.run(job.command);
      if (result.ok()) {
        status = "ok";
        break;
      }
      status = result.error();
    }
    if (attempt < config_.max_retries) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  auto expr = CronExpression::parse(job.expression);
  auto next_run = expr.ok() ? expr.value().next_occurrence()
                            : std::chrono::system_clock::now() + std::chrono::hours(1);
  (void)store_.update_after_run(job.id, status, next_run);
}

std::optional<Scheduler::ChannelDispatchPayload>
Scheduler::parse_channel_dispatch_payload(const std::string &command) const {
  const std::string trimmed = common::trim(command);
  if (trimmed.size() < 2 || trimmed.front() != '{' || trimmed.back() != '}') {
    return std::nullopt;
  }

  const auto payload = common::json_parse_flat(trimmed);
  const auto kind_it = payload.find("kind");
  if (kind_it == payload.end() || common::to_lower(common::trim(kind_it->second)) != "channel_message") {
    return std::nullopt;
  }

  ChannelDispatchPayload out;
  const auto channel_it = payload.find("channel");
  const auto to_it = payload.find("to");
  const auto text_it = payload.find("text");
  if (channel_it == payload.end() || to_it == payload.end() || text_it == payload.end()) {
    return std::nullopt;
  }
  out.channel = channel_it->second;
  out.to = to_it->second;
  out.text = text_it->second;
  const auto id_it = payload.find("id");
  if (id_it != payload.end()) {
    out.id = id_it->second;
  }
  return out;
}

common::Status
Scheduler::dispatch_channel_payload(const Scheduler::ChannelDispatchPayload &payload) const {
  if (runtime_config_ == nullptr) {
    return common::Status::error("scheduler channel dispatch unavailable: missing runtime config");
  }

  channels::SendService sender(*runtime_config_);
  return sender.send({.channel = payload.channel, .recipient = payload.to, .text = payload.text});
}

} // namespace ghostclaw::heartbeat
