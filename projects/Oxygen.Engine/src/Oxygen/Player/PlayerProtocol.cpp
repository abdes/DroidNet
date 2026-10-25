//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Player/PlayerProtocol.h>
#include <Oxygen/SceneInstantiation/SceneInstantiation.h>
#include <Oxygen/Scripting/Analysis/DiagnosticJson.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>

namespace oxygen::player {

namespace {

  using nlohmann::json;

  auto ToJson(const vortex::GpuWorkStat& stat) -> json
  {
    return json { { "avg_ms", stat.avg_ms }, { "max_ms", stat.max_ms } };
  }

  auto ToJson(const vortex::GpuWorkGroup& group) -> json
  {
    auto items = json::array();
    for (const auto& item : group.items) {
      items.push_back(json {
        { "id", item.id },
        { "name", item.name },
        { "time", ToJson(item.time) },
      });
    }
    return json {
      { "bucket", vortex::GetGpuWorkBucketInfo(group.bucket).id },
      { "time", ToJson(group.time) },
      { "items", std::move(items) },
    };
  }

  auto ToJson(const std::span<const vortex::GpuWorkGroup> groups) -> json
  {
    auto result = json::array();
    for (const auto& group : groups) {
      result.push_back(ToJson(group));
    }
    return result;
  }

} // namespace

auto to_string(const LifecycleState state) noexcept -> std::string_view
{
  switch (state) {
  case LifecycleState::kSessionStarted:
    return "session_started";
  case LifecycleState::kSceneLoaded:
    return "scene_loaded";
  case LifecycleState::kSceneFailed:
    return "scene_failed";
  case LifecycleState::kPaused:
    return "paused";
  case LifecycleState::kResumed:
    return "resumed";
  case LifecycleState::kSessionEnded:
    return "session_ended";
  case LifecycleState::kStopping:
    return "stopping";
  }
  return "__NotSupported__";
}

auto LifecycleData(const LifecycleState state, const std::string_view scene,
  const std::string_view message,
  const std::span<const instantiation::SceneInstantiationIssue> issues,
  const std::string_view code) -> json
{
  auto event = json {
    { "state", to_string(state) },
  };
  if (!scene.empty()) {
    event["scene"] = scene;
  }
  if (!message.empty()) {
    event["message"] = message;
  }
  if (!code.empty()) {
    event["code"] = code;
  }
  if (!issues.empty()) {
    auto list = json::array();
    for (const auto& issue : issues) {
      auto entry = json {
        { "code", issue.code },
        { "scene", data::to_string(issue.scene) },
        { "message", issue.message },
      };
      if (issue.node_index.has_value()) {
        entry["node_index"] = *issue.node_index;
      }
      if (!issue.node_path.empty()) {
        auto path = json::array();
        for (const auto& id : issue.node_path) {
          path.push_back(data::to_string(id));
        }
        entry["node_path"] = std::move(path);
      }
      list.push_back(std::move(entry));
    }
    event["issues"] = std::move(list);
  }
  return event;
}

auto DiagnosticData(const scripting::ScriptDiagnostic& diagnostic) -> json
{
  return json::parse(scripting::ToJson(diagnostic));
}

auto StatsData(const std::uint32_t fps) -> json
{
  return json { { "fps", fps } };
}

auto GpuWorkData(const vortex::GpuWorkSummary& summary) -> json
{
  auto views = json::array();
  for (const auto& view : summary.views) {
    views.push_back(json {
      { "view_id", view.view_id },
      { "name", view.name },
      { "time", ToJson(view.time) },
      { "groups", ToJson(view.groups) },
    });
  }
  return json {
    { "schema_version", vortex::GpuWorkSummary::kSchemaVersion },
    { "revision", summary.revision },
    { "last_frame", summary.last_frame.get() },
    { "window_ms", summary.window_ms },
    { "valid_frames", summary.valid_frames },
    { "invalid_frames", summary.invalid_frames },
    { "frame_gpu", ToJson(summary.frame_gpu) },
    { "idle", ToJson(summary.idle) },
    { "views", std::move(views) },
    { "frame_work", ToJson(summary.frame_work) },
    { "frame_work_time", ToJson(summary.frame_work_time) },
    { "frame_gpu_history_ms", summary.frame_gpu_history_ms },
  };
}

} // namespace oxygen::player
