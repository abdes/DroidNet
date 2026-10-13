//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Control/EventLog.h>

namespace oxygen::control {

namespace {

  constexpr auto kLogCallbackId = "oxygen.control.events";
  //! `oxygen.log` lines start with `[lua.<level>] `.
  constexpr std::string_view kScriptLogPrefix = "[lua.";

  auto SeverityOf(const loguru::Verbosity verbosity) -> std::string_view
  {
    if (verbosity <= loguru::Verbosity_FATAL) {
      return "fatal";
    }
    if (verbosity == loguru::Verbosity_ERROR) {
      return "error";
    }
    if (verbosity == loguru::Verbosity_WARNING) {
      return "warning";
    }
    return "info";
  }

  // Runs under loguru's lock: it must not log.
  auto OnLog(void* user_data, const loguru::Message& message) -> void
  {
    auto* events = static_cast<EventLog*>(user_data);
    try {
      const auto line
        = ClassifyLogLine(message.filename != nullptr ? message.filename : "",
          message.message != nullptr ? message.message : "");
      events->Publish("log",
        nlohmann::json {
          { "severity", SeverityOf(message.verbosity) },
          { "channel", line.channel },
          { "text", line.text },
        });
    } catch (...) {
      // A line that cannot be serialized is dropped.
      return;
    }
  }

} // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) - loguru's order
auto ClassifyLogLine(const std::string_view file, const std::string_view text)
  -> LogLine
{
  if (text.starts_with(kScriptLogPrefix)) {
    if (const auto end = text.find("] "); end != std::string_view::npos) {
      return { .channel = "script", .text = text.substr(end + 2U) };
    }
  }
  auto channel = file;
  if (const auto slash = channel.find_last_of("/\\");
    slash != std::string_view::npos) {
    channel.remove_prefix(slash + 1U);
  }
  if (const auto dot = channel.find('.'); dot != std::string_view::npos) {
    channel = channel.substr(0U, dot);
  }
  return { .channel = channel, .text = text };
}

EventLog::EventLog(const std::size_t capacity)
  : capacity_((std::max)(capacity, std::size_t { 1U }))
{
}

EventLog::~EventLog() { StopLogCapture(); }

auto EventLog::Publish(
  const std::string_view topic, nlohmann::json data) noexcept -> void
{
  auto published = std::function<void()> {};
  try {
    const auto lock = std::scoped_lock(mutex_);
    events_.push_back(ControlEvent {
      .seq = next_seq_++,
      .frame = frame_.load(std::memory_order_relaxed),
      .topic = std::string(topic),
      .data = std::move(data),
    });
    while (events_.size() > capacity_) {
      events_.pop_front();
    }
    published = published_;
  } catch (...) {
    // An event that cannot be stored is dropped.
    return;
  }
  if (published) {
    try {
      published();
    } catch (...) {
      return;
    }
  }
}

auto EventLog::Since(const uint64_t since,
  const std::span<const std::string> topics, const std::size_t limit) const
  -> EventBatch
{
  const auto lock = std::scoped_lock(mutex_);
  auto batch = EventBatch { .next_seq = next_seq_ };
  const auto first = since + 1U;
  if (!events_.empty() && events_.front().seq > first) {
    batch.dropped = events_.front().seq - first;
  }
  for (const auto& event : events_) {
    if (event.seq < first) {
      continue;
    }
    if (limit != 0U && batch.events.size() >= limit) {
      batch.next_seq = event.seq;
      break;
    }
    if (topics.empty() || std::ranges::contains(topics, event.topic)) {
      batch.events.push_back(event);
    }
  }
  return batch;
}

auto EventLog::LatestSeq() const -> uint64_t
{
  const auto lock = std::scoped_lock(mutex_);
  return next_seq_ - 1U;
}

auto EventLog::SetFrame(const uint64_t frame) noexcept -> void
{
  frame_.store(frame, std::memory_order_relaxed);
}

auto EventLog::SetPublishedHandler(std::function<void()> handler) -> void
{
  const auto lock = std::scoped_lock(mutex_);
  published_ = std::move(handler);
}

auto EventLog::AddInterest(const std::string_view topic) -> void
{
  const auto lock = std::scoped_lock(mutex_);
  ++interest_[std::string(topic)];
}

auto EventLog::RemoveInterest(const std::string_view topic) -> void
{
  const auto lock = std::scoped_lock(mutex_);
  if (const auto it = interest_.find(topic); it != interest_.end()) {
    if (--it->second <= 0) {
      interest_.erase(it);
    }
  }
}

auto EventLog::HasInterest(const std::string_view topic) const -> bool
{
  const auto lock = std::scoped_lock(mutex_);
  return interest_.contains(topic);
}

auto EventLog::StartLogCapture() -> void
{
  if (capturing_) {
    return;
  }
  loguru::add_callback(kLogCallbackId, &OnLog, this, loguru::Verbosity_INFO);
  capturing_ = true;
}

auto EventLog::StopLogCapture() noexcept -> void
{
  if (capturing_) {
    loguru::remove_callback(kLogCallbackId);
    capturing_ = false;
  }
}

} // namespace oxygen::control
