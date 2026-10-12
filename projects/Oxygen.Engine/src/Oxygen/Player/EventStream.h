//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Player/PlayerProtocol.h>

namespace oxygen::scripting {
struct ScriptDiagnostic;
} // namespace oxygen::scripting

namespace oxygen::player {

class PipeChannel;

//! Where the player reports to its launcher.
/*!
 Without a pipe every report is dropped: a standalone player logs to its
 console only. Reports may be made from any thread.

 Log capture forwards every log line at INFO or above as a `log` event while
 it is on; `oxygen.log` output arrives on the `script` channel.
*/
class EventStream final {
public:
  //! `channel` may be null.
  explicit EventStream(std::unique_ptr<PipeChannel> channel);
  ~EventStream();

  OXYGEN_MAKE_NON_COPYABLE(EventStream)
  OXYGEN_MAKE_NON_MOVABLE(EventStream)

  [[nodiscard]] auto IsConnected() const noexcept -> bool;
  //! True once a connected launcher closed the pipe.
  [[nodiscard]] auto IsLauncherGone() const noexcept -> bool;

  auto Lifecycle(LifecycleState state, std::string_view scene = {},
    std::string_view message = {},
    std::span<const instantiation::SceneInstantiationIssue> issues = {})
    -> void;
  auto Diagnostic(const scripting::ScriptDiagnostic& diagnostic) -> void;
  //! An event line made with `PlayerProtocol.h`.
  auto Send(std::string_view line) noexcept -> void;

  //! Request lines received since the last call.
  [[nodiscard]] auto TakeRequests() -> std::vector<std::string>;

  auto StartLogCapture() -> void;
  auto StopLogCapture() noexcept -> void;

private:
  std::unique_ptr<PipeChannel> channel_;
  bool capturing_ { false };
};

//! The `channel` and `text` of a log event for a log line from `file`.
struct LogLine {
  std::string_view channel;
  std::string_view text;
};
[[nodiscard]] auto ClassifyLogLine(std::string_view file, std::string_view text)
  -> LogLine;

} // namespace oxygen::player
