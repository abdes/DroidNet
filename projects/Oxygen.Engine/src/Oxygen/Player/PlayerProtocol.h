//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include <Oxygen/SceneInstantiation/SceneInstantiation.h>

namespace oxygen::scripting {
struct ScriptDiagnostic;
} // namespace oxygen::scripting

namespace oxygen::vortex {
struct GpuWorkSummary;
} // namespace oxygen::vortex

// The player's event stream: one JSON object per line, events from the player
// (`oxygen.player-events.schema.json`) and requests from the launcher
// (`oxygen.player-requests.schema.json`).
namespace oxygen::player {

enum class LifecycleState : std::uint8_t {
  kStarting,
  kSessionStarted,
  kSceneLoaded,
  kSceneFailed,
  kPaused,
  kResumed,
  kSessionEnded,
  //! The process is ending.
  kStopped,
};

[[nodiscard]] auto to_string(LifecycleState state) noexcept -> std::string_view;

enum class LogSeverity : std::uint8_t {
  kFatal,
  kError,
  kWarning,
  kInfo,
};

[[nodiscard]] auto to_string(LogSeverity severity) noexcept -> std::string_view;

//! A `lifecycle` event line. `scene`, `message` and `issues` are written only
//! when not empty.
[[nodiscard]] auto MakeLifecycleEvent(LifecycleState state,
  std::string_view scene = {}, std::string_view message = {},
  std::span<const instantiation::SceneInstantiationIssue> issues = {})
  -> std::string;

//! A `log` event line.
[[nodiscard]] auto MakeLogEvent(LogSeverity severity, std::string_view channel,
  std::string_view text) -> std::string;

//! A `diagnostic` event line carrying the diagnostic's JSON form.
[[nodiscard]] auto MakeDiagnosticEvent(
  const scripting::ScriptDiagnostic& diagnostic) -> std::string;

//! A `gpu_work` event line mirroring the summary field for field, with
//! buckets as their catalog ids.
[[nodiscard]] auto MakeGpuWorkEvent(const vortex::GpuWorkSummary& summary)
  -> std::string;

struct PlayRequest {
  //! Scene virtual path; the launch file's scene when empty.
  std::optional<std::string> scene;
};
struct PauseRequest { };
struct ResumeRequest { };
struct StepRequest {
  std::uint32_t frames { 1U };
};
struct EndSessionRequest { };
struct StopRequest { };
struct StatsRequest {
  bool gpu_work { false };
};

using PlayerRequest = std::variant<PlayRequest, PauseRequest, ResumeRequest,
  StepRequest, EndSessionRequest, StopRequest, StatsRequest>;

//! Parses one request line; the error says what is wrong with it.
[[nodiscard]] auto ParseRequest(std::string_view line)
  -> std::expected<PlayerRequest, std::string>;

} // namespace oxygen::player
