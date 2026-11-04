//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportLimits.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/ImportSourceAnalysis.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/InputImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/InputImportSettings.h>
#include <Oxygen/Cooker/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/Internal/TextureImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/ScriptImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/ScriptImportSettings.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::CapturedInputSet;
using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportFormat;
using oxygen::content::import::ImportManifest;
using oxygen::content::import::ImportManifestJob;
using oxygen::content::import::ImportManifestJobOrigin;
using oxygen::content::import::ImportSourceDocument;
using oxygen::content::import::InputImportSettings;
using oxygen::content::import::kDefaultMaxSourceBytes;
using oxygen::content::import::SceneImportSettings;
using oxygen::content::import::ScriptAssetImportSettings;
using oxygen::content::import::TextureImportSettings;
using oxygen::cooker::test::DiagnosticSummary;
namespace internal = oxygen::content::import::internal;
namespace request_codes = oxygen::content::import::request_codes;

auto AbsoluteRoot() -> std::string
{
  return std::filesystem::absolute("cooked").string();
}

auto ValidSceneSettings() -> SceneImportSettings
{
  auto settings = SceneImportSettings {};
  settings.source_path = "model.gltf";
  settings.virtual_path = "/Content/model.gltf";
  settings.cooked_root = AbsoluteRoot();
  return settings;
}

//! The one refusal of `diagnostics` has `code` at `pointer`.
auto ExpectRefusal(const std::vector<ImportDiagnostic>& diagnostics,
  const std::string_view code, const std::string_view pointer) -> void
{
  ASSERT_EQ(diagnostics.size(), 1U) << DiagnosticSummary(diagnostics);
  EXPECT_EQ(diagnostics.front().code, code);
  EXPECT_EQ(diagnostics.front().json_pointer, pointer);
}

//! A setting a model import refuses, by the manifest key that states it.
struct SceneRefusal final {
  std::string_view code;
  std::string_view pointer;
  std::function<void(SceneImportSettings&)> apply;
};

NOLINT_TEST(RequestRefusalsTest, AModelsRefusedSettingHasItsCodeAndPointer)
{
  const auto cases = std::vector<SceneRefusal> {
    {
      .code = request_codes::kVirtualPathMissing,
      .pointer = "/virtual_path",
      .apply = [](auto& s) -> void { s.virtual_path.clear(); },
    },
    {
      .code = request_codes::kSourceFormatMismatch,
      .pointer = "/source",
      .apply = [](auto& s) -> void { s.source_path = "model.fbx"; },
    },
    {
      .code = request_codes::kOptionInvalid,
      .pointer = "/unit_policy",
      .apply = [](auto& s) -> void { s.unit_policy = "bad"; },
    },
    {
      .code = request_codes::kOptionInvalid,
      .pointer = "/up_axis",
      .apply = [](auto& s) -> void { s.up_axis = "bad"; },
    },
    {
      .code = request_codes::kOptionInvalid,
      .pointer = "/normals_policy",
      .apply = [](auto& s) -> void { s.normals_policy = "bad"; },
    },
    {
      .code = request_codes::kOptionInvalid,
      .pointer = "/tangents_policy",
      .apply = [](auto& s) -> void { s.tangents_policy = "bad"; },
    },
    {
      .code = request_codes::kOptionInvalid,
      .pointer = "/node_pruning",
      .apply = [](auto& s) -> void { s.node_pruning = "bad"; },
    },
    {
      .code = request_codes::kOptionOutOfRange,
      .pointer = "/gltf_omitted_light_range_m",
      .apply = [](auto& s) -> void { s.gltf_omitted_light_range_m = 0.0F; },
    },
    {
      .code = request_codes::kOptionOutOfRange,
      .pointer = "/light_intensity_scale",
      .apply = [](auto& s) -> void { s.light_intensity_scale = -1.0F; },
    },
    {
      .code = request_codes::kOptionOutOfRange,
      .pointer = "/normals_smoothing_angle_deg",
      .apply = [](auto& s) -> void { s.normals_smoothing_angle_deg = 181.0F; },
    },
    {
      .code = request_codes::kOptionOutOfRange,
      .pointer = "/lod_levels",
      .apply = [](auto& s) -> void { s.lod_levels = 0U; },
    },
    {
      .code = request_codes::kOptionOutOfRange,
      .pointer = "/lod_reduction_percent",
      .apply = [](auto& s) -> void { s.lod_reduction_percent = 0.0F; },
    },
    {
      .code = request_codes::kUnitScaleNeedsCustomPolicy,
      .pointer = "/unit_scale",
      .apply = [](auto& s) -> void { s.unit_scale_set = true; },
    },
    {
      .code = request_codes::kCookedRootMissing,
      .pointer = "/output",
      .apply = [](auto& s) -> void { s.cooked_root.clear(); },
    },
    {
      .code = request_codes::kCookedRootNotAbsolute,
      .pointer = "/output",
      .apply = [](auto& s) -> void { s.cooked_root = "relative/cooked"; },
    },
  };

  for (const auto& refusal : cases) {
    auto settings = ValidSceneSettings();
    refusal.apply(settings);
    auto diagnostics = std::vector<ImportDiagnostic> {};

    const auto request
      = internal::BuildSceneRequest(settings, ImportFormat::kGltf, diagnostics);

    EXPECT_FALSE(request.has_value()) << refusal.pointer;
    ExpectRefusal(diagnostics, refusal.code, refusal.pointer);
  }
}

NOLINT_TEST(RequestRefusalsTest, ATexturesRefusedOptionHasItsCodeAndPointer)
{
  using Member = std::string TextureImportSettings::*;
  const auto cases = std::vector<std::pair<Member, std::string_view>> {
    { &TextureImportSettings::preset, "/preset" },
    { &TextureImportSettings::intent, "/intent" },
    { &TextureImportSettings::color_space, "/color_space" },
    { &TextureImportSettings::output_format, "/output_format" },
    { &TextureImportSettings::data_format, "/data_format" },
    { &TextureImportSettings::mip_policy, "/mip_policy" },
    { &TextureImportSettings::mip_filter, "/mip_filter" },
    { &TextureImportSettings::bc7_quality, "/bc7_quality" },
    { &TextureImportSettings::hdr_handling, "/hdr_handling" },
    { &TextureImportSettings::cube_layout, "/cube_layout" },
  };

  for (const auto& [member, pointer] : cases) {
    auto settings = TextureImportSettings {};
    settings.source_path = "image.png";
    settings.cooked_root = AbsoluteRoot();
    settings.*member = "bad";
    auto diagnostics = std::vector<ImportDiagnostic> {};

    EXPECT_FALSE(
      internal::BuildTextureRequest(settings, diagnostics).has_value())
      << pointer;
    ExpectRefusal(diagnostics, request_codes::kOptionInvalid, pointer);
  }
}

NOLINT_TEST(RequestRefusalsTest, AScriptsRefusedOptionHasItsCodeAndPointer)
{
  auto settings = ScriptAssetImportSettings {};
  settings.source_path = "script.lua";
  settings.cooked_root = AbsoluteRoot();
  settings.compile_mode = "bad";
  auto diagnostics = std::vector<ImportDiagnostic> {};
  EXPECT_FALSE(
    internal::BuildScriptAssetRequest(settings, diagnostics).has_value());
  ExpectRefusal(diagnostics, request_codes::kOptionInvalid, "/compile_mode");

  settings.compile_mode = "debug";
  settings.script_storage = "bad";
  diagnostics.clear();
  EXPECT_FALSE(
    internal::BuildScriptAssetRequest(settings, diagnostics).has_value());
  ExpectRefusal(diagnostics, request_codes::kOptionInvalid, "/script_storage");
}

NOLINT_TEST(RequestRefusalsTest, ARelativeContextRootIsRefused)
{
  auto settings = InputImportSettings {};
  settings.source_path = "Player.input.json";
  settings.cooked_context_roots = { "relative/context" };
  auto diagnostics = std::vector<ImportDiagnostic> {};

  EXPECT_FALSE(
    internal::BuildInputImportRequest(settings, diagnostics).has_value());
  ExpectRefusal(diagnostics, request_codes::kContextRootNotAbsolute,
    "/cooked_context_roots");
}

NOLINT_TEST(RequestRefusalsTest, AnOverridesRefusalIsLocatedUnderItsName)
{
  auto settings = ValidSceneSettings();
  auto& override_settings = settings.texture_overrides["a/b"];
  override_settings.output_format = "rgba8";
  override_settings.bc7_quality = "high";
  auto diagnostics = std::vector<ImportDiagnostic> {};

  EXPECT_FALSE(
    internal::BuildSceneRequest(settings, ImportFormat::kGltf, diagnostics)
      .has_value());
  ExpectRefusal(diagnostics, request_codes::kBc7QualityNeedsBc7Format,
    "/texture_overrides/a~1b/bc7_quality");
}

//! A refusal of a job's settings names the manifest and the job's key.
NOLINT_TEST(RequestRefusalsTest, AJobLocatesItsSettingsRefusalInItsManifest)
{
  auto job = ImportManifestJob {};
  job.job_type = "gltf";
  job.origin = ImportManifestJobOrigin {
    .source_path = "project.manifest.json",
    .index = 2U,
  };
  job.gltf = ValidSceneSettings();
  job.gltf.unit_scale_set = true;
  auto diagnostics = std::vector<ImportDiagnostic> {};

  EXPECT_FALSE(job.BuildRequest(diagnostics).has_value());
  ExpectRefusal(diagnostics, request_codes::kUnitScaleNeedsCustomPolicy,
    "/jobs/2/unit_scale");
  EXPECT_EQ(diagnostics.front().source_path, "project.manifest.json");
}

//! A job's document keeps its own location: the job is not about that pointer.
NOLINT_TEST(RequestRefusalsTest, AJobLeavesADocumentsRefusalWhereItIs)
{
  auto job = ImportManifestJob {};
  job.job_type = "material-descriptor";
  job.origin = ImportManifestJobOrigin {
    .source_path = "project.manifest.json",
    .index = 2U,
  };
  job.material_descriptor.descriptor_path = "missing.material.json";
  job.material_descriptor.cooked_root = AbsoluteRoot();
  auto diagnostics = std::vector<ImportDiagnostic> {};

  EXPECT_FALSE(job.BuildRequest(diagnostics).has_value());
  ExpectRefusal(diagnostics, request_codes::kDocumentReadFailed, "");
  EXPECT_EQ(diagnostics.front().source_path, "missing.material.json");
}

//! `BuildRequests` keeps every job's refusal, each located by its own job.
NOLINT_TEST(RequestRefusalsTest, EveryRefusedJobReportsAtItsOwnIndex)
{
  auto manifest = ImportManifest {};
  for (const auto index : { 0U, 1U }) {
    auto job = ImportManifestJob {};
    job.job_type = "gltf";
    job.origin = ImportManifestJobOrigin { .source_path = {}, .index = index };
    job.gltf = ValidSceneSettings();
    job.gltf.unit_scale_set = true;
    manifest.jobs.push_back(std::move(job));
  }
  auto diagnostics = std::vector<ImportDiagnostic> {};

  EXPECT_TRUE(manifest.BuildRequests(diagnostics).empty());
  ASSERT_EQ(diagnostics.size(), 2U) << DiagnosticSummary(diagnostics);
  EXPECT_EQ(diagnostics.at(0).json_pointer, "/jobs/0/unit_scale");
  EXPECT_EQ(diagnostics.at(1).json_pointer, "/jobs/1/unit_scale");
}

//! What a job's settings refuse, the cook and source analysis refuse alike.
struct ManifestRule final {
  std::string_view name;
  std::string_view code;
  std::string_view pointer;
  std::function<void(ImportManifestJob&)> apply;
};

NOLINT_TEST(RequestRefusalsTest, AnalysisRefusesWhatTheCookRefusesWithOneCode)
{
  const auto cases = std::vector<ManifestRule> {
    {
      .name = "model source type",
      .code = request_codes::kSourceFormatMismatch,
      .pointer = "/jobs/2/source",
      .apply = [](auto& job) -> void {
        job.job_type = "gltf";
        job.gltf = ValidSceneSettings();
        job.gltf.source_path = "model.fbx";
      },
    },
    {
      .name = "unit scale policy",
      .code = request_codes::kUnitScaleNeedsCustomPolicy,
      .pointer = "/jobs/2/unit_scale",
      .apply = [](auto& job) -> void {
        job.job_type = "gltf";
        job.gltf = ValidSceneSettings();
        job.gltf.unit_scale_set = true;
      },
    },
    {
      .name = "omitted light range",
      .code = request_codes::kOptionOutOfRange,
      .pointer = "/jobs/2/gltf_omitted_light_range_m",
      .apply = [](auto& job) -> void {
        job.job_type = "gltf";
        job.gltf = ValidSceneSettings();
        job.gltf.gltf_omitted_light_range_m
          = std::numeric_limits<float>::denorm_min();
      },
    },
    {
      .name = "BC7 quality format",
      .code = request_codes::kBc7QualityNeedsBc7Format,
      .pointer = "/jobs/2/bc7_quality",
      .apply = [](auto& job) -> void {
        job.job_type = "texture";
        job.texture.source_path = "image.png";
        job.texture.virtual_path = "/Content/image.otex";
        job.texture.cooked_root = AbsoluteRoot();
        job.texture.output_format = "rgba8";
        job.texture.bc7_quality = "high";
      },
    },
    {
      .name = "input source name",
      .code = request_codes::kInputSourceNameInvalid,
      .pointer = "/jobs/2/source",
      .apply = [](auto& job) -> void {
        job.job_type = "input";
        job.input.source_path = "Player.json";
      },
    },
    {
      .name = "script sidecar target",
      .code = request_codes::kTargetSceneNotCanonical,
      .pointer = "/jobs/2/target_scene_virtual_path",
      .apply = [](auto& job) -> void {
        job.job_type = "script-sidecar";
        job.scripting_sidecar.inline_bindings_json
          = R"({"bindings":[{"node_index":0,"slot_id":"00000000-0000-4000-8000-00000000000a","script_virtual_path":"/Scripts/test.oscript"}]})";
        job.scripting_sidecar.cooked_root = AbsoluteRoot();
        job.scripting_sidecar.target_scene_virtual_path = "not/canonical/..";
      },
    },
    {
      .name = "physics sidecar target",
      .code = request_codes::kTargetSceneMissing,
      .pointer = "/jobs/2/target_scene_virtual_path",
      .apply = [](auto& job) -> void {
        job.job_type = "physics-sidecar";
        job.physics_sidecar.inline_bindings_json
          = R"({"bindings":{"rigid_bodies":[{"node_index":0,"shape_ref":"/Cooked/Physics/Shapes/test_shape.ocshape","material_ref":"/Cooked/Physics/Materials/default.opmat"}]}})";
      },
    },
  };

  for (const auto& rule : cases) {
    auto job = ImportManifestJob {};
    job.origin = ImportManifestJobOrigin {
      .source_path = "project.manifest.json",
      .index = 2U,
    };
    rule.apply(job);
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(job.BuildRequest(diagnostics).has_value()) << rule.name;
    ExpectRefusal(diagnostics, rule.code, rule.pointer);

    auto manifest = ImportManifest {};
    manifest.jobs.push_back(std::move(job));
    const auto analysis = manifest.AnalyzeSources();

    ASSERT_EQ(analysis.jobs.size(), 1U) << rule.name;
    EXPECT_FALSE(analysis.complete) << rule.name;
    ExpectRefusal(analysis.jobs.front().diagnostics, rule.code, rule.pointer);
  }
}

class DocumentRefusalsTest : public testing::Test {
protected:
  [[nodiscard]] static auto PathText(const std::filesystem::path& path)
    -> std::string
  {
    return path.generic_string();
  }

  [[nodiscard]] static auto Path() -> std::filesystem::path
  {
    return std::filesystem::path(testing::TempDir()) / "refused.document.json";
  }

  static auto Write(const std::string_view text) -> void
  {
    auto output = std::ofstream(Path(), std::ios::binary);
    output << text;
  }

  //! A capture of `Path()` that claims the bytes of `recorded`.
  [[nodiscard]] static auto CaptureOf(const std::string_view recorded)
    -> std::shared_ptr<const CapturedInputSet>
  {
    const auto digest = oxygen::base::ComputeSha256(
      std::as_bytes(std::span(recorded.data(), recorded.size())));
    auto hex = std::string {};
    for (const auto byte : digest) {
      hex += fmt::format("{:02x}", byte);
    }
    return CapturedInputSet::Parse(nlohmann::json {
      { "schema_version", 1 },
      {
        "inputs",
        nlohmann::json::array({
          {
            { "logical_path", PathText(Path()) },
            { "exists", true },
            {
              "metadata",
              {
                { "size", recorded.size() },
                { "is_directory", false },
                { "is_symlink", false },
                { "last_modified_seconds", 0 },
                { "last_modified_nanoseconds", 0 },
              },
            },
            {
              "file",
              {
                { "path", PathText(Path()) },
                { "size", recorded.size() },
                { "sha256", hex },
              },
            },
          },
        }),
      },
    }
        .dump());
  }
};

NOLINT_TEST_F(DocumentRefusalsTest, AMissingDocumentCannotBeRead)
{
  std::filesystem::remove(Path());
  auto diagnostics = std::vector<ImportDiagnostic> {};

  EXPECT_FALSE(
    ImportSourceDocument::Load(Path(), "test", diagnostics).has_value());

  ExpectRefusal(diagnostics, request_codes::kDocumentReadFailed, "");
  EXPECT_EQ(diagnostics.front().source_path, Path().string());
}

NOLINT_TEST_F(DocumentRefusalsTest, ADocumentOverTheSourceLimitIsTooLarge)
{
  Write("{}");
  std::filesystem::resize_file(Path(), kDefaultMaxSourceBytes.get() + 1U);
  auto diagnostics = std::vector<ImportDiagnostic> {};

  EXPECT_FALSE(
    ImportSourceDocument::Load(Path(), "test", diagnostics).has_value());
  std::filesystem::resize_file(Path(), 0U);

  ExpectRefusal(diagnostics, request_codes::kDocumentTooLarge, "");
}

NOLINT_TEST_F(DocumentRefusalsTest, ADocumentTheCaptureLacksIsNotCaptured)
{
  Write("{}");
  const auto capture = CapturedInputSet::Parse(nlohmann::json {
    { "schema_version", 1 },
    { "inputs", nlohmann::json::array() },
  }
      .dump());
  auto diagnostics = std::vector<ImportDiagnostic> {};

  EXPECT_FALSE(
    ImportSourceDocument::Load(Path(), "test", diagnostics, capture.get())
      .has_value());

  ExpectRefusal(diagnostics, request_codes::kDocumentNotCaptured, "");
}

NOLINT_TEST_F(DocumentRefusalsTest, ADocumentThatChangedMismatchesItsCapture)
{
  const auto capture = CaptureOf("{\"a\":1}");

  // Another size is refused before the read, the same size after it.
  for (const auto* text : { "{}", "{\"a\":2}" }) {
    Write(text);
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(
      ImportSourceDocument::Load(Path(), "test", diagnostics, capture.get())
        .has_value());
    ExpectRefusal(diagnostics, request_codes::kDocumentCaptureMismatch, "");
  }
}

} // namespace
