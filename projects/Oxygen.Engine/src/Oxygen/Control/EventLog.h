//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Control/api_export.h>

namespace oxygen::control {

//! A log line as a `log` event: script output (`[lua.<level>] text`) on
//! channel `script`, other lines on their source file's stem.
struct LogLine {
  std::string_view channel;
  std::string_view text;
};

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) - loguru's order
OXGN_CTRL_NDAPI auto ClassifyLogLine(
  std::string_view file, std::string_view text) -> LogLine;

//! One published event.
struct ControlEvent {
  //! Increases by one per event, across all topics; starts at 1.
  uint64_t seq { 0U };
  //! The engine frame being run when the event was published.
  uint64_t frame { 0U };
  std::string topic;
  nlohmann::json data;
};

//! Events missed since a sequence number.
struct EventBatch {
  std::vector<ControlEvent> events;
  //! The sequence number to ask for next.
  uint64_t next_seq { 1U };
  //! Events after `since` that the buffer no longer holds.
  uint64_t dropped { 0U };
};

//! The numbered events a running engine publishes for remote clients.
/*!
 Publishers (the command dispatcher, application modules, log capture) add
 events from any thread; the buffer keeps the most recent `capacity` of them
 whether or not anybody listens, so a client can catch up after a gap.

 Topics are free-form names (`lifecycle`, `command`, `log`, `diagnostic`,
 `gpu_work`). A publisher whose events cost something to produce asks
 `HasInterest` first; the control server registers interest for the topics
 its clients subscribe to.
*/
class EventLog final {
public:
  static constexpr std::size_t kDefaultCapacity = 4096U;

  OXGN_CTRL_API explicit EventLog(std::size_t capacity = kDefaultCapacity);
  OXGN_CTRL_API ~EventLog();

  OXYGEN_MAKE_NON_COPYABLE(EventLog)
  OXYGEN_MAKE_NON_MOVABLE(EventLog)

  //! Thread-safe; must not log (it runs inside log capture).
  OXGN_CTRL_API auto Publish(
    std::string_view topic, nlohmann::json data) noexcept -> void;
  OXGN_CTRL_NDAPI auto Since(uint64_t since,
    std::span<const std::string> topics = {}, std::size_t limit = 0U) const
    -> EventBatch;
  OXGN_CTRL_NDAPI auto LatestSeq() const -> uint64_t;

  //! Stamps later events with the frame being run.
  OXGN_CTRL_API auto SetFrame(uint64_t frame) noexcept -> void;

  //! Called after each publish, from the publishing thread.
  OXGN_CTRL_API auto SetPublishedHandler(std::function<void()> handler) -> void;

  OXGN_CTRL_API auto AddInterest(std::string_view topic) -> void;
  OXGN_CTRL_API auto RemoveInterest(std::string_view topic) -> void;
  OXGN_CTRL_NDAPI auto HasInterest(std::string_view topic) const -> bool;

  //! Publishes log lines at INFO and above on topic `log` until stopped.
  OXGN_CTRL_API auto StartLogCapture() -> void;
  OXGN_CTRL_API auto StopLogCapture() noexcept -> void;

private:
  std::size_t capacity_;
  mutable std::mutex mutex_;
  std::deque<ControlEvent> events_;
  uint64_t next_seq_ { 1U };
  std::atomic<uint64_t> frame_ { 0U };
  std::function<void()> published_;
  std::map<std::string, int, std::less<>> interest_;
  bool capturing_ { false };
};

} // namespace oxygen::control
