//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/SceneImportRequestBuilder.cpp

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Import/ModelImportRecordError.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnostic;

using nlohmann::json;
using oxygen::Uuid;
using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportFormat;
using oxygen::content::import::ImportManifest;
using oxygen::content::import::LodGenerationOptions;
using oxygen::content::import::ModelImportRecord;
using oxygen::content::import::ModelImportRecordContent;
using oxygen::content::import::ModelNodeContinuity;
using oxygen::content::import::ModelPublishedNode;
using oxygen::content::import::MountDeclaration;
using oxygen::content::import::MountTable;
using oxygen::content::import::MountTableRequest;
using oxygen::content::import::ResolveMountTable;
using oxygen::content::import::SceneImportSettings;
using oxygen::content::import::SurfaceProvenance;
using oxygen::content::import::internal::BuildSceneRequest;
using oxygen::data::NodeId;

constexpr std::string_view kSourceIdentity
  = "01990000-0000-7000-8000-000000000001";

auto Identity() -> Uuid { return Uuid::FromString(kSourceIdentity).value(); }

class SceneImportRequestBuilderTest : public oxygen::testing::TempDirTest {
protected:
  //! The project's mounts: `Content` on the temp root, where the sources,
  //! records and manifests of these tests live.
  auto Mounts() -> const MountTable&
  {
    if (mounts_.mounts.empty()) {
      mounts_ = ResolveMountTable(MountTableRequest {
        .project_root = TempDir(),
        .declarations
        = { MountDeclaration { .name = "Content", .path = std::string(".") } },
        .declared_in = "import-manifest.json",
        .working_directory = TempDir(),
      });
    }
    return mounts_;
  }

  //! The manifest fields every manifest of these tests declares.
  static auto ProjectFields(json document) -> json
  {
    document["mounts"]
      = json::array({ { { "name", "Content" }, { "path", "." } } });
    document["output"] = "cooked";
    return document;
  }

  auto Settings() -> SceneImportSettings
  {
    auto settings = SceneImportSettings {};
    settings.source_path = TempPath("model.gltf").string();
    settings.virtual_path = "/Cooked/model.gltf";
    settings.cooked_root = TempPath("cooked").string();
    return settings;
  }

  //! A new record of the model `identity`, at that identity in the temp
  //! root's `Content` mount, imported from the file of the same name beside
  //! it.
  auto SaveRecord(const std::string_view identity,
    const json& settings = json::object()) -> std::filesystem::path
  {
    auto findings = std::vector<ImportDiagnostic> {};
    const auto path
      = ModelImportRecord::RecordPathOf(Mounts(), identity, findings);
    if (!path.has_value()) {
      throw std::invalid_argument(
        "No record location for " + std::string(identity));
    }
    ModelImportRecord::Save({
      .record_path = *path,
      .source = TempPath(std::filesystem::path(identity).filename().string()),
      .settings_json = settings.dump(),
      .mounts = oxygen::make_observer(&Mounts()),
    });
    return *path;
  }

private:
  MountTable mounts_;
};

//! Without a record, an import has an identity of its own, fresh on every
//! run, and no node history.
NOLINT_TEST_F(SceneImportRequestBuilderTest, WithoutARecordTheIdentityIsFresh)
{
  std::vector<ImportDiagnostic> errors;
  const auto first = BuildSceneRequest(Settings(), ImportFormat::kGltf, errors);
  const auto second
    = BuildSceneRequest(Settings(), ImportFormat::kGltf, errors);

  ASSERT_HAS_VALUE(first) << DiagnosticSummary(errors);
  ASSERT_HAS_VALUE(second) << DiagnosticSummary(errors);
  ASSERT_NE(first->surface_provenance, nullptr);
  ASSERT_NE(second->surface_provenance, nullptr);
  EXPECT_TRUE(first->surface_provenance->SourceIdentity().IsValidV7());
  EXPECT_NE(first->surface_provenance->SourceIdentity(),
    second->surface_provenance->SourceIdentity());
  EXPECT_FALSE(first->model_import.has_value());
}

//! A job records its source as the mount path of the file, whatever folder the
//! job's identity names.
NOLINT_TEST_F(SceneImportRequestBuilderTest, TheSourceIsRecordedAsItsMountPath)
{
  std::filesystem::create_directories(TempPath("Materials"));
  {
    std::ofstream output(TempPath("Materials/Red.omat.json"));
    output << R"({ "name": "Red" })";
    ASSERT_TRUE(output.good());
  }
  const auto manifest_path = TempPath("import-manifest.json");
  {
    std::ofstream output(manifest_path);
    output << ProjectFields({
                              { "version", 1 },
                              {
                                "jobs",
                                json::array({
                                  {
                                    { "type", "material-descriptor" },
                                    { "source", "Materials/Red.omat.json" },
                                    { "virtual_path", "/Cooked/Look/Red.omat" },
                                  },
                                }),
                              },
                            })
                .dump();
    ASSERT_TRUE(output.good());
  }

  std::vector<ImportDiagnostic> errors;
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  const auto request = manifest->jobs.at(0).BuildRequest(errors);

  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_EQ(request->source_mount_path, "/Content/Materials/Red.omat.json");
}

//! A model request carries the model's identity, which names its scene.
NOLINT_TEST_F(SceneImportRequestBuilderTest, CarriesTheModelIdentity)
{
  std::vector<ImportDiagnostic> errors;
  const auto request
    = BuildSceneRequest(Settings(), ImportFormat::kGltf, errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_EQ(request->asset_virtual_path, "/Cooked/model.gltf");
  EXPECT_EQ(request->GetSceneName(), "model");
  EXPECT_EQ(request->ModelProductVirtualPath(
              request->GetSceneName(), oxygen::data::kSceneDescriptorExtension),
    "/Cooked/model.gltf/model.oscene");

  auto settings = Settings();
  settings.virtual_path.clear();
  std::vector<ImportDiagnostic> refused;
  EXPECT_FALSE(
    BuildSceneRequest(settings, ImportFormat::kGltf, refused).has_value());
  EXPECT_THAT(refused,
    HasDiagnostic(oxygen::content::import::request_codes::kVirtualPathMissing));
}

//! The record supplies the identity, the provenance and what node continuity
//! needs: the continuity entries and the previously published nodes.
NOLINT_TEST_F(SceneImportRequestBuilderTest, TheRecordSuppliesTheModelsHistory)
{
  auto record = std::make_shared<ModelImportRecordContent>();
  record->source_identity = Identity();
  record->surface_provenance
    = std::make_shared<const SurfaceProvenance>(Identity());
  record->node_continuity = {
    ModelNodeContinuity {
      .anchor = "Body/Door_Front_L",
      .continues = "Body/Door_FL",
    },
  };
  record->published_nodes = {
    ModelPublishedNode {
      .anchor = "Body",
      .node_id = NodeId::FromStableIdentity("Body"),
    },
  };
  auto settings = Settings();
  settings.model_record = record;

  std::vector<ImportDiagnostic> errors;
  const auto request = BuildSceneRequest(settings, ImportFormat::kGltf, errors);

  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_EQ(request->surface_provenance, record->surface_provenance);
  const auto& model_import = request->model_import;
  ASSERT_HAS_VALUE(model_import);
  EXPECT_EQ(model_import->node_continuity, record->node_continuity);
  EXPECT_EQ(model_import->published_nodes, record->published_nodes);
}

//! Preparing a request reads no file and attaches no destination.
NOLINT_TEST_F(SceneImportRequestBuilderTest, PreparesWithoutDestinationOrFileIo)
{
  auto settings = Settings();
  settings.cooked_root.clear();
  std::vector<ImportDiagnostic> errors;
  const auto request = settings.Prepare(ImportFormat::kGltf, errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_FALSE(request->cooked_root.has_value());
  EXPECT_EQ(request->source_path, std::filesystem::path(settings.source_path));
}

//! A `model` job names its model by identity and runs the model's record as a
//! gltf or fbx job, by the source's extension: the record's settings, source
//! and source identity, at the job's identity.
NOLINT_TEST_F(SceneImportRequestBuilderTest, AModelJobRunsItsRecord)
{
  const auto gltf = SaveRecord("/Content/model.gltf",
    json {
      { "name", "the_gltf" },
      { "gltf_scene", "Interior" },
    });
  const auto fbx = SaveRecord("/Content/model.fbx");
  const auto gltf_identity
    = ModelImportRecord::Read(gltf, Mounts()).source_identity;
  const auto fbx_identity
    = ModelImportRecord::Read(fbx, Mounts()).source_identity;
  const auto manifest_path = TempPath("import-manifest.json");
  {
    const auto document = ProjectFields({
      { "version", 1 },
      {
        "jobs",
        json::array({
          {
            { "type", "model" },
            { "virtual_path", "/Content/model.gltf" },
          },
          {
            { "id", "the-fbx" },
            { "type", "model" },
            { "virtual_path", "/Content/model.fbx" },
          },
        }),
      },
    });
    std::ofstream output(manifest_path);
    output << document.dump();
    ASSERT_TRUE(output.good());
  }

  std::vector<ImportDiagnostic> errors;
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);

  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs[0].job_type, "gltf");
  EXPECT_EQ(manifest->jobs[1].job_type, "fbx");
  EXPECT_EQ(manifest->jobs[1].id, "the-fbx");
  const auto first = manifest->jobs[0].BuildRequest(errors);
  const auto second = manifest->jobs[1].BuildRequest(errors);
  ASSERT_HAS_VALUE(first) << DiagnosticSummary(errors);
  ASSERT_HAS_VALUE(second) << DiagnosticSummary(errors);
  EXPECT_EQ(first->asset_virtual_path, "/Content/model.gltf");
  EXPECT_EQ(first->job_name, "the_gltf");
  EXPECT_EQ(first->options.gltf_scene, "Interior");
  EXPECT_EQ(first->surface_provenance->SourceIdentity(), gltf_identity);
  EXPECT_TRUE(first->model_import.has_value());
  EXPECT_EQ(second->GetFormat(), ImportFormat::kFbx);
  EXPECT_EQ(second->surface_provenance->SourceIdentity(), fbx_identity);
}

//! The chosen glTF scene is a setting: the record keeps it only when chosen,
//! and an import from the settings asks for it.
NOLINT_TEST_F(SceneImportRequestBuilderTest, TheChosenGltfSceneIsASetting)
{
  auto settings = Settings();
  EXPECT_FALSE(json::parse(ModelImportRecord::MakeSettings(settings))
      .contains("gltf_scene"));
  settings.gltf_scene = "#2";
  EXPECT_EQ(json::parse(ModelImportRecord::MakeSettings(settings))
              .value("gltf_scene", std::string {}),
    "#2");
  std::vector<ImportDiagnostic> errors;
  const auto request = settings.Prepare(ImportFormat::kGltf, errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_EQ(request->options.gltf_scene, "#2");
  EXPECT_EQ(
    SceneImportSettings::FromOptions(request->options).gltf_scene, "#2");
}

//! Identity and provenance belong to the record: a job cannot carry them, a
//! `model` job carries nothing of the record's, and a model without a record
//! is refused.
NOLINT_TEST_F(SceneImportRequestBuilderTest, JobsCarryNoIdentityOfTheirOwn)
{
  static_cast<void>(SaveRecord("/Content/model.gltf"));
  const auto refused
    = [this](const json& job) -> std::vector<ImportDiagnostic> {
    const auto document = ProjectFields({
      { "version", 1 },
      { "jobs", json::array({ job }) },
    });
    std::vector<ImportDiagnostic> errors;
    EXPECT_FALSE(
      ImportManifest::Parse(document.dump(), TempDir(), {}, errors).has_value())
      << job.dump();
    return errors;
  };

  static_cast<void>(refused({
    { "type", "gltf" },
    { "source", "model.gltf" },
    { "virtual_path", "/Cooked/model.gltf" },
    { "surface_source_identity", kSourceIdentity },
  }));
  static_cast<void>(refused({
    { "type", "model" },
    { "virtual_path", "/Content/model.gltf" },
    { "unit_policy", "preserve" },
  }));
  EXPECT_THAT(refused({
                { "type", "model" },
                { "virtual_path", "/Content/missing.gltf" },
              }),
    HasDiagnostic(oxygen::content::import::model_record_codes::kUnreadable));
}

//! The model import settings a record holds reach the import: up axis,
//! smoothing angle, LOD generation, light and camera flags, intensity scale,
//! texture size, material remap and per-geometry defaults; the record writes
//! them back.
NOLINT_TEST_F(SceneImportRequestBuilderTest, ModelImportSettingsReachTheImport)
{
  using oxygen::content::import::SourceUpAxis;
  using oxygen::data::SurfaceId;
  using oxygen::data::pak::geometry::LodPolicyType;
  const auto surface = std::string { "01990000-0000-7000-8000-0000000000aa" };
  const auto settings_json = json {
    { "up_axis", "z" },
    { "normals_smoothing_angle_deg", 30 },
    { "generate_lods", true },
    { "lod_levels", 5 },
    { "lod_reduction_percent", 40 },
    { "import_lights", false },
    { "import_cameras", false },
    { "light_intensity_scale", 2.5 },
    { "max_size", 1024 },
    { "material_remap", { { "Paint", "/Content/Materials/Red.omat" } } },
    {
      "texture_overrides",
      { { "Paint.base_color", { { "max_size", 512 } } } },
    },
    {
      "geometry_defaults",
      {
        {
          "Body",
          {
            { "lod_policy", { { "type", "fixed" }, { "lod_index", 1 } } },
            {
              "surfaces",
              {
                {
                  surface,
                  { { "visible", false }, { "casts_shadows", false } },
                },
              },
            },
          },
        },
      },
    },
  };
  static_cast<void>(SaveRecord("/Content/model.fbx", settings_json));
  const auto manifest_path = TempPath("settings-manifest.json");
  {
    std::ofstream output(manifest_path);
    output << ProjectFields({
                              { "version", 1 },
                              {
                                "jobs",
                                json::array({
                                  {
                                    { "type", "model" },
                                    { "virtual_path", "/Content/model.fbx" },
                                  },
                                }),
                              },
                            })
                .dump();
    ASSERT_TRUE(output.good());
  }

  std::vector<ImportDiagnostic> errors;
  const auto manifest = ImportManifest::Load(manifest_path, {}, errors);
  ASSERT_HAS_VALUE(manifest) << DiagnosticSummary(errors);
  const auto request = manifest->jobs.at(0).BuildRequest(errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);

  const auto& options = request->options;
  EXPECT_EQ(options.coordinate.up_axis, SourceUpAxis::kZ);
  EXPECT_FLOAT_EQ(options.normals_smoothing_angle_deg, 30.0F);
  EXPECT_TRUE(options.lod_generation.enabled);
  EXPECT_EQ(options.lod_generation.levels, 5U);
  EXPECT_FLOAT_EQ(options.lod_generation.reduction_percent, 40.0F);
  EXPECT_FALSE(options.import_lights);
  EXPECT_FALSE(options.import_cameras);
  EXPECT_FLOAT_EQ(options.light_intensity_scale, 2.5F);
  EXPECT_EQ(options.texture_tuning.max_size, 1024U);
  ASSERT_TRUE(options.texture_overrides.contains("Paint.base_color"));
  EXPECT_EQ(options.texture_overrides.at("Paint.base_color").max_size, 512U);
  EXPECT_EQ(options.material_remap.at("Paint"), "/Content/Materials/Red.omat");
  ASSERT_TRUE(options.geometry_defaults.contains("Body"));
  const auto& body = options.geometry_defaults.at("Body");
  ASSERT_HAS_VALUE(body.lod_policy);
  EXPECT_EQ(body.lod_policy->type, LodPolicyType::kFixed);
  EXPECT_EQ(body.lod_policy->lod_index, 1U);
  const auto parsed_id = SurfaceId::FromString(surface);
  ASSERT_HAS_VALUE(parsed_id);
  const auto id = parsed_id.value();
  ASSERT_TRUE(body.surfaces.contains(id));
  EXPECT_FALSE(body.surfaces.at(id).visible.value_or(true));
  EXPECT_FALSE(body.surfaces.at(id).casts_shadows.value_or(true));

  // The record writes back what it read.
  const auto written = json::parse(
    ModelImportRecord::MakeSettings(SceneImportSettings::FromOptions(options)));
  EXPECT_EQ(written.at("up_axis"), "z");
  EXPECT_EQ(written.at("import_lights"), false);
  EXPECT_EQ(written.at("generate_lods"), true);
  EXPECT_EQ(written.at("lod_levels"), 5);
  EXPECT_EQ(written.at("lod_reduction_percent"), 40);
  EXPECT_EQ(written.at("max_size"), 1024);
  EXPECT_EQ(written.at("material_remap"), settings_json.at("material_remap"));
  EXPECT_EQ(written.at("geometry_defaults").at("Body").at("surfaces"),
    settings_json.at("geometry_defaults").at("Body").at("surfaces"));
  EXPECT_EQ(
    written.at("geometry_defaults").at("Body").at("lod_policy").at("lod_index"),
    1);
}

//! Without settings, the import takes the engine defaults, the same as the
//! editor's: the file's up axis, 60 degrees, no generated LODs, lights and
//! cameras at scale 1, source texture size.
NOLINT_TEST_F(SceneImportRequestBuilderTest, ModelImportSettingsDefaults)
{
  using oxygen::content::import::SourceUpAxis;
  std::vector<ImportDiagnostic> errors;
  const auto request = Settings().Prepare(ImportFormat::kGltf, errors);
  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_EQ(request->options.coordinate.up_axis, SourceUpAxis::kFromFile);
  EXPECT_FLOAT_EQ(request->options.normals_smoothing_angle_deg, 60.0F);
  const auto lods = LodGenerationOptions {};
  EXPECT_FALSE(request->options.lod_generation.enabled);
  EXPECT_EQ(request->options.lod_generation.levels, lods.levels);
  EXPECT_FLOAT_EQ(
    request->options.lod_generation.reduction_percent, lods.reduction_percent);
  EXPECT_TRUE(request->options.import_lights);
  EXPECT_TRUE(request->options.import_cameras);
  EXPECT_FLOAT_EQ(request->options.light_intensity_scale, 1.0F);
  EXPECT_EQ(request->options.texture_tuning.max_size, 0U);
  const auto written = json::parse(ModelImportRecord::MakeSettings(
    SceneImportSettings::FromOptions(request->options)));
  EXPECT_FALSE(written.contains("up_axis"));
  EXPECT_FALSE(written.contains("material_remap"));
  EXPECT_FALSE(written.contains("geometry_defaults"));
}

//! Settings whose values break their rules are refused: a smoothing angle
//! above 180 degrees, an unknown up axis, LOD levels or a reduction outside
//! their ranges, a LOD policy whose thresholds decrease, and a surface key
//! that is not a surface ID.
NOLINT_TEST_F(
  SceneImportRequestBuilderTest, InvalidModelImportSettingsAreRefused)
{
  auto angle = Settings();
  angle.normals_smoothing_angle_deg = 181.0F;
  auto axis = Settings();
  axis.up_axis = "x";
  auto no_levels = Settings();
  no_levels.lod_levels = LodGenerationOptions::kMinLevels - 1U;
  auto too_many_levels = Settings();
  too_many_levels.lod_levels = LodGenerationOptions::kMaxLevels + 1U;
  auto no_reduction = Settings();
  no_reduction.lod_reduction_percent = 0.0F;
  auto full_reduction = Settings();
  full_reduction.lod_reduction_percent = 100.0F;
  for (const auto& settings :
    { angle, axis, no_levels, too_many_levels, no_reduction, full_reduction }) {
    std::vector<ImportDiagnostic> errors;
    EXPECT_FALSE(settings.Prepare(ImportFormat::kGltf, errors).has_value());
  }

  const auto refused = [&](const json& defaults) -> bool {
    const auto path = TempPath("refused-manifest.json");
    {
      std::ofstream output(path);
      output << ProjectFields({
                                { "version", 1 },
                                {
                                  "jobs",
                                  json::array({
                                    {
                                      { "type", "fbx" },
                                      { "source", "model.fbx" },
                                      { "virtual_path", "/Cooked/model.fbx" },
                                      { "geometry_defaults", defaults },
                                    },
                                  }),
                                },
                              })
                  .dump();
    }
    std::vector<ImportDiagnostic> errors;
    return !ImportManifest::Load(path, {}, errors).has_value();
  };
  EXPECT_TRUE(refused({ { "Body",
    { { "lod_policy",
      { { "type", "distance" }, { "thresholds", { 20.0, 10.0 } },
        { "hysteresis_ratio", 0.1 } } } } } }));
  EXPECT_TRUE(refused({ { "Body",
    { { "surfaces", { { "not-an-id", { { "visible", true } } } } } } } }));
  EXPECT_FALSE(refused({ { "Body", json::object() } }));
}

} // namespace
