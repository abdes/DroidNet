//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
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
