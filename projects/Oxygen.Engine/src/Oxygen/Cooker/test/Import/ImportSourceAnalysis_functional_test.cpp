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
#include <sstream>
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
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportSourceAnalysis.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/Import/Internal/SourceAnalysisRunner.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/test/Pak/PakTestSupport.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  class CancelVerificationReader final : public IAsyncFileReader {
  public:
    std::stop_source cancellation;
    size_t reads = 0;
    bool drained = false;

    auto ReadFile(
      const std::filesystem::path& /*path*/, ReadOptions /*options*/)
      -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>> override
    {
      ++reads;
      if (reads == 2U) {
        const auto drain
          = ScopeGuard([this] noexcept -> void { drained = true; });
        cancellation.request_stop();
        co_await pending_;
      }
      constexpr auto text = std::string_view { R"({"name":"Observed"})" };
      auto bytes = std::vector<std::byte>(text.size());
      std::memcpy(bytes.data(), text.data(), text.size());
      co_return Ok(std::move(bytes));
    }

    auto Exists(const std::filesystem::path& /*path*/)
      -> co::Co<Result<bool, FileErrorInfo>> override
    {
      co_return Ok(true);
    }

    auto GetFileInfo(const std::filesystem::path& /*path*/)
      -> co::Co<Result<FileInfo, FileErrorInfo>> override
    {
      co_return Ok(FileInfo {});
    }

  private:
    co::Event pending_;
  };

  class ImportSourceAnalysisTest : public pak::test::TempDirFixture {
  protected:
    auto Write(const std::filesystem::path& name, const std::string_view text)
      -> std::filesystem::path
    {
      const auto path = Root() / name;
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
      temp_ = std::make_unique<oxygen::cooker::test::ScopedTempDir>();
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
      const auto material
        = WriteFile("material.json", R"({"name":"Stone","textures":{
      "base_color":{"virtual_path":"/.cooked/Textures/color.otex"},
      "thickness":{"virtual_path":"/.cooked/Textures/thickness.otex"}}})");
      const auto scene = WriteFile("scene.json",
        R"({"version":)" + std::to_string(internal::kSceneDescriptorVersion)
          + R"(,"name":"Scene.v2","nodes":[{"id":"00000000-0000-4000-8000-000000000001"},
      {"id":"00000000-0000-4000-8000-000000000002","parent":0,"instance":{
        "scene_ref":"/.cooked/Props/Crate.glb/Crate.oscene",
        "overrides":[{"path":["00000000-0000-4000-8000-000000000003"],"components":{"geometry":{"parts":[{
          "kind":"surface","id":"00000000-0000-4000-8000-000000000004",
          "layout_revision":"0000000000000000000000000000000000000000000000000000000000000000",
          "values":{"/material":"/.cooked/Materials/Red.omat"}}]}}}]}}],
      "renderables":[{"node":0,"geometry_ref":"/.cooked/Geometry/body.ogeo"}],
      "starting_input_contexts":[{"context_ref":"/.cooked/Input/Gameplay.oimap","priority":2}],
      "references":{"scripts":["/.cooked/Scripts/move.oscript"]},
      "environment":{"post_process_volume":{"auto_exposure_metering_mask":"/.cooked/Textures/mask.otex"}}})");
      auto manifest = ImportManifest {};
      auto material_job = ImportManifestJob {};
      material_job.id = "material";
      material_job.job_type = "material-descriptor";
      material_job.virtual_path = "/.cooked/Materials/Stone.omat";
      material_job.material_descriptor.descriptor_path = material.string();
      manifest.jobs.push_back(material_job);
      auto scene_job = ImportManifestJob {};
      scene_job.id = "scene";
      scene_job.job_type = "scene-descriptor";
      scene_job.virtual_path = "/.cooked/Scenes/Scene.oscene";
      scene_job.scene_descriptor.descriptor_path = scene.string();
      manifest.jobs.push_back(scene_job);
      report_ = manifest.AnalyzeSources();
    }

    static inline std::unique_ptr<oxygen::cooker::test::ScopedTempDir> temp_;
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
      "/.cooked/Scenes/Scene.oscene");
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
          return reference.virtual_path == "/.cooked/Input/Gameplay.oimap";
        });
    ASSERT_NE(context, references.end()) << report.ToJson();
    EXPECT_EQ(context->kind, ImportDependencyKind::kInputMappingContext);
    EXPECT_EQ(context->object_path, "starting_input_contexts[0].context_ref");
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
    const auto scene = find("/.cooked/Props/Crate.glb/Crate.oscene");
    ASSERT_NE(scene, references.end()) << report.ToJson();
    EXPECT_EQ(scene->kind, ImportDependencyKind::kScene);
    EXPECT_EQ(scene->object_path, "nodes[1].instance.scene_ref");
    EXPECT_TRUE(scene->required);
    const auto material = find("/.cooked/Materials/Red.omat");
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
    EXPECT_EQ(serialized.at("version"), 1);
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
    const auto geometry = Write("geometry.json", R"({
      "name":"Body","bounds":{"min":[-1,-1,-1],"max":[1,1,1]},
      "buffers":[{"uri":"vertices.bin","virtual_path":"/.cooked/Buffers/local.obuf"}],
      "lods":[{"name":"LOD0","mesh_type":"standard",
        "bounds":{"min":[-1,-1,-1],"max":[1,1,1]},
        "buffers":{"vb_ref":"/.cooked/Buffers/local.obuf","ib_ref":"/.cooked/Buffers/external.obuf"},
        "submeshes":[{"surface_id":"018f8f8f-1111-7111-8111-111111111111",
          "material_ref":"/.cooked/Materials/stone.omat","views":[{"view_ref":"__all__"}]}]}]})");
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "geometry-descriptor";
    job.geometry_descriptor.descriptor_path = geometry.string();
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& result = report.jobs.front();
    EXPECT_EQ(result.outputs.size(), 2U);
    EXPECT_EQ(result.references.size(), 2U);
    EXPECT_FALSE(
      std::ranges::any_of(result.references, [](const auto& reference) -> auto {
        return reference.virtual_path == "/.cooked/Buffers/local.obuf";
      }));
    const auto buffer = std::ranges::find(result.observations,
      Path("vertices.bin"), &ImportSourceObservation::path);
    ASSERT_NE(buffer, result.observations.end());
    EXPECT_TRUE(buffer->reads.empty());
  }

  //! An input source is the only input of its job; every declared action and
  //! mapping context is an output named under the source's identity, and an
  //! action of another source named by virtual path is a reference.
  NOLINT_TEST_F(
    ImportSourceAnalysisTest, InputFileDeclaresItsActionsAndContextsAsOutputs)
  {
    const auto input = Write("Player.input.json", R"({
      "actions":[{"name":"Jump","type":"bool"},{"name":"Dash","type":"bool"}],
      "contexts":[{"name":"Gameplay","mappings":[
        {"action":"Jump","slot":"Space","trigger":"pressed"},
        {"action":"/Content/Common.input/Fire.oiact","slot":"F"}]}]})");
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.id = "input:Gameplay/Player.input.json";
    job.job_type = "input";
    job.input.source_path = input.string();
    job.virtual_path = "/Content/Gameplay/Player.input";
    job.loose_cooked_layout.virtual_mount_root = "/Content";
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
    EXPECT_EQ(result.references.front().object_path,
      "$.contexts[0].mappings[1].action");
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
    job.loose_cooked_layout.virtual_mount_root = "/Content";
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
    const auto path
      = Write("texture.json", R"({"source":"pixels.png","name":"Pixels"})");
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "texture-descriptor";
    job.texture.source_path = path.string();
    job.virtual_path = "/.cooked/Pixels.otex";
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
        = std::string("/.cooked/Props/static_scalar_triangle.") + type;
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
  //! +Y up and right-handed.
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
    job.virtual_path = "/.cooked/rooms.gltf";
    job.gltf.virtual_path = job.virtual_path;
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    ASSERT_EQ(report.jobs.size(), 1U);
    const auto& result = report.jobs.front();
    ASSERT_HAS_VALUE(result.model) << report.ToJson();
    const auto& model = *result.model;
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
      if (diagnostic.code == "import.source_feature_dropped"
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
  //! never reads the whole file, and its facts equal a parse of the whole
  //! file.
  NOLINT_TEST_F(ImportSourceAnalysisTest, GlbAnalysisReadsOnlyItsJsonChunk)
  {
    const auto source
      = oxygen::cooker::test::AssetsDir() / "Models" / "Tabuleiro.glb";
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.id = "glb";
    job.job_type = "gltf";
    job.gltf.source_path = source.string();
    job.virtual_path = "/.cooked/Props/Tabuleiro.glb";
    job.gltf.virtual_path = job.virtual_path;
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    ASSERT_EQ(report.jobs.size(), 1U);
    const auto& result = report.jobs.front();
    ASSERT_HAS_VALUE(result.model) << report.ToJson();
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
    auto errors = std::ostringstream {};
    auto request = job.gltf.Prepare(ImportFormat::kGltf, errors);
    ASSERT_HAS_VALUE(request) << errors.str();
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
    for (const auto* identity : { "", "/.cooked/static_scalar_triangle.fbx" }) {
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
    job.texture.source_path = Path("missing.png").string();
    job.virtual_path = "/.cooked/missing.otex";
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
        return diagnostic.code == "analysis.source_missing";
      }));
  }

  NOLINT_TEST_F(ImportSourceAnalysisTest, RequiredDirectoryIsNotAFile)
  {
    const auto directory = Path("image.png");
    ASSERT_TRUE(std::filesystem::create_directory(directory));
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "texture";
    job.texture.source_path = directory.string();
    job.virtual_path = "/.cooked/directory.otex";
    job.texture.virtual_path = job.virtual_path;
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    EXPECT_FALSE(report.complete);
    ASSERT_EQ(report.jobs.size(), 1U);
    EXPECT_TRUE(std::ranges::any_of(
      report.jobs.front().diagnostics, [](const auto& diagnostic) -> auto {
        return diagnostic.code == "analysis.source_not_file";
      }));
  }

  NOLINT_TEST_F(
    ImportSourceAnalysisTest, CancellationNeverProducesCompleteAnalysis)
  {
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "material-descriptor";
    job.material_descriptor.descriptor_path = Path("unread.json").string();
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
    job.material_descriptor.descriptor_path = Path("source.json").string();
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
    job.texture.source_path = Path("sky.png").string();
    job.virtual_path = "/.cooked/sky.otex";
    job.texture.virtual_path = job.virtual_path;
    job.texture.cubemap = true;
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    ASSERT_TRUE(report.complete) << report.ToJson();
    const auto& result = report.jobs.front();
    EXPECT_EQ(result.files.size(), 6U);
    const auto rejected = std::ranges::find(
      result.observations, Path("sky_px.png"), &ImportSourceObservation::path);
    ASSERT_NE(rejected, result.observations.end());
    EXPECT_FALSE(rejected->exists);
    for (const auto& observation : result.observations) {
      EXPECT_TRUE(observation.reads.empty());
    }
  }
} // namespace
} // namespace oxygen::content::import::test
