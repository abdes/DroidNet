//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Player/EventStream.h>
#include <Oxygen/Player/PlayerProtocol.h>
#include <Oxygen/Player/SchemaCheck.h>
#include <Oxygen/SceneInstantiation/SceneInstantiation.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>

namespace {

using nlohmann::json;
using oxygen::player::ClassifyLogLine;
using oxygen::player::EndSessionRequest;
using oxygen::player::EventSchema;
using oxygen::player::LifecycleState;
using oxygen::player::LogSeverity;
using oxygen::player::MakeDiagnosticEvent;
using oxygen::player::MakeGpuWorkEvent;
using oxygen::player::MakeLifecycleEvent;
using oxygen::player::MakeLogEvent;
using oxygen::player::ParseRequest;
using oxygen::player::PauseRequest;
using oxygen::player::PlayRequest;
using oxygen::player::ResumeRequest;
using oxygen::player::StatsRequest;
using oxygen::player::StepRequest;
using oxygen::player::StopRequest;

//! Parses an event line, checks it is one line and valid against the event
//! schema, and returns it.
auto CheckEvent(const std::string& line) -> json
{
  EXPECT_EQ(line.find('\n'), std::string::npos);
  auto event = json::parse(line);
  const auto errors = EventSchema().Validate(event);
  EXPECT_TRUE(errors.empty()) << line << "\n" << errors.front();
  return event;
}

NOLINT_TEST(PlayerProtocolTest, LifecycleEventsMatchTheSchema)
{
  const auto started = CheckEvent(
    MakeLifecycleEvent(LifecycleState::kSessionStarted, "/Scenes/A.oscene"));
  EXPECT_EQ(started["state"], "session_started");
  EXPECT_EQ(started["scene"], "/Scenes/A.oscene");
  EXPECT_FALSE(started.contains("issues"));

  const auto issues
    = std::vector<oxygen::instantiation::SceneInstantiationIssue> {
        { .scene = oxygen::data::AssetKey::FromVirtualPath("/Scenes/A.oscene"),
          .node_index = 3U,
          .message = "unsupported record" },
        { .scene = oxygen::data::AssetKey::FromVirtualPath("/Scenes/A.oscene"),
          .node_index = std::nullopt,
          .message = "no starting camera" },
      };
  const auto failed
    = CheckEvent(MakeLifecycleEvent(LifecycleState::kSceneFailed,
      "/Scenes/A.oscene", "the scene has no camera", issues));
  EXPECT_EQ(failed["message"], "the scene has no camera");
  ASSERT_EQ(failed["issues"].size(), 2U);
  EXPECT_EQ(failed["issues"][0]["node_index"], 3);
  EXPECT_FALSE(failed["issues"][1].contains("node_index"));

  for (const auto state :
    { LifecycleState::kStarting, LifecycleState::kSceneLoaded,
      LifecycleState::kPaused, LifecycleState::kResumed,
      LifecycleState::kSessionEnded, LifecycleState::kStopped }) {
    static_cast<void>(CheckEvent(MakeLifecycleEvent(state)));
  }
}

NOLINT_TEST(PlayerProtocolTest, LogEventsMatchTheSchemaAndKeepLineBreaks)
{
  const auto event = CheckEvent(
    MakeLogEvent(LogSeverity::kWarning, "script", "first\nsecond \"quoted\""));
  EXPECT_EQ(event["severity"], "warning");
  EXPECT_EQ(event["channel"], "script");
  EXPECT_EQ(event["text"], "first\nsecond \"quoted\"");
}

NOLINT_TEST(PlayerProtocolTest, DiagnosticEventsCarryTheDiagnosticJson)
{
  const auto diagnostic = oxygen::scripting::ScriptDiagnostic {
    .severity = oxygen::scripting::DiagnosticSeverity::kError,
    .code = oxygen::scripting::DiagnosticCode::kCompileError,
    .message = "unexpected symbol",
    .labels = {},
    .notes = {},
    .help = std::nullopt,
    .slot = oxygen::scripting::SlotContext { .node = "Cube",
      .slot_index = 0U,
      .hook = "compile" },
  };

  const auto event = CheckEvent(MakeDiagnosticEvent(diagnostic));

  EXPECT_EQ(event["diagnostic"]["message"], "unexpected symbol");
}

NOLINT_TEST(PlayerProtocolTest, GpuWorkEventsMirrorTheSummary)
{
  using oxygen::vortex::GpuWorkBucket;
  auto summary = oxygen::vortex::GpuWorkSummary {};
  summary.revision = 7U;
  summary.window_ms = 500.0F;
  summary.valid_frames = 30U;
  summary.frame_gpu = { .avg_ms = 6.0F, .max_ms = 8.0F };
  summary.views.push_back({
    .view_id = 2U,
    .name = "MainView",
    .time = { .avg_ms = 5.0F, .max_ms = 7.0F },
    .groups = { {
      .bucket = GpuWorkBucket::kBasePass,
      .time = { .avg_ms = 2.0F, .max_ms = 3.0F },
      .items = { { .id = "base_pass.opaque",
        .name = "Opaque",
        .time = { .avg_ms = 2.0F, .max_ms = 3.0F } } },
    } },
  });
  summary.frame_work.push_back({
    .bucket = GpuWorkBucket::kBackground,
    .time = { .avg_ms = 1.0F, .max_ms = 1.0F },
    .items = {},
  });
  summary.frame_gpu_history_ms = { 5.0F, 6.0F };

  const auto event = CheckEvent(MakeGpuWorkEvent(summary));

  const auto& out = event["summary"];
  EXPECT_EQ(
    out["schema_version"], oxygen::vortex::GpuWorkSummary::kSchemaVersion);
  EXPECT_EQ(out["revision"], 7);
  EXPECT_EQ(out["views"][0]["groups"][0]["bucket"], "base_pass");
  EXPECT_EQ(out["frame_work"][0]["bucket"], "background");
  EXPECT_EQ(out["frame_gpu_history_ms"].size(), 2U);
}

NOLINT_TEST(PlayerProtocolTest, ParsesEveryRequest)
{
  const auto play = ParseRequest(R"({"request":"play"})");
  ASSERT_TRUE(play.has_value());
  EXPECT_FALSE(std::get<PlayRequest>(*play).scene.has_value());

  const auto play_scene
    = ParseRequest(R"({"request":"play","scene":"/Scenes/B.oscene"})");
  ASSERT_TRUE(play_scene.has_value());
  EXPECT_EQ(std::get<PlayRequest>(*play_scene).scene, "/Scenes/B.oscene");

  EXPECT_TRUE(std::holds_alternative<PauseRequest>(
    ParseRequest(R"({"request":"pause"})").value()));
  EXPECT_TRUE(std::holds_alternative<ResumeRequest>(
    ParseRequest(R"({"request":"resume"})").value()));
  EXPECT_EQ(
    std::get<StepRequest>(ParseRequest(R"({"request":"step"})").value()).frames,
    1U);
  EXPECT_EQ(std::get<StepRequest>(
              ParseRequest(R"({"request":"step","frames":4})").value())
              .frames,
    4U);
  EXPECT_TRUE(std::holds_alternative<EndSessionRequest>(
    ParseRequest(R"({"request":"end_session"})").value()));
  EXPECT_TRUE(std::holds_alternative<StopRequest>(
    ParseRequest(R"({"request":"stop"})").value()));
  EXPECT_TRUE(std::get<StatsRequest>(
    ParseRequest(R"({"request":"stats","gpu_work":true})").value())
      .gpu_work);
}

NOLINT_TEST(PlayerProtocolTest, RejectsMalformedRequests)
{
  EXPECT_FALSE(ParseRequest("play").has_value());
  EXPECT_FALSE(ParseRequest(R"({"request":"jump"})").has_value());
  EXPECT_FALSE(ParseRequest(R"({"request":"stats"})").has_value());
  EXPECT_FALSE(ParseRequest(R"({"request":"step","frames":0})").has_value());
  EXPECT_FALSE(ParseRequest(R"({"request":"pause","frames":2})").has_value());
  EXPECT_FALSE(ParseRequest(R"({"request":"play","scene":"B"})").has_value());
}

NOLINT_TEST(PlayerProtocolTest, ScriptLogLinesUseTheScriptChannel)
{
  const auto script = ClassifyLogLine(
    "H:/Oxygen/Scripting/LogBindings.cpp", "[lua.info] hello world");
  EXPECT_EQ(script.channel, "script");
  EXPECT_EQ(script.text, "hello world");

  const auto engine = ClassifyLogLine(
    "H:\\Oxygen\\Content\\AssetLoader.cpp", "mounted '/cooked'");
  EXPECT_EQ(engine.channel, "AssetLoader");
  EXPECT_EQ(engine.text, "mounted '/cooked'");
}

} // namespace
