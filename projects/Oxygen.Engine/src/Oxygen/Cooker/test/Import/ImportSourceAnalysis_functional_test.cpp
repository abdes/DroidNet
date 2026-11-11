//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ImportSourceAnalysis.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportLimits.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportSourceAnalysis.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/CollisionShapeSource.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/InputSource.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsMaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/ScriptSource.h>
#include <Oxygen/Cooker/Import/Internal/ScriptingSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/SourceAnalysisRunner.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace oxygen::content::import::test {
namespace {

  using oxygen::content::import::ImportDiagnostic;
  using oxygen::cooker::test::DiagnosticSummary;
  class CancelVerificationReader final : public IAsyncFileReader {
  public:
    std::stop_source cancellation;
    size_t reads = 0;
    bool drained = false;

    auto ReadFile(const std::filesystem::path& /*path*/,
      ReadOptions /*options*/) -> co::Co<Result<serio::ByteBuffer>> override
    {
      ++reads;
      if (reads == 2U) {
        const auto drain
          = ScopeGuard([this] noexcept -> void { drained = true; });
        cancellation.request_stop();
        co_await pending_;
      }
      constexpr auto text = std::string_view { R"({"name":"Observed"})" };
      co_return serio::ByteBuffer::CopyOf(std::as_bytes(std::span(text)));
    }

    auto Exists(const std::filesystem::path& /*path*/)
      -> co::Co<Result<bool>> override
    {
      co_return Ok(true);
    }

    [[nodiscard]] auto SourceLimit() const noexcept -> serio::ByteCount override
    {
      return kDefaultMaxSourceBytes;
    }

    auto GetFileInfo(const std::filesystem::path& /*path*/)
      -> co::Co<Result<FileInfo>> override
    {
      co_return Ok(FileInfo {});
    }

  private:
    co::Event pending_;
  };

  class ImportSourceAnalysisTest : public oxygen::testing::TempDirTest {
  protected:
    auto Write(const std::filesystem::path& name, const std::string_view text)
      -> std::filesystem::path
    {
      const auto path = TempDir() / name;
      auto file = std::ofstream(path, std::ios::binary);
      file << text;
      file.close();
      EXPECT_TRUE(file.good());
      return path;
    }
  };

  //! Analyzes one material and one scene descriptor once for the whole suite;
  //! every test reads the same read-only report.
  class MixedDescriptorAnalysisTest : public ::testing::Test {
  protected:
    static auto SetUpTestSuite() -> void
    {
      temp_ = std::make_unique<oxygen::testing::ScopedTempDir>();
      try {
        Analyze();
      } catch (const std::exception& error) {
        error_ = std::string("Suite setup threw: ") + error.what();
      }
    }

    static auto TearDownTestSuite() -> void
    {
      report_.reset();
      error_.clear();
      temp_.reset();
    }

    static auto WriteFile(const std::filesystem::path& name,
      const std::string_view text) -> std::filesystem::path
    {
      const auto path = temp_->Path() / name;
      auto file = std::ofstream(path, std::ios::binary);
      file << text;
      file.close();
      if (!file.good()) {
        error_ = "Failed to write " + path.string();
      }
      return path;
    }

    static auto Analyze() -> void
    {
      namespace authored = oxygen::cooker::test;
      constexpr auto kMaterial = "/Cooked/Materials/Stone.omat";
      constexpr auto kScene = "/Cooked/Scenes/Scene.oscene";
      const auto material = WriteFile("material.json",
        authored::MaterialDescriptor { kMaterial }
          .Texture("base_color", "/Cooked/Textures/color.otex")
          .Texture("thickness", "/Cooked/Textures/thickness.otex")
          .Text());
      auto scene_document = authored::SceneDescriptor { kScene };
      scene_document.Set("/name", "Scene.v2")
        .Remove("/nodes/0/name")
        .Append("/nodes",
          {
            { "id", "00000000-0000-4000-8000-000000000002" },
            { "parent", 0 },
            {
              "instance",
              {
                {
                  "scene_ref",
                  scene_document.Spell("/Cooked/Props/Crate.glb/Crate.oscene"),
                },
                {
                  "overrides",
                  nlohmann::json::array({
                    {
                      { "path", { "00000000-0000-4000-8000-000000000003" } },
                      {
                        "components",
                        {
                          {
                            "geometry",
                            {
                              {
                                "parts",
                                nlohmann::json::array({
                                  {
                                    { "kind", "surface" },
                                    {
                                      "id",
                                      "00000000-0000-4000-8000-000000000004",
                                    },
                                    { "layout_revision", std::string(64, '0') },
                                    {
                                      "values",
                                      {
                                        {
                                          "/material",
                                          scene_document.Spell(
                                            "/Cooked/Materials/Red.omat"),
                                        },
                                      },
                                    },
                                  },
                                }),
                              },
                            },
                          },
                        },
                      },
                    },
                  }),
                },
              },
            },
          })
        .Renderable(0, "/Cooked/Geometry/body.ogeo")
        .Set("/starting_input_contexts",
          nlohmann::json::array({
            {
              {
                "context_ref",
                scene_document.Spell("/Cooked/Input/Gameplay.oimap"),
              },
              { "priority", 2 },
            },
          }))
        .Set("/references/scripts",
          nlohmann::json::array(
            { scene_document.Spell("/Cooked/Scripts/move.oscript") }))
        .Set("/environment/post_process_volume/auto_exposure_metering_mask",
          scene_document.Spell("/Cooked/Textures/mask.otex"));
      const auto scene = WriteFile("scene.json", scene_document.Text());
      auto manifest = ImportManifest {};
      auto material_job = ImportManifestJob {};
      material_job.id = "material";
      material_job.job_type = "material-descriptor";
      material_job.virtual_path = "/Cooked/Materials/Stone.omat";
      material_job.material_descriptor.descriptor_path = material.string();
      manifest.jobs.push_back(material_job);
      auto scene_job = ImportManifestJob {};
      scene_job.id = "scene";
      scene_job.job_type = "scene-descriptor";
      scene_job.virtual_path = "/Cooked/Scenes/Scene.oscene";
      scene_job.scene_descriptor.descriptor_path = scene.string();
      manifest.jobs.push_back(scene_job);
      report_ = manifest.AnalyzeSources();
    }

    static inline std::unique_ptr<oxygen::testing::ScopedTempDir> temp_;
    static inline std::string error_;
    static inline std::optional<ImportSourceAnalysis> report_;
  };

  NOLINT_TEST_F(MixedDescriptorAnalysisTest,
    MixedDescriptorsExposeNativeReferencesAndOutputs)
  {
    ASSERT_TRUE(error_.empty()) << error_;
    if (!report_.has_value()) {
      FAIL() << "Suite setup produced no report";
    }
    const auto& report = report_.value();
    ASSERT_TRUE(report.complete) << report.ToJson();
    ASSERT_EQ(report.jobs.size(), 2U);
    EXPECT_EQ(report.jobs.front().references.size(), 2U);
    EXPECT_EQ(report.jobs.back().references.size(), 6U);
    EXPECT_EQ(report.jobs.back().outputs.front().virtual_path,
      "/Cooked/Scenes/Scene.oscene");
  }

  //! A scene's starting input contexts must be cooked before it, so they are
  //! required references like its other assets.
  NOLINT_TEST_F(MixedDescriptorAnalysisTest,
    SceneStartingInputContextsAreRequiredReferences)
  {
    ASSERT_TRUE(error_.empty()) << error_;
    if (!report_.has_value()) {
      FAIL() << "Suite setup produced no report";
    }
    const auto& report = report_.value();
    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& references = report.jobs.back().references;
    const auto context
      = std::ranges::find_if(references, [](const auto& reference) -> bool {
          return reference.virtual_path == "/Cooked/Input/Gameplay.oimap";
        });
    ASSERT_NE(context, references.end()) << report.ToJson();
    EXPECT_EQ(context->kind, ImportDependencyKind::kInputMappingContext);
    EXPECT_EQ(context->json_pointer, "/starting_input_contexts/0/context_ref");
    EXPECT_TRUE(context->required);
  }

  //! A scene's instanced scenes must be cooked before it, since its cook
  //! composes them, and the assets its instance changes name are references
  //! like its renderables' overrides.
  NOLINT_TEST_F(MixedDescriptorAnalysisTest,
    SceneInstancesAndTheirChangedAssetsAreRequiredReferences)
  {
    ASSERT_TRUE(error_.empty()) << error_;
    if (!report_.has_value()) {
      FAIL() << "Suite setup produced no report";
    }
    const auto& report = report_.value();
    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& references = report.jobs.back().references;
    const auto find = [&references](const std::string_view path) -> auto {
      return std::ranges::find_if(
        references, [path](const auto& reference) -> bool {
          return reference.virtual_path == path;
        });
    };
    const auto scene = find("/Cooked/Props/Crate.glb/Crate.oscene");
    ASSERT_NE(scene, references.end()) << report.ToJson();
    EXPECT_EQ(scene->kind, ImportDependencyKind::kScene);
    EXPECT_EQ(scene->json_pointer, "/nodes/1/instance/scene_ref");
    EXPECT_TRUE(scene->required);
    const auto material = find("/Cooked/Materials/Red.omat");
    ASSERT_NE(material, references.end()) << report.ToJson();
    EXPECT_EQ(material->kind, ImportDependencyKind::kMaterial);
    EXPECT_TRUE(material->required);
  }

  NOLINT_TEST_F(
    MixedDescriptorAnalysisTest, MixedDescriptorsRecordMetadataOnlyReadProofs)
  {
    ASSERT_TRUE(error_.empty()) << error_;
    if (!report_.has_value()) {
      FAIL() << "Suite setup produced no report";
    }
    const auto& report = report_.value();
    ASSERT_TRUE(report.complete) << report.ToJson();
    ASSERT_EQ(report.jobs.size(), 2U);
    for (const auto& job : report.jobs) {
      ASSERT_EQ(job.observations.size(), 1U);
      ASSERT_EQ(job.observations.front().reads.size(), 1U);
      EXPECT_EQ(job.observations.front().reads.front().max_bytes, 0U);
    }
  }

  NOLINT_TEST_F(
    MixedDescriptorAnalysisTest, MixedDescriptorsAnalysisWritesNoCookedOutput)
  {
    ASSERT_TRUE(error_.empty()) << error_;
    if (!report_.has_value()) {
      FAIL() << "Suite setup produced no report";
    }
    const auto& report = report_.value();
    ASSERT_TRUE(report.complete) << report.ToJson();
    EXPECT_FALSE(std::filesystem::exists(temp_->Path() / ".cooked"));
  }

  NOLINT_TEST_F(
    MixedDescriptorAnalysisTest, MixedDescriptorsReportSerializesVersionAndJobs)
  {
    ASSERT_TRUE(error_.empty()) << error_;
    if (!report_.has_value()) {
      FAIL() << "Suite setup produced no report";
    }
    const auto& report = report_.value();
    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto serialized = nlohmann::json::parse(report.ToJson());
    EXPECT_EQ(serialized.at("schema_version"), kSourceAnalysisSchemaVersion);
    EXPECT_EQ(serialized.at("jobs").size(), 2U);
  }

  NOLINT_TEST_F(
    MixedDescriptorAnalysisTest, MixedDescriptorsReportValidatesAgainstSchema)
  {
    ASSERT_TRUE(error_.empty()) << error_;
    if (!report_.has_value()) {
      FAIL() << "Suite setup produced no report";
    }
    const auto& report = report_.value();
    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto serialized = nlohmann::json::parse(report.ToJson());
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Import/Schemas/oxygen.source-analysis.schema.json");
    EXPECT_THAT(oxygen::cooker::test::ValidateJson(schema, serialized),
      ::testing::IsEmpty());
  }

  NOLINT_TEST_F(ImportSourceAnalysisTest,
    GeometrySeparatesLocalOutputsFromExternalReferences)
  {
    static_cast<void>(Write("vertices.bin", "not decoded during analysis"));
    constexpr auto kBody = "/Content/Body.ogeo";
    auto body = cooker::test::GeometryDescriptor { kBody };
    const auto bounds = nlohmann::json {
      { "min", { -1, -1, -1 } },
      { "max", { 1, 1, 1 } },
    };
    body.Set("/bounds", bounds)
      .AppendBuffer(
        { { "source", "vertices.bin" } }, "/Content/Buffers/local.obuf")
      .Set("/lods/0/mesh_type", "standard")
      .Remove("/lods/0/procedural")
      .Set("/lods/0/bounds", bounds)
      .Reference("/lods/0/buffers/vb_ref", "/Content/Buffers/local.obuf")
      .Reference("/lods/0/buffers/ib_ref", "/Content/Buffers/external.obuf")
      .SubmeshMaterial(0, 0, "/Content/Materials/stone.omat");
    const auto geometry = Write("geometry.json", body.Text());
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "geometry-descriptor";
    job.virtual_path = "/Content/Body.ogeo";
    job.mounts = oxygen::cooker::test::ContentMountTable(TempDir());
    job.geometry_descriptor.descriptor_path = geometry.string();
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& result = report.jobs.front();
    EXPECT_EQ(result.outputs.size(), 2U);
    EXPECT_EQ(result.references.size(), 2U);
    EXPECT_FALSE(
      std::ranges::any_of(result.references, [](const auto& reference) -> auto {
        return reference.virtual_path == "/Content/Buffers/local.obuf";
      }));
    const auto buffer = std::ranges::find(result.observations,
      TempPath("vertices.bin"), &ImportSourceObservation::path);
    ASSERT_NE(buffer, result.observations.end());
    EXPECT_TRUE(buffer->reads.empty());
  }

  //! An input source is the only input of its job; every declared action and
  //! mapping context is an output named under the source's identity, and an
  //! action of another source named by virtual path is a reference.
  NOLINT_TEST_F(
    ImportSourceAnalysisTest, InputFileDeclaresItsActionsAndContextsAsOutputs)
  {
    constexpr auto kPlayer = "/Content/Gameplay/Player.input";
    auto player = cooker::test::InputSource { kPlayer };
    player.Action("Jump", "bool")
      .Action("Dash", "bool")
      .Context("Gameplay")
      .Append("/contexts/0/mappings",
        { { "action", "Jump" }, { "slot", "Space" }, { "trigger", "pressed" } })
      .Append("/contexts/0/mappings",
        {
          {
            "action",
            player.ActionReference("/Content/Common.input/Fire.oiact"),
          },
          { "slot", "F" },
        });
    const auto input = Write("Player.input.json", player.Text());
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.id = "input:Gameplay/Player.input.json";
    job.job_type = "input";
    job.input.source_path = input.string();
    job.virtual_path = "/Content/Gameplay/Player.input";
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& result = report.jobs.front();
    auto outputs = std::vector<std::string> {};
    std::ranges::transform(result.outputs, std::back_inserter(outputs),
      [](const auto& output) -> std::string { return output.virtual_path; });
    EXPECT_THAT(outputs,
      ::testing::ElementsAre("/Content/Gameplay/Player.input/Jump.oiact",
        "/Content/Gameplay/Player.input/Dash.oiact",
        "/Content/Gameplay/Player.input/Gameplay.oimap"));
    ASSERT_EQ(result.references.size(), 1U);
    EXPECT_EQ(result.references.front().virtual_path,
      "/Content/Common.input/Fire.oiact");
    EXPECT_EQ(
      result.references.front().json_pointer, "/contexts/0/mappings/1/action");
    ASSERT_EQ(result.observations.size(), 1U);
  }

  //! A script file is the only input of its job; the script asset at the job's
  //! identity is its output.
  NOLINT_TEST_F(ImportSourceAnalysisTest, ScriptFileIsTheInputOfItsScriptAsset)
  {
    const auto script = Write("move.lua", "return {}");
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.id = "script:Player/move.lua";
    job.job_type = "script";
    job.script.source_path = script.string();
    job.virtual_path = "/Content/Player/move.oscript";
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& result = report.jobs.front();
    ASSERT_EQ(result.outputs.size(), 1U);
    EXPECT_EQ(
      result.outputs.front().virtual_path, "/Content/Player/move.oscript");
    EXPECT_EQ(result.outputs.front().kind, ImportDependencyKind::kScript);
    EXPECT_TRUE(result.references.empty());
    ASSERT_EQ(result.files.size(), 1U);
    EXPECT_EQ(result.files.front().path,
      std::filesystem::absolute(script).lexically_normal());
  }

  NOLINT_TEST_F(
    ImportSourceAnalysisTest, TextureDescriptorUsesNativeRecipeWithoutDecoding)
  {
    static_cast<void>(
      Write("pixels.png", "metadata analysis must not decode this"));
    const auto path = Write("texture.json",
      cooker::test::TextureDescriptor { "/Cooked/Pixels.otex", "pixels.png" }
        .Set("/name", "Pixels")
        .Text());
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "texture-descriptor";
    job.mounts = oxygen::cooker::test::ContentMountTable(TempDir());
    job.texture.source_path = path.string();
    job.virtual_path = "/Cooked/Pixels.otex";
    job.texture.virtual_path = job.virtual_path;
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& result = report.jobs.front();
    ASSERT_EQ(result.outputs.size(), 1U);
    EXPECT_EQ(result.outputs.front().kind, ImportDependencyKind::kTexture);
    EXPECT_EQ(result.files.size(), 2U);
  }

  NOLINT_TEST_F(
    ImportSourceAnalysisTest, BothModelFormatsDeclareTheirNativeProducts)
  {
    const auto models = oxygen::cooker::test::AssetsDir() / "Models";
    auto manifest = ImportManifest {};
    for (const auto* const type : { "gltf", "fbx" }) {
      auto job = ImportManifestJob {};
      job.id = type;
      job.job_type = type;
      auto& settings = job.job_type == "gltf" ? job.gltf : job.fbx;
      settings.source_path
        = (models / (std::string("static_scalar_triangle.") + type)).string();
      job.virtual_path
        = std::string("/Cooked/Props/static_scalar_triangle.") + type;
      settings.virtual_path = job.virtual_path;
      manifest.jobs.push_back(job);
    }
    const auto report = manifest.AnalyzeSources();
    ASSERT_TRUE(report.complete) << report.ToJson();
    ASSERT_EQ(report.jobs.size(), 2U);
    for (size_t index = 0; index < report.jobs.size(); ++index) {
      const auto& job = report.jobs.at(index);
      const auto& identity = manifest.jobs.at(index).virtual_path;
      for (const auto kind : {
             ImportDependencyKind::kMaterial,
             ImportDependencyKind::kGeometry,
             ImportDependencyKind::kScene,
           }) {
        EXPECT_TRUE(std::ranges::any_of(job.outputs,
          [kind](const auto& output) -> auto { return output.kind == kind; }));
      }
      // Every product is flat under the model; the scene takes its stem.
      for (const auto& output : job.outputs) {
        EXPECT_TRUE(output.virtual_path.starts_with(identity + "/"))
          << output.virtual_path;
        EXPECT_EQ(
          output.virtual_path.find('/', identity.size() + 1), std::string::npos)
          << output.virtual_path;
      }
      EXPECT_TRUE(std::ranges::any_of(
        job.outputs, [&identity](const auto& output) -> auto {
          return output.virtual_path
            == identity + "/static_scalar_triangle.oscene";
        }));
      ASSERT_EQ(job.observations.size(), 1U);
      EXPECT_FALSE(job.observations.front().reads.empty());
      // Each reports its source facts; only glTF has scenes.
      ASSERT_HAS_VALUE(job.model) << report.ToJson();
      EXPECT_EQ(job.model->format, job.job_type);
      EXPECT_EQ(
        job.model->format_version, job.job_type == "gltf" ? "2.0" : "7.4");
      EXPECT_EQ(job.model->mesh_count, 1U);
      EXPECT_FALSE(job.model->up.empty());
      EXPECT_EQ(job.model->materials.size(),
        static_cast<size_t>(std::ranges::count(job.outputs,
          ImportDependencyKind::kMaterial, &ImportLogicalDependency::kind)));
      EXPECT_EQ(job.model->scenes.size(), job.job_type == "gltf" ? 1U : 0U);
    }
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Import/Schemas/oxygen.source-analysis.schema.json");
    EXPECT_THAT(oxygen::cooker::test::ValidateJson(
                  schema, nlohmann::json::parse(report.ToJson())),
      ::testing::IsEmpty());
  }

  //! A glTF model job lists the file's scenes: each by index, name and the
  //! selector the `gltf_scene` setting names it by (`#N` when the name is
  //! missing or shared), the default, and its root and reachable node counts.
  //! Every scene but the one imported is reported dropped. glTF is in metres,
  //! +Y up and right-handed; the format version is the file's
  //! `asset.version`.
  NOLINT_TEST_F(ImportSourceAnalysisTest, GltfJobListsTheFilesScenes)
  {
    const auto source = Write("rooms.gltf", R"({
      "asset": {"version": "2.0"},
      "nodes": [{"name": "Hall", "children": [2]}, {"name": "Garden"},
        {"name": "Lamp"}],
      "scenes": [{"name": "Day", "nodes": [0]}, {"nodes": [0, 1]},
        {"name": "Night", "nodes": [1]}, {"name": "Night", "nodes": [0]}],
      "scene": 1
    })");
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.id = "rooms";
    job.job_type = "gltf";
    job.gltf.source_path = source.string();
    job.virtual_path = "/Cooked/rooms.gltf";
    job.gltf.virtual_path = job.virtual_path;
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    ASSERT_EQ(report.jobs.size(), 1U);
    const auto& result = report.jobs.front();
    ASSERT_HAS_VALUE(result.model) << report.ToJson();
    const auto& model = *result.model;
    EXPECT_EQ(model.format, "gltf");
    EXPECT_EQ(model.format_version, "2.0");
    EXPECT_EQ(model.unit_meters, 1.0);
    EXPECT_EQ(model.right, "+X");
    EXPECT_EQ(model.up, "+Y");
    EXPECT_EQ(model.front, "+Z");
    EXPECT_EQ(model.left_handed, false);
    EXPECT_EQ(model.node_count, 3U);
    EXPECT_EQ(model.mesh_count, 0U);
    const auto expected = std::vector<ModelSourceScene> {
      {
        .index = 0U,
        .name = "Day",
        .selector = "Day",
        .is_default = false,
        .root_count = 1U,
        .node_count = 2U,
      },
      {
        .index = 1U,
        .name = "",
        .selector = "#1",
        .is_default = true,
        .root_count = 2U,
        .node_count = 3U,
      },
      {
        .index = 2U,
        .name = "Night",
        .selector = "#2",
        .is_default = false,
        .root_count = 1U,
        .node_count = 1U,
      },
      {
        .index = 3U,
        .name = "Night",
        .selector = "#3",
        .is_default = false,
        .root_count = 1U,
        .node_count = 2U,
      },
    };
    EXPECT_EQ(model.scenes, expected);
    auto dropped = std::vector<std::string> {};
    for (const auto& diagnostic : result.diagnostics) {
      if (diagnostic.code == import_codes::kSourceFeatureDropped
        && diagnostic.object_path.starts_with("/scenes/")) {
        EXPECT_EQ(diagnostic.severity, ImportSeverity::kWarning);
        dropped.push_back(diagnostic.object_path);
      }
    }
    EXPECT_EQ(dropped,
      (std::vector<std::string> { "/scenes/0", "/scenes/2", "/scenes/3" }));
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Import/Schemas/oxygen.source-analysis.schema.json");
    EXPECT_THAT(oxygen::cooker::test::ValidateJson(
                  schema, nlohmann::json::parse(report.ToJson())),
      ::testing::IsEmpty());
  }

  //! The metadata of a `.glb` comes from its JSON chunk alone: the analysis
  //! never reads the whole file, and its facts, the `glb` format included,
  //! equal a parse of the whole file.
  NOLINT_TEST_F(ImportSourceAnalysisTest, GlbAnalysisReadsOnlyItsJsonChunk)
  {
    const auto source
      = oxygen::cooker::test::AssetsDir() / "Models" / "Tabuleiro.glb";
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.id = "glb";
    job.job_type = "gltf";
    job.gltf.source_path = source.string();
    job.virtual_path = "/Cooked/Props/Tabuleiro.glb";
    job.gltf.virtual_path = job.virtual_path;
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    ASSERT_EQ(report.jobs.size(), 1U);
    const auto& result = report.jobs.front();
    ASSERT_HAS_VALUE(result.model) << report.ToJson();
    EXPECT_EQ(result.model->format, "glb");
    EXPECT_EQ(result.model->format_version, "2.0");
    const auto primary = std::ranges::find(result.observations,
      std::filesystem::absolute(source).lexically_normal(),
      &ImportSourceObservation::path);
    ASSERT_NE(primary, result.observations.end());
    auto read_bytes = uint64_t { 0 };
    for (const auto& read : primary->reads) {
      EXPECT_NE(read.max_bytes, 0U) << "the whole file was read";
      read_bytes += read.max_bytes;
    }
    EXPECT_LT(read_bytes, std::filesystem::file_size(source));

    // The whole file, through the analysis's own request and preparations.
    auto file = std::ifstream(source, std::ios::binary);
    const auto text = std::string(std::istreambuf_iterator<char>(file), {});
    ASSERT_EQ(text.size(), std::filesystem::file_size(source));
    auto errors = std::vector<ImportDiagnostic> {};
    auto request = job.gltf.Prepare(ImportFormat::kGltf, errors);
    ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
    const auto source_id = request->source_path.string();
    auto naming = NamingService {};
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = source_id;
    input.request = *request;
    input.naming_service = observer_ptr { &naming };
    auto adapter = adapters::GltfAdapter {};
    ASSERT_TRUE(adapter
        .Parse(std::as_bytes(std::span(text)), input,
          adapters::ModelParseMode::kMetadata)
        .success);
    auto expected = adapter.DescribeSource();
    for (const auto& material : adapter.PrepareMaterials(input).sources) {
      expected.materials.push_back(material.material.storage_name);
    }
    static_cast<void>(adapter.PrepareGeometry(input));
    for (const auto& texture : adapter.PrepareTextures(input).sources) {
      expected.textures.push_back(texture.name);
    }
    EXPECT_FALSE(expected.materials.empty());
    EXPECT_EQ(*result.model, expected);
  }

  //! A model's identity is the virtual path of its source file: one without
  //! it, or of another type, is refused before any product is named.
  NOLINT_TEST_F(ImportSourceAnalysisTest, ModelIdentityNamesItsSourceFile)
  {
    const auto source = oxygen::cooker::test::AssetsDir() / "Models"
      / "static_scalar_triangle.gltf";
    for (const auto* identity : { "", "/Cooked/static_scalar_triangle.fbx" }) {
      SCOPED_TRACE(identity);
      auto manifest = ImportManifest {};
      auto job = ImportManifestJob {};
      job.id = "model";
      job.job_type = "gltf";
      job.gltf.source_path = source.string();
      job.virtual_path = identity;
      job.gltf.virtual_path = identity;
      manifest.jobs.push_back(job);
      const auto report = manifest.AnalyzeSources();
      EXPECT_FALSE(report.complete);
      ASSERT_EQ(report.jobs.size(), 1U);
      EXPECT_TRUE(report.jobs.front().outputs.empty());
    }
  }

  NOLINT_TEST_F(
    ImportSourceAnalysisTest, MissingRequiredInputIsAnObservedFailure)
  {
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "texture";
    job.texture.source_path = TempPath("missing.png").string();
    job.virtual_path = "/Cooked/missing.otex";
    job.texture.virtual_path = job.virtual_path;
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    EXPECT_FALSE(report.complete);
    ASSERT_EQ(report.jobs.size(), 1U);
    const auto& result = report.jobs.front();
    ASSERT_EQ(result.observations.size(), 1U);
    EXPECT_FALSE(result.observations.front().exists);
    EXPECT_TRUE(std::ranges::any_of(
      result.diagnostics, [](const auto& diagnostic) -> auto {
        return diagnostic.code == import_codes::kAnalysisSourceMissing;
      }));
  }

  NOLINT_TEST_F(ImportSourceAnalysisTest, RequiredDirectoryIsNotAFile)
  {
    const auto directory = TempPath("image.png");
    ASSERT_TRUE(std::filesystem::create_directory(directory));
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "texture";
    job.texture.source_path = directory.string();
    job.virtual_path = "/Cooked/directory.otex";
    job.texture.virtual_path = job.virtual_path;
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    EXPECT_FALSE(report.complete);
    ASSERT_EQ(report.jobs.size(), 1U);
    EXPECT_TRUE(std::ranges::any_of(
      report.jobs.front().diagnostics, [](const auto& diagnostic) -> auto {
        return diagnostic.code == import_codes::kAnalysisSourceNotFile;
      }));
  }

  NOLINT_TEST_F(
    ImportSourceAnalysisTest, CancellationNeverProducesCompleteAnalysis)
  {
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "material-descriptor";
    job.material_descriptor.descriptor_path = TempPath("unread.json").string();
    manifest.jobs.push_back(job);
    auto stop = std::stop_source {};
    stop.request_stop();
    const auto report = manifest.AnalyzeSources(stop.get_token());
    EXPECT_FALSE(report.complete);
    for (const auto& job_report : report.jobs) {
      EXPECT_TRUE(job_report.observations.empty());
    }
  }

  NOLINT_TEST_F(
    ImportSourceAnalysisTest, CancellationDuringVerificationDrainsItsReader)
  {
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "material-descriptor";
    job.material_descriptor.descriptor_path = TempPath("source.json").string();
    manifest.jobs.push_back(job);
    auto loop = ImportEventLoop {};
    auto pool = co::ThreadPool(loop, 1U);
    auto reader = CancelVerificationReader {};
    const auto report = detail::RunSourceAnalysis(
      manifest, loop, reader, pool, reader.cancellation.get_token());
    EXPECT_FALSE(report.complete);
    EXPECT_EQ(reader.reads, 2U);
    EXPECT_TRUE(reader.drained);
  }
  NOLINT_TEST_F(
    ImportSourceAnalysisTest, ImplicitCubemapRecordsFacesAndRejectedSuffixes)
  {
    for (const auto* const suffix :
      { "posx", "negx", "posy", "negy", "posz", "negz" }) {
      static_cast<void>(
        Write(std::string("sky_") + suffix + ".png", "metadata only"));
    }
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "texture";
    job.texture.source_path = TempPath("sky.png").string();
    job.virtual_path = "/Cooked/sky.otex";
    job.texture.virtual_path = job.virtual_path;
    job.texture.cubemap = true;
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& result = report.jobs.front();
    EXPECT_EQ(result.files.size(), 6U);
    const auto rejected = std::ranges::find(result.observations,
      TempPath("sky_px.png"), &ImportSourceObservation::path);
    ASSERT_NE(rejected, result.observations.end());
    EXPECT_FALSE(rejected->exists);
    for (const auto& observation : result.observations) {
      EXPECT_TRUE(observation.reads.empty());
    }
  }

  //! The analysis plans what the settings ask: a remapped material is an
  //! output only when its target is missing, and a setting keyed to a part
  //! the file lacks warns.
  NOLINT_TEST_F(ImportSourceAnalysisTest, GltfJobPlansTheModelSettings)
  {
    const auto source = Write("remapped.gltf", R"({
      "asset": {"version": "2.0"},
      "materials": [{"name": "Paint"}, {"name": "Glass"}],
      "nodes": [{"name": "Car"}],
      "scenes": [{"nodes": [0]}], "scene": 0
    })");
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.id = "remapped";
    job.job_type = "gltf";
    job.gltf.source_path = source.string();
    job.virtual_path = "/Cooked/remapped.gltf";
    job.gltf.virtual_path = job.virtual_path;
    job.gltf.material_remap = {
      { "Paint", "/Content/Materials/Red.omat" },
      { "Chrome", "/Content/Materials/Chrome.omat" },
    };
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    ASSERT_EQ(report.jobs.size(), 1U);
    const auto& result = report.jobs.front();
    const auto required = [&](const std::string_view name) -> bool {
      const auto it = std::ranges::find_if(
        result.outputs, [&](const ImportLogicalDependency& output) -> bool {
          return output.kind == ImportDependencyKind::kMaterial
            && output.virtual_path.ends_with(
              std::string("/") + std::string(name) + ".omat");
        });
      EXPECT_NE(it, result.outputs.end()) << name;
      return it != result.outputs.end() && it->required;
    };
    EXPECT_FALSE(required("Paint"));
    EXPECT_TRUE(required("Glass"));
    EXPECT_TRUE(std::ranges::any_of(
      result.diagnostics, [](const auto& diagnostic) -> bool {
        return diagnostic.code == import_codes::kSettingTargetMissing
          && diagnostic.object_path == "material_remap/Chrome";
      }));
    EXPECT_FALSE(std::ranges::any_of(
      result.diagnostics, [](const auto& diagnostic) -> bool {
        return diagnostic.object_path == "material_remap/Paint";
      }));
  }

  //! The canonical virtual paths `result` references, in order.
  auto Referenced(const ImportSourceJobAnalysis& result)
    -> std::vector<std::string>
  {
    auto paths = std::vector<std::string> {};
    for (const auto& reference : result.references) {
      paths.push_back(reference.virtual_path);
    }
    return paths;
  }

  //! A buffer container reads its buffers' files and produces each buffer,
  //! named relative to the container's identity.
  NOLINT_TEST_F(ImportSourceAnalysisTest, BufferContainerProducesItsBuffers)
  {
    std::filesystem::create_directories(TempDir() / "Geometry");
    const auto data = Write("Geometry/crate.bin", "vertices");
    auto container = cooker::test::BufferContainer {
      "/Content/Geometry/crate.buffers",
    };
    container.AppendBuffer({ { "source", "crate.bin" } },
      "/Content/Geometry/crate.buffers/crate_vb.obuf");
    const auto path = Write("Geometry/crate.buffers.json", container.Text());
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "buffer-container";
    job.mounts = oxygen::cooker::test::ContentMountTable(TempDir());
    job.buffer_container.descriptor_path = path.string();
    job.source_mount_path = "/Content/Geometry/crate.buffers.json";
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& result = report.jobs.front();
    ASSERT_EQ(result.outputs.size(), 1U);
    EXPECT_EQ(result.outputs.front().virtual_path,
      "/Content/Geometry/crate.buffers/crate_vb.obuf");
    EXPECT_EQ(result.outputs.front().kind, ImportDependencyKind::kBuffer);
    EXPECT_TRUE(std::ranges::any_of(result.files,
      [&data](const auto& file) -> bool {
        return file.path == std::filesystem::absolute(data).lexically_normal();
      }))
      << report.ToJson();
  }

  //! A physics material descriptor is the only input of its asset and
  //! references nothing.
  NOLINT_TEST_F(ImportSourceAnalysisTest, PhysicsMaterialReferencesNothing)
  {
    const auto material = cooker::test::PhysicsMaterialDescriptor {
      "/Content/Physics/Wood.opmat",
    };
    const auto path = Write("Wood.physics-material.json", material.Text());
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "physics-material-descriptor";
    job.physics_material_descriptor.descriptor_path = path.string();
    job.virtual_path = material.Identity();
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& result = report.jobs.front();
    ASSERT_EQ(result.outputs.size(), 1U);
    EXPECT_EQ(result.outputs.front().virtual_path, material.Identity());
    EXPECT_TRUE(result.references.empty());
    ASSERT_EQ(result.files.size(), 1U);
  }

  //! A collision shape references its physics material, resolved against the
  //! shape's identity.
  NOLINT_TEST_F(ImportSourceAnalysisTest, CollisionShapeReferencesItsMaterial)
  {
    const auto shape = cooker::test::CollisionShapeDescriptor {
      "/Content/Physics/Crate.ocshape",
      "/Content/Physics/Wood.opmat",
    };
    const auto path = Write("Crate.shape.json", shape.Text());
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "collision-shape-descriptor";
    job.collision_shape_descriptor.descriptor_path = path.string();
    job.virtual_path = shape.Identity();
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& result = report.jobs.front();
    ASSERT_EQ(result.outputs.size(), 1U);
    EXPECT_EQ(result.outputs.front().virtual_path, shape.Identity());
    EXPECT_EQ(Referenced(result),
      std::vector<std::string> { "/Content/Physics/Wood.opmat" });
    EXPECT_EQ(result.references.front().json_pointer, "/material_ref");
  }

  //! A convex hull also references the geometry it cooks from, so the
  //! geometry cooks first.
  NOLINT_TEST_F(ImportSourceAnalysisTest, ConvexHullReferencesItsGeometry)
  {
    auto shape = cooker::test::CollisionShapeDescriptor {
      "/Content/Physics/Rock.ocshape",
      "/Content/Physics/Wood.opmat",
    };
    shape.Remove("/half_extents")
      .Set("/shape_type", "convex_hull")
      .Set("/geometry_ref", "../Meshes/Rock.ogeo");
    const auto path = Write("Rock.shape.json", shape.Text());
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "collision-shape-descriptor";
    job.collision_shape_descriptor.descriptor_path = path.string();
    job.virtual_path = shape.Identity();
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& result = report.jobs.front();
    EXPECT_EQ(Referenced(result),
      (std::vector<std::string> {
        "/Content/Physics/Wood.opmat",
        "/Content/Meshes/Rock.ogeo",
      }));
    ASSERT_EQ(result.references.size(), 2U);
    EXPECT_EQ(result.references.back().json_pointer, "/geometry_ref");
  }

  //! Each diagnostic as its code and the JSON location it names.
  auto Located(const std::vector<ImportDiagnostic>& diagnostics)
    -> std::vector<std::pair<std::string, std::string>>
  {
    auto located = std::vector<std::pair<std::string, std::string>> {};
    for (const auto& diagnostic : diagnostics) {
      located.emplace_back(diagnostic.code, diagnostic.json_pointer);
    }
    return located;
  }

  //! Whether analysis refused the only job of `report` with exactly the
  //! diagnostics the cook's preparation gives for the same document.
  auto RefusesAsTheCook(const ImportSourceAnalysis& report,
    const std::vector<ImportDiagnostic>& cook) -> bool
  {
    return !report.complete && !cook.empty()
      && Located(report.jobs.front().diagnostics) == Located(cook);
  }

  //! A source that is not JSON is refused, as its cook refuses it.
  NOLINT_TEST_F(ImportSourceAnalysisTest, SourceThatIsNotJsonIsRefused)
  {
    constexpr auto kText = std::string_view { "{ not json" };
    const auto path = Write("Park.physics-sidecar.json", kText);
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "physics-sidecar";
    job.physics_sidecar.source_path = path.string();
    job.physics_sidecar.target_scene_virtual_path
      = "/Content/Scenes/Park.oscene";
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    auto cook = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(PreparePhysicsSidecarDocument(kText, path, cook).has_value());
    EXPECT_TRUE(RefusesAsTheCook(report, cook)) << report.ToJson();
  }

  //! A source without what its job reads is refused: a collision shape names
  //! its physics material.
  NOLINT_TEST_F(
    ImportSourceAnalysisTest, CollisionShapeWithoutMaterialIsRefused)
  {
    auto shape = cooker::test::CollisionShapeDescriptor {
      "/Content/Physics/Crate.ocshape",
      "/Content/Physics/Wood.opmat",
    };
    shape.Remove("/material_ref");
    const auto path = Write("Crate.shape.json", shape.Text());
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "collision-shape-descriptor";
    job.collision_shape_descriptor.descriptor_path = path.string();
    job.virtual_path = shape.Identity();
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    auto cook = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(
      internal::PrepareCollisionShapeDescriptor(shape.Text(), path, cook)
        .has_value());
    EXPECT_TRUE(RefusesAsTheCook(report, cook)) << report.ToJson();
  }

  //! A physics sidecar references its target scene and what its bindings
  //! name, relative to the scene's folder, and produces the scene's physics
  //! companion.
  NOLINT_TEST_F(
    ImportSourceAnalysisTest, PhysicsSidecarReferencesItsSceneAndBindings)
  {
    auto sidecar = cooker::test::PhysicsSidecar {
      "/Content/Scenes/Park.physics-sidecar",
    };
    sidecar.AppendBinding("rigid_bodies",
      {
        { "node_index", 0 },
        { "shape_ref", sidecar.Spell("/Content/Physics/Crate.ocshape") },
        { "material_ref", sidecar.Spell("/Content/Physics/Wood.opmat") },
      });
    const auto path = Write("Park.physics-sidecar.json", sidecar.Text());
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "physics-sidecar";
    job.physics_sidecar.source_path = path.string();
    job.physics_sidecar.target_scene_virtual_path
      = "/Content/Scenes/Park.oscene";
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& result = report.jobs.front();
    ASSERT_EQ(result.outputs.size(), 1U);
    EXPECT_EQ(
      result.outputs.front().virtual_path, "/Content/Scenes/Park.opscene");
    EXPECT_EQ(result.outputs.front().kind, ImportDependencyKind::kPhysicsScene);
    EXPECT_EQ(Referenced(result),
      (std::vector<std::string> {
        "/Content/Scenes/Park.oscene",
        "/Content/Physics/Crate.ocshape",
        "/Content/Physics/Wood.opmat",
      }));
    EXPECT_EQ(result.references.at(1).json_pointer,
      "/bindings/rigid_bodies/0/shape_ref");
  }

  //! A reference the grammar refuses in a sidecar is reported with its code,
  //! as the cook would refuse it.
  NOLINT_TEST_F(
    ImportSourceAnalysisTest, PhysicsSidecarSelfAbsoluteReferenceIsRefused)
  {
    auto sidecar = cooker::test::PhysicsSidecar {
      "/Content/Scenes/Park.physics-sidecar",
    };
    sidecar.AppendBinding("colliders",
      {
        { "node_index", 0 },
        { "shape_ref", "/Content/Physics/Crate.ocshape" },
        { "material_ref", sidecar.Spell("/Content/Physics/Wood.opmat") },
      });
    const auto path = Write("Park.physics-sidecar.json", sidecar.Text());
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "physics-sidecar";
    job.physics_sidecar.source_path = path.string();
    job.physics_sidecar.target_scene_virtual_path
      = "/Content/Scenes/Park.oscene";
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    EXPECT_FALSE(report.complete);
    const auto& diagnostics = report.jobs.front().diagnostics;
    EXPECT_TRUE(std::ranges::any_of(diagnostics,
      [](const auto& diagnostic) -> bool {
        return diagnostic.code == import_codes::kReferenceInvalid
          && diagnostic.json_pointer == "/bindings/colliders/0/shape_ref"
          && diagnostic.message.contains(
            oxygen::content::vpath_codes::kReferenceSelfAbsolute);
      }))
      << report.ToJson();
  }

  //! A script sidecar references its target scene, the scripts it attaches
  //! and the assets their parameters name, from a file or inline bindings.
  NOLINT_TEST_F(
    ImportSourceAnalysisTest, ScriptSidecarReferencesItsSceneScriptsAndAssets)
  {
    auto sidecar = cooker::test::ScriptSidecar {
      "/Content/Scenes/Park.script-sidecar",
    };
    sidecar.Bind(0U, "/Content/Scripts/Spin.oscript",
      { { .key = "target", .asset = "/Content/Materials/Red.omat" } });
    const auto path = Write("Park.script-sidecar.json", sidecar.Text());
    const auto expected = std::vector<std::string> {
      "/Content/Scenes/Park.oscene",
      "/Content/Scripts/Spin.oscript",
      "/Content/Materials/Red.omat",
    };
    for (const auto inline_bindings : { false, true }) {
      auto manifest = ImportManifest {};
      auto job = ImportManifestJob {};
      job.job_type = "script-sidecar";
      if (inline_bindings) {
        job.scripting_sidecar.inline_bindings_json
          = sidecar.Json().at("bindings").dump();
      } else {
        job.scripting_sidecar.source_path = path.string();
      }
      job.scripting_sidecar.target_scene_virtual_path
        = "/Content/Scenes/Park.oscene";
      manifest.jobs.push_back(job);

      const auto report = manifest.AnalyzeSources();

      ASSERT_TRUE(report.complete) << report.ToJson();
      const auto& result = report.jobs.front();
      EXPECT_TRUE(result.outputs.empty());
      EXPECT_EQ(Referenced(result), expected) << inline_bindings;
      EXPECT_EQ(
        result.references.at(2).json_pointer, "/bindings/0/params/0/value");
    }
  }
  //! A physics material the schema refuses is refused by analysis with the
  //! cook's code, at the value.
  NOLINT_TEST_F(ImportSourceAnalysisTest, PhysicsMaterialIsRefusedAsTheCook)
  {
    constexpr auto kText = std::string_view { R"({ "static_friction": -1 })" };
    const auto path = Write("Wood.opmat.json", kText);
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "physics-material-descriptor";
    job.physics_material_descriptor.descriptor_path = path.string();
    job.virtual_path = "/Content/Physics/Wood.opmat";
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    auto cook = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(internal::PreparePhysicsMaterialDescriptor(kText, path, cook)
        .has_value());
    EXPECT_TRUE(RefusesAsTheCook(report, cook)) << report.ToJson();
    EXPECT_TRUE(std::ranges::any_of(cook,
      [](const auto& diagnostic) -> bool {
        return diagnostic.code
          == import_codes::kPhysicsMaterialSchemaValidationFailed
          && diagnostic.json_pointer == "/static_friction";
      }))
      << report.ToJson();
  }

  //! An input source whose actions are not an array is refused by analysis
  //! with the cook's code, at the field.
  NOLINT_TEST_F(ImportSourceAnalysisTest, InputSourceIsRefusedAsTheCook)
  {
    constexpr auto kText = std::string_view { R"({ "actions": "none" })" };
    constexpr auto kIdentity
      = std::string_view { "/Content/Input/Player.input" };
    const auto path = Write("Player.input.json", kText);
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "input";
    job.input.source_path = path.string();
    job.virtual_path = std::string { kIdentity };
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    auto cook = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(
      PrepareInputDocument(kText, path, kIdentity, cook).has_value());
    EXPECT_TRUE(RefusesAsTheCook(report, cook)) << report.ToJson();
    EXPECT_TRUE(std::ranges::any_of(cook,
      [](const auto& diagnostic) -> bool {
        return diagnostic.code == import_codes::kInputSchemaValidationFailed
          && diagnostic.json_pointer == "/actions";
      }))
      << report.ToJson();
  }

  //! A script sidecar row the cook refuses is refused by analysis with the
  //! cook's code, at the row.
  NOLINT_TEST_F(ImportSourceAnalysisTest, ScriptSidecarIsRefusedAsTheCook)
  {
    constexpr auto kText = std::string_view { R"({ "bindings": [ 7 ] })" };
    const auto path = Write("Park.scripting-sidecar.json", kText);
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "script-sidecar";
    job.scripting_sidecar.source_path = path.string();
    job.scripting_sidecar.target_scene_virtual_path
      = "/Content/Scenes/Park.oscene";
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    auto cook = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(
      PrepareScriptingSidecarDocument(kText, path, cook).has_value());
    EXPECT_TRUE(RefusesAsTheCook(report, cook)) << report.ToJson();
  }

  //! A script whose declarations the analyzer refuses is refused by analysis
  //! with the cook's codes.
  NOLINT_TEST_F(ImportSourceAnalysisTest, ScriptIsRefusedAsTheCook)
  {
    constexpr auto kText = std::string_view { "function (" };
    const auto path = Write("Broken.luau", kText);
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "script";
    job.script.source_path = path.string();
    job.virtual_path = "/Content/Scripts/Broken.oscript";
    manifest.jobs.push_back(job);

    const auto report = manifest.AnalyzeSources();

    auto cook = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(AnalyzeScriptDeclarations(
      std::as_bytes(std::span { kText }), path.string(), path, cook)
        .has_value());
    EXPECT_TRUE(RefusesAsTheCook(report, cook)) << report.ToJson();
  }

} // namespace
} // namespace oxygen::content::import::test
