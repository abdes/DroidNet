//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Player/EventStream.h>
#include <Oxygen/Player/PipeChannel.h>
#include <Oxygen/Player/PlayerProtocol.h>
#include <Oxygen/SceneInstantiation/SceneInstantiation.h>

namespace oxygen::player {

namespace {

  constexpr auto kLogCallbackId = "oxygen.player.events";
  //! `oxygen.log` lines start with `[lua.<level>] `.
  constexpr std::string_view kScriptLogPrefix = "[lua.";

  auto SeverityOf(const loguru::Verbosity verbosity) -> LogSeverity
  {
    if (verbosity <= loguru::Verbosity_FATAL) {
      return LogSeverity::kFatal;
    }
    if (verbosity == loguru::Verbosity_ERROR) {
      return LogSeverity::kError;
    }
    if (verbosity == loguru::Verbosity_WARNING) {
      return LogSeverity::kWarning;
    }
    return LogSeverity::kInfo;
  }

  // Runs under loguru's lock: it must not log.
  auto OnLog(void* user_data, const loguru::Message& message) -> void
  {
    auto* stream = static_cast<EventStream*>(user_data);
    try {
      const auto line
        = ClassifyLogLine(message.filename != nullptr ? message.filename : "",
          message.message != nullptr ? message.message : "");
      stream->Send(
        MakeLogEvent(SeverityOf(message.verbosity), line.channel, line.text));
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

EventStream::EventStream(std::unique_ptr<PipeChannel> channel)
  : channel_(std::move(channel))
{
}

EventStream::~EventStream() { StopLogCapture(); }

auto EventStream::IsConnected() const noexcept -> bool
{
  return channel_ != nullptr;
}

auto EventStream::IsLauncherGone() const noexcept -> bool
{
  return channel_ != nullptr && channel_->IsClosed();
}

auto EventStream::Lifecycle(const LifecycleState state,
  const std::string_view scene, const std::string_view message,
  const std::span<const instantiation::SceneInstantiationIssue> issues) -> void
{
  if (channel_) {
    channel_->Send(MakeLifecycleEvent(state, scene, message, issues));
  }
}

auto EventStream::Diagnostic(const scripting::ScriptDiagnostic& diagnostic)
  -> void
{
  if (channel_) {
    channel_->Send(MakeDiagnosticEvent(diagnostic));
  }
}

auto EventStream::Send(const std::string_view line) noexcept -> void
{
  if (channel_) {
    channel_->Send(line);
  }
}

auto EventStream::TakeRequests() -> std::vector<std::string>
{
  return channel_ ? channel_->TakeRequests() : std::vector<std::string> {};
}

auto EventStream::StartLogCapture() -> void
{
  if (!channel_ || capturing_) {
    return;
  }
  loguru::add_callback(kLogCallbackId, &OnLog, this, loguru::Verbosity_INFO);
  capturing_ = true;
}

auto EventStream::StopLogCapture() noexcept -> void
{
  if (capturing_) {
    loguru::remove_callback(kLogCallbackId);
    capturing_ = false;
  }
}

} // namespace oxygen::player
