//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Content/DependencyRequirementError.h>
#include <Oxygen/Content/SourceKeyConflictError.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Player/PlayerProtocol.h>
#include <Oxygen/Player/SchemaCheck.h>
#include <Oxygen/SceneInstantiation/SceneInstantiation.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>

namespace {

using nlohmann::json;
using oxygen::player::DiagnosticData;
using oxygen::player::EventSchema;
using oxygen::player::GpuWorkData;
using oxygen::player::kStatsTopic;
using oxygen::player::LifecycleData;
using oxygen::player::LifecycleState;
using oxygen::player::StatsData;

//! Checks event data is valid for its topic against the player's event
//! schema, and returns it.
auto CheckEvent(const std::string& topic, const json& data) -> json
{
  const auto event = json { { "topic", topic }, { "data", data } };
  const auto errors = EventSchema().Validate(event);
  EXPECT_TRUE(errors.empty()) << event.dump() << "\n"
                              << (errors.empty() ? "" : errors.front());
  return data;
}

NOLINT_TEST(PlayerProtocolTest, LifecycleEventsMatchTheSchema)
{
  namespace issue_codes = oxygen::instantiation::issue_codes;
  const auto bulb = oxygen::data::NodeId::FromStableIdentity("/test/Bulb");
  const auto started = CheckEvent("lifecycle",
    LifecycleData(LifecycleState::kSessionStarted, "/Scenes/A.oscene"));
  EXPECT_EQ(started["state"], "session_started");
  EXPECT_EQ(started["scene"], "/Scenes/A.oscene");
  EXPECT_FALSE(started.contains("issues"));

  const auto issues
    = std::vector<oxygen::instantiation::SceneInstantiationIssue> {
        {
          .code = std::string(issue_codes::kChangeUnresolved),
          .scene = oxygen::data::AssetKey::FromVirtualPath("/Scenes/A.oscene"),
          .node_index = 3U,
          .node_path = { bulb },
          .message = "unresolved change",
        },
        {
          .code = std::string(issue_codes::kStartingCameraMissing),
          .scene = oxygen::data::AssetKey::FromVirtualPath("/Scenes/A.oscene"),
          .node_index = std::nullopt,
          .node_path = {},
          .message = "no starting camera",
        },
      };
  const auto failed = CheckEvent("lifecycle",
    LifecycleData(LifecycleState::kSceneFailed, "/Scenes/A.oscene",
      "the scene has no camera", issues));
  EXPECT_EQ(failed["message"], "the scene has no camera");
  ASSERT_EQ(failed["issues"].size(), 2U);
  EXPECT_EQ(failed["issues"][0]["code"].get<std::string>(),
    issue_codes::kChangeUnresolved);
  EXPECT_EQ(failed["issues"][0]["node_index"], 3);
  EXPECT_EQ(failed["issues"][0]["node_path"][0], oxygen::data::to_string(bulb));
  EXPECT_FALSE(failed["issues"][1].contains("node_index"));
  EXPECT_FALSE(failed["issues"][1].contains("node_path"));

  // A model scene is refused with its code.
  const auto refused = CheckEvent("lifecycle",
    LifecycleData(LifecycleState::kSceneFailed, "/Scenes/Model.oscene",
      "the scene is a model", {}, oxygen::player::kModelSceneCode));
  EXPECT_EQ(
    refused["code"].get<std::string>(), oxygen::player::kModelSceneCode);

  for (const auto state : {
         LifecycleState::kSceneLoaded,
         LifecycleState::kPaused,
         LifecycleState::kResumed,
         LifecycleState::kSessionEnded,
         LifecycleState::kStopping,
       }) {
    static_cast<void>(CheckEvent("lifecycle", LifecycleData(state)));
  }
}

//! The `scene_failed` event of a launch mount refused with `error`.
auto MountFailedEvent(const std::exception_ptr& error) -> json
{
  auto data = LifecycleData(LifecycleState::kSceneFailed, "/Scenes/A.oscene",
    "the cooked content could not be mounted", {},
    oxygen::player::MountFailureCode(error));
  if (auto refusal = oxygen::player::MountRefusalData(error);
    !refusal.is_null()) {
    data["refusal"] = std::move(refusal);
  }
  return CheckEvent("lifecycle", data);
}

//! A pak whose requirements fail names each one in the refusal.
NOLINT_TEST(PlayerProtocolTest, UnmetRequirementsAreListedInTheRefusal)
{
  const auto material
    = oxygen::data::AssetKey::FromVirtualPath("/Engine/Default.omat");
  const auto texture
    = oxygen::data::TextureKeyFromVirtualPath("/Engine/Meter.otex");
  const auto error = std::make_exception_ptr(
    oxygen::content::DependencyRequirementError("Content.pak",
      {
        {
          .mount = "Engine",
          .requirement = { .kind = oxygen::data::KeyReferenceKind::kAsset,
            .key = material,
            .type = oxygen::data::AssetType::kMaterial, },
          .failure = oxygen::content::RequirementFailure::kTypeMismatch,
          .found_type = oxygen::data::AssetType::kGeometry,
          .found_mount = {},
        },
        {
          .mount = "Engine",
          .requirement = { .kind = oxygen::data::KeyReferenceKind::kTexture,
            .key = texture,
            .type = oxygen::data::AssetType::kUnknown, },
          .failure = oxygen::content::RequirementFailure::kMissing,
          .found_type = std::nullopt,
          .found_mount = {},
        },
      }));

  const auto event = MountFailedEvent(error);

  EXPECT_EQ(
    event["code"].get<std::string>(), oxygen::player::kDependencyMissingCode);
  EXPECT_EQ(event["refusal"]["source"], "Content.pak");
  const auto& requirements = event["refusal"]["requirements"];
  ASSERT_EQ(requirements.size(), 2U);
  EXPECT_EQ(requirements[0]["kind"], "asset");
  EXPECT_EQ(requirements[0]["key"], oxygen::data::to_string(material));
  EXPECT_EQ(requirements[0]["failure"], "type_mismatch");
  EXPECT_EQ(requirements[0]["found_type"],
    oxygen::data::to_string(oxygen::data::AssetType::kGeometry));
  EXPECT_EQ(requirements[1]["kind"], "texture");
  EXPECT_EQ(requirements[1]["failure"], "missing");
  EXPECT_FALSE(requirements[1].contains("type"));
}

//! A source whose SourceKey is taken names both sources in the refusal.
NOLINT_TEST(PlayerProtocolTest, SourceKeyConflictNamesBothSources)
{
  const auto error
    = std::make_exception_ptr(oxygen::content::SourceKeyConflictError(
      oxygen::data::SourceKey {}, "B.pak", "A.pak"));

  const auto event = MountFailedEvent(error);

  EXPECT_EQ(
    event["code"].get<std::string>(), oxygen::player::kSourceKeyConflictCode);
  EXPECT_EQ(event["refusal"]["source"], "B.pak");
  EXPECT_EQ(event["refusal"]["existing_source"], "A.pak");
}

//! Every other mount failure has its own code and no refusal.
NOLINT_TEST(PlayerProtocolTest, EveryMountFailureHasACode)
{
  using oxygen::data::CookedDefect;
  using oxygen::data::CookedOpenError;
  const auto cases
    = std::vector<std::pair<std::exception_ptr, std::string_view>> {
        {
          std::make_exception_ptr(
            CookedOpenError(CookedDefect::kAbsent, "gone")),
          oxygen::player::kContentMissingCode,
        },
        {
          std::make_exception_ptr(
            CookedOpenError(CookedDefect::kDigestMismatch, "corrupt")),
          oxygen::player::kContentRefusedCode,
        },
        {
          std::make_exception_ptr(std::system_error(
            std::make_error_code(std::errc::permission_denied), "denied")),
          oxygen::player::kContentUnreadableCode,
        },
        {
          std::make_exception_ptr(std::runtime_error("other")),
          oxygen::player::kMountFailedCode,
        },
      };

  for (const auto& [error, code] : cases) {
    const auto event = MountFailedEvent(error);
    EXPECT_EQ(event["code"].get<std::string>(), code);
    EXPECT_FALSE(event.contains("refusal"));
  }
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
      .hook = "compile", },
  };

  const auto data = CheckEvent("diagnostic", DiagnosticData(diagnostic));

  EXPECT_EQ(data["message"], "unexpected symbol");
}

NOLINT_TEST(PlayerProtocolTest, StatsEventsCarryWholeFramesPerSecond)
{
  const auto out = CheckEvent(std::string(kStatsTopic), StatsData(60U));

  EXPECT_EQ(out["fps"], 60);
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
        .time = { .avg_ms = 2.0F, .max_ms = 3.0F }, }, },
    }, },
  });
  summary.frame_work.push_back({
    .bucket = GpuWorkBucket::kBackground,
    .time = { .avg_ms = 1.0F, .max_ms = 1.0F },
    .items = {},
  });
  summary.frame_gpu_history_ms = { 5.0F, 6.0F };

  const auto out = CheckEvent("gpu_work", GpuWorkData(summary));

  EXPECT_EQ(
    out["schema_version"], oxygen::vortex::GpuWorkSummary::kSchemaVersion);
  EXPECT_EQ(out["revision"], 7);
  EXPECT_EQ(out["views"][0]["groups"][0]["bucket"], "base_pass");
  EXPECT_EQ(out["frame_work"][0]["bucket"], "background");
  EXPECT_EQ(out["frame_gpu_history_ms"].size(), 2U);
}

} // namespace
