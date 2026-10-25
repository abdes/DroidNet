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
#include <sstream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Pak/PakTestSupport.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::Uuid;
using oxygen::content::import::ImportFormat;
using oxygen::content::import::ImportManifest;
using oxygen::content::import::ModelImportRecord;
using oxygen::content::import::ModelImportRecordContent;
using oxygen::content::import::ModelNodeContinuity;
using oxygen::content::import::ModelPublishedNode;
using oxygen::content::import::SceneImportSettings;
using oxygen::content::import::SurfaceProvenance;
using oxygen::content::import::internal::BuildSceneRequest;
using oxygen::data::NodeId;

constexpr std::string_view kSourceIdentity
  = "01990000-0000-7000-8000-000000000001";

auto Identity() -> Uuid { return Uuid::FromString(kSourceIdentity).value(); }

class SceneImportRequestBuilderTest
  : public oxygen::content::pak::test::TempDirFixture {
protected:
  auto Settings() -> SceneImportSettings
  {
    auto settings = SceneImportSettings {};
    settings.source_path = Path("model.gltf").string();
    settings.virtual_path = "/.cooked/model.gltf";
    settings.cooked_root = Path("cooked").string();
    return settings;
  }

  //! A record of `source` named by `identity`, in the temp content root.
  auto SaveRecord(const std::string_view source,
    const std::string_view virtual_path, const Uuid& identity,
    const json& settings = json::object()) -> std::filesystem::path
  {
    const auto path = ModelImportRecord::PathFor(Root(), identity);
    ModelImportRecord::Save({
      .record_path = path,
      .content_root = Root(),
      .source = Path(source),
      .virtual_path = std::string(virtual_path),
      .settings_json = settings.dump(),
    });
    return path;
  }
};

//! Without a record, an import has an identity of its own, fresh on every
//! run, and no node history.
NOLINT_TEST_F(SceneImportRequestBuilderTest, WithoutARecordTheIdentityIsFresh)
{
  std::ostringstream errors;
  const auto first = BuildSceneRequest(Settings(), ImportFormat::kGltf, errors);
  const auto second
    = BuildSceneRequest(Settings(), ImportFormat::kGltf, errors);

  ASSERT_HAS_VALUE(first) << errors.str();
  ASSERT_HAS_VALUE(second) << errors.str();
  ASSERT_NE(first->surface_provenance, nullptr);
  ASSERT_NE(second->surface_provenance, nullptr);
  EXPECT_TRUE(first->surface_provenance->SourceIdentity().IsValidV7());
  EXPECT_NE(first->surface_provenance->SourceIdentity(),
    second->surface_provenance->SourceIdentity());
  EXPECT_FALSE(first->model_import.has_value());
}

//! A job's source sits in the folder of what it cooks: the index records it as
//! the producer of the job's entries.
NOLINT_TEST_F(SceneImportRequestBuilderTest, TheSourceSitsBesideWhatItCooks)
{
  auto request = oxygen::content::import::ImportRequest {};
  request.source_path = Path("anywhere") / "Red.omat.json";
  request.asset_virtual_path = "/Content/Materials/Red.omat";
  EXPECT_EQ(request.SourceVirtualPath(), "/Content/Materials/Red.omat.json");

  request.source_path = Path("anywhere") / "Player.input.json";
  request.asset_virtual_path = "/Content/Player.input";
  EXPECT_EQ(request.SourceVirtualPath(), "/Content/Player.input.json");

  request.asset_virtual_path.clear();
  EXPECT_TRUE(request.SourceVirtualPath().empty());
}

//! A model request carries the model's identity, which names its scene.
NOLINT_TEST_F(SceneImportRequestBuilderTest, CarriesTheModelIdentity)
{
  std::ostringstream errors;
  const auto request
    = BuildSceneRequest(Settings(), ImportFormat::kGltf, errors);
  ASSERT_HAS_VALUE(request) << errors.str();
  EXPECT_EQ(request->asset_virtual_path, "/.cooked/model.gltf");
  EXPECT_EQ(request->GetSceneName(), "model");
  EXPECT_EQ(request->SourceVirtualPath(), "/.cooked/model.gltf")
    << "a model is its own source";
  EXPECT_EQ(request->ModelProductVirtualPath(
              request->GetSceneName(), oxygen::data::kSceneDescriptorExtension),
    "/.cooked/model.gltf/model.oscene");

  auto settings = Settings();
  settings.virtual_path.clear();
  std::ostringstream refused;
  EXPECT_FALSE(
    BuildSceneRequest(settings, ImportFormat::kGltf, refused).has_value());
  EXPECT_THAT(
    refused.str(), testing::HasSubstr("import.asset_virtual_path_missing"));
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

  std::ostringstream errors;
  const auto request = BuildSceneRequest(settings, ImportFormat::kGltf, errors);

  ASSERT_HAS_VALUE(request) << errors.str();
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
  std::ostringstream errors;
  const auto request = settings.Prepare(ImportFormat::kGltf, errors);
  ASSERT_HAS_VALUE(request) << errors.str();
  EXPECT_FALSE(request->cooked_root.has_value());
  EXPECT_EQ(request->source_path, std::filesystem::path(settings.source_path));
}

//! A `model` job runs its record as a gltf or fbx job, by the source's
//! extension: the record's settings, source, virtual path and identity.
NOLINT_TEST_F(SceneImportRequestBuilderTest, AModelJobRunsItsRecord)
{
  const auto gltf_identity = Identity();
  const auto fbx_identity
    = Uuid::FromString("01990000-0000-7000-8000-000000000002").value();
  const auto gltf
    = SaveRecord("model.gltf", "/.cooked/model.gltf", gltf_identity,
      json {
        { "name", "the_gltf" },
        { "gltf_scene", "Interior" },
      });
  const auto fbx = SaveRecord("model.fbx", "/.cooked/model.fbx", fbx_identity);
  const auto manifest_path = Path("import-manifest.json");
  {
    const auto document = json {
      { "version", 1 },
      { "output", Path("cooked").string() },
      {
        "jobs",
        json::array({
          {
            { "type", "model" },
            { "record", gltf.lexically_relative(Root()).generic_string() },
          },
          {
            { "id", "the-fbx" },
            { "type", "model" },
            { "record", fbx.generic_string() },
          },
        }),
      },
    };
    std::ofstream output(manifest_path);
    output << document.dump();
    ASSERT_TRUE(output.good());
  }

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);

  ASSERT_HAS_VALUE(manifest) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs[0].job_type, "gltf");
  EXPECT_EQ(manifest->jobs[1].job_type, "fbx");
  EXPECT_EQ(manifest->jobs[1].id, "the-fbx");
  const auto first = manifest->jobs[0].BuildRequest(errors);
  const auto second = manifest->jobs[1].BuildRequest(errors);
  ASSERT_HAS_VALUE(first) << errors.str();
  ASSERT_HAS_VALUE(second) << errors.str();
  EXPECT_EQ(first->asset_virtual_path, "/.cooked/model.gltf");
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
  std::ostringstream errors;
  const auto request = settings.Prepare(ImportFormat::kGltf, errors);
  ASSERT_HAS_VALUE(request) << errors.str();
  EXPECT_EQ(request->options.gltf_scene, "#2");
  EXPECT_EQ(
    SceneImportSettings::FromOptions(request->options).gltf_scene, "#2");
}

//! Identity and provenance belong to the record: a job cannot carry them, and
//! a `model` job carries nothing of the record's.
NOLINT_TEST_F(SceneImportRequestBuilderTest, JobsCarryNoIdentityOfTheirOwn)
{
  const auto record
    = SaveRecord("model.gltf", "/.cooked/model.gltf", Identity());
  const auto refused = [](const json& job) -> std::string {
    const auto document = json {
      { "version", 1 },
      { "jobs", json::array({ job }) },
    };
    std::ostringstream errors;
    EXPECT_FALSE(ImportManifest::Parse(
      document.dump(), std::filesystem::current_path(), std::nullopt, errors)
        .has_value())
      << job.dump();
    return errors.str();
  };

  static_cast<void>(refused({
    { "type", "gltf" },
    { "source", "model.gltf" },
    { "virtual_path", "/.cooked/model.gltf" },
    { "surface_source_identity", kSourceIdentity },
  }));
  static_cast<void>(refused({
    { "type", "model" },
    { "record", record.generic_string() },
    { "unit_policy", "preserve" },
  }));
  EXPECT_THAT(refused({
                { "type", "model" },
                { "record", Path("missing.import.json").generic_string() },
              }),
    testing::HasSubstr("import.model_record_invalid"));
}

//! The model import settings a record holds reach the import: up axis,
//! smoothing angle, light and camera flags, intensity scale, texture size,
//! material remap and per-geometry defaults; the record writes them back.
NOLINT_TEST_F(SceneImportRequestBuilderTest, ModelImportSettingsReachTheImport)
{
  using oxygen::content::import::SourceUpAxis;
  using oxygen::data::SurfaceId;
  using oxygen::data::pak::geometry::LodPolicyType;
  const auto surface = std::string { "01990000-0000-7000-8000-0000000000aa" };
  const auto settings_json = json {
    { "up_axis", "z" },
    { "normals_smoothing_angle_deg", 30 },
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
  const auto record
    = SaveRecord("model.fbx", "/.cooked/model.fbx", Identity(), settings_json);
  const auto manifest_path = Path("settings-manifest.json");
  {
    std::ofstream output(manifest_path);
    output << json {
      { "version", 1 },
      { "output", Path("cooked").string() },
      {
        "jobs",
        json::array({ { { "type", "model" }, { "record", record.string() } } }),
      },
    }.dump();
    ASSERT_TRUE(output.good());
  }

  std::ostringstream errors;
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_HAS_VALUE(manifest) << errors.str();
  const auto request = manifest->jobs.at(0).BuildRequest(errors);
  ASSERT_HAS_VALUE(request) << errors.str();

  const auto& options = request->options;
  EXPECT_EQ(options.coordinate.up_axis, SourceUpAxis::kZ);
  EXPECT_FLOAT_EQ(options.normals_smoothing_angle_deg, 30.0F);
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
  EXPECT_EQ(written.at("max_size"), 1024);
  EXPECT_EQ(written.at("material_remap"), settings_json.at("material_remap"));
  EXPECT_EQ(written.at("geometry_defaults").at("Body").at("surfaces"),
    settings_json.at("geometry_defaults").at("Body").at("surfaces"));
  EXPECT_EQ(
    written.at("geometry_defaults").at("Body").at("lod_policy").at("lod_index"),
    1);
}

//! Without settings, the import takes the engine defaults, the same as the
//! editor's: the file's up axis, 60 degrees, lights and cameras at scale 1,
//! source texture size.
NOLINT_TEST_F(SceneImportRequestBuilderTest, ModelImportSettingsDefaults)
{
  using oxygen::content::import::SourceUpAxis;
  std::ostringstream errors;
  const auto request = Settings().Prepare(ImportFormat::kGltf, errors);
  ASSERT_HAS_VALUE(request) << errors.str();
  EXPECT_EQ(request->options.coordinate.up_axis, SourceUpAxis::kFromFile);
  EXPECT_FLOAT_EQ(request->options.normals_smoothing_angle_deg, 60.0F);
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
//! above 180 degrees, an unknown up axis, a LOD policy whose thresholds
//! decrease, and a surface key that is not a surface ID.
NOLINT_TEST_F(
  SceneImportRequestBuilderTest, InvalidModelImportSettingsAreRefused)
{
  auto angle = Settings();
  angle.normals_smoothing_angle_deg = 181.0F;
  auto axis = Settings();
  axis.up_axis = "x";
  for (const auto& settings : { angle, axis }) {
    std::ostringstream errors;
    EXPECT_FALSE(settings.Prepare(ImportFormat::kGltf, errors).has_value());
  }

  const auto refused = [&](const json& defaults) -> bool {
    const auto path = Path("refused-manifest.json");
    {
      std::ofstream output(path);
      output << json {
        { "version", 1 },
        { "output", Path("cooked").string() },
        {
          "jobs",
          json::array({ {
            { "type", "fbx" },
            { "source", Path("model.fbx").string() },
            { "virtual_path", "/.cooked/model.fbx" },
            { "geometry_defaults", defaults },
          }, }),
        },
      }.dump();
    }
    std::ostringstream errors;
    return !ImportManifest::Load(path, std::nullopt, errors).has_value();
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
