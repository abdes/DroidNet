//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Player/PlayerProtocol.h>
#include <Oxygen/Player/SchemaCheck.h>
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
  case LifecycleState::kStarting:
    return "starting";
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
  case LifecycleState::kStopped:
    return "stopped";
  }
  return "__NotSupported__";
}

auto to_string(const LogSeverity severity) noexcept -> std::string_view
{
  switch (severity) {
  case LogSeverity::kFatal:
    return "fatal";
  case LogSeverity::kError:
    return "error";
  case LogSeverity::kWarning:
    return "warning";
  case LogSeverity::kInfo:
    return "info";
  }
  return "__NotSupported__";
}

auto MakeLifecycleEvent(const LifecycleState state,
  const std::string_view scene, const std::string_view message,
  const std::span<const instantiation::SceneInstantiationIssue> issues)
  -> std::string
{
  auto event = json {
    { "event", "lifecycle" },
    { "state", to_string(state) },
  };
  if (!scene.empty()) {
    event["scene"] = scene;
  }
  if (!message.empty()) {
    event["message"] = message;
  }
  if (!issues.empty()) {
    auto list = json::array();
    for (const auto& issue : issues) {
      auto entry = json {
        { "scene", data::to_string(issue.scene) },
        { "message", issue.message },
      };
      if (issue.node_index.has_value()) {
        entry["node_index"] = *issue.node_index;
      }
      list.push_back(std::move(entry));
    }
    event["issues"] = std::move(list);
  }
  return event.dump(-1, ' ', false, json::error_handler_t::replace);
}

auto MakeLogEvent(const LogSeverity severity, const std::string_view channel,
  const std::string_view text) -> std::string
{
  const auto event = json {
    { "event", "log" },
    { "severity", to_string(severity) },
    { "channel", channel },
    { "text", text },
  };
  return event.dump(-1, ' ', false, json::error_handler_t::replace);
}

auto MakeDiagnosticEvent(const scripting::ScriptDiagnostic& diagnostic)
  -> std::string
{
  const auto event = json {
    { "event", "diagnostic" },
    { "diagnostic", json::parse(scripting::ToJson(diagnostic)) },
  };
  return event.dump(-1, ' ', false, json::error_handler_t::replace);
}

auto MakeGpuWorkEvent(const vortex::GpuWorkSummary& summary) -> std::string
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
  const auto event = json {
    { "event", "gpu_work" },
    {
      "summary",
      {
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
      },
    },
  };
  return event.dump(-1, ' ', false, json::error_handler_t::replace);
}

auto ParseRequest(const std::string_view line)
  -> std::expected<PlayerRequest, std::string>
{
  const auto document = json::parse(line, nullptr, false);
  if (document.is_discarded()) {
    return std::unexpected(std::string("the request is not valid JSON"));
  }
  if (const auto errors = RequestSchema().Validate(document); !errors.empty()) {
    return std::unexpected(fmt::format("the request does not match "
                                       "oxygen.player-requests.schema.json: {}",
      fmt::join(errors, "; ")));
  }

  const auto kind = document.at("request").get<std::string>();
  if (kind == "play") {
    auto request = PlayRequest {};
    if (const auto it = document.find("scene"); it != document.end()) {
      request.scene = it->get<std::string>();
    }
    return request;
  }
  if (kind == "pause") {
    return PauseRequest {};
  }
  if (kind == "resume") {
    return ResumeRequest {};
  }
  if (kind == "step") {
    return StepRequest {
      .frames = document.value("frames", std::uint32_t { 1U }),
    };
  }
  if (kind == "end_session") {
    return EndSessionRequest {};
  }
  if (kind == "stop") {
    return StopRequest {};
  }
  return StatsRequest { .gpu_work = document.at("gpu_work").get<bool>() };
}

} // namespace oxygen::player
