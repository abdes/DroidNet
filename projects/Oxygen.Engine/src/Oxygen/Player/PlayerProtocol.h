//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <nlohmann/json.hpp>

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

//! Data of a `lifecycle` event.
[[nodiscard]] auto LifecycleData(LifecycleState state,
  std::string_view scene = {}, std::string_view message = {},
  std::span<const instantiation::SceneInstantiationIssue> issues = {})
  -> nlohmann::json;

//! Data of a `diagnostic` event: the diagnostic itself.
[[nodiscard]] auto DiagnosticData(const scripting::ScriptDiagnostic& diagnostic)
  -> nlohmann::json;

//! Data of a `gpu_work` event: the summary, field for field, with buckets
//! written as their catalog ids.
[[nodiscard]] auto GpuWorkData(const vortex::GpuWorkSummary& summary)
  -> nlohmann::json;

} // namespace oxygen::player
