//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <exception>
#include <span>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/SceneInstantiation/SceneInstantiation.h>

namespace oxygen::scripting {
struct ScriptDiagnostic;
} // namespace oxygen::scripting

namespace oxygen::vortex {
struct GpuWorkSummary;
} // namespace oxygen::vortex

namespace oxygen::player {

//! The player's `lifecycle` events.
enum class LifecycleState : std::uint8_t {
  kSessionStarted,
  kSceneLoaded,
  kSceneFailed,
  kPaused,
  kResumed,
  kSessionEnded,
  //! The engine is shutting down.
  kStopping,
};

[[nodiscard]] auto to_string(LifecycleState state) noexcept -> std::string_view;

//! The code of a `scene_failed` event for a model scene: a model is placed
//! in an authored scene, never played on its own.
inline constexpr std::string_view kModelSceneCode = "player.model_scene";

//! The code of a `scene_failed` event for a pak whose requirements the cooked
//! roots and paks mounted below it do not meet.
inline constexpr std::string_view kDependencyMissingCode
  = "player.launch.dependency_missing";

//! The code of a `scene_failed` event for a launch mount that does not exist.
inline constexpr std::string_view kContentMissingCode
  = "player.launch.content_missing";
//! The code of a `scene_failed` event for a launch mount whose cooked bytes
//! are refused: another format or version, corrupt or unverifiable.
inline constexpr std::string_view kContentRefusedCode
  = "player.launch.content_refused";
//! The code of a `scene_failed` event for a launch mount that has the
//! SourceKey of another mounted source.
inline constexpr std::string_view kSourceKeyConflictCode
  = "player.launch.source_key_conflict";
//! The code of a `scene_failed` event for a launch mount the system could not
//! read, such as one another process holds or one access is denied to.
inline constexpr std::string_view kContentUnreadableCode
  = "player.launch.content_unreadable";
//! The code of a `scene_failed` event for any other failure to mount the
//! launch file's content.
inline constexpr std::string_view kMountFailedCode
  = "player.launch.mount_failed";

//! The code of the `scene_failed` event for `error`, thrown while mounting
//! the launch file's content.
[[nodiscard]] auto MountFailureCode(const std::exception_ptr& error) noexcept
  -> std::string_view;

//! The `refusal` of the `scene_failed` event for `error`, thrown while
//! mounting the launch file's content: the refused source with each failed
//! requirement, or the source whose SourceKey conflicts and the one that has
//! it. Null when `error` carries nothing beyond its message.
[[nodiscard]] auto MountRefusalData(const std::exception_ptr& error)
  -> nlohmann::json;

//! Data of a `lifecycle` event; `code` names why a scene was refused.
[[nodiscard]] auto LifecycleData(LifecycleState state,
  std::string_view scene = {}, std::string_view message = {},
  std::span<const instantiation::SceneInstantiationIssue> issues = {},
  std::string_view code = {}) -> nlohmann::json;

//! Data of a `diagnostic` event: the diagnostic itself.
[[nodiscard]] auto DiagnosticData(const scripting::ScriptDiagnostic& diagnostic)
  -> nlohmann::json;

//! The topic of the player's frame statistics, published while a client
//! subscribes to it.
inline constexpr std::string_view kStatsTopic = "stats";

//! Data of a `stats` event: `{fps}`, whole frames per second.
[[nodiscard]] auto StatsData(std::uint32_t fps) -> nlohmann::json;

//! Data of a `gpu_work` event: the summary, field for field, with buckets
//! written as their catalog ids.
[[nodiscard]] auto GpuWorkData(const vortex::GpuWorkSummary& summary)
  -> nlohmann::json;

} // namespace oxygen::player
