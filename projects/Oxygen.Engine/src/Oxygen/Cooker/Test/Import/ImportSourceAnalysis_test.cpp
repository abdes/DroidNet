//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportSourceAnalysis.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/SourceAnalysisRunner.h>
#include <Oxygen/Cooker/Test/Pak/PakTestSupport.h>
#include <Oxygen/Data/PakFormat_world.h>
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

    auto ReadFile(const std::filesystem::path&, ReadOptions)
      -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>> override
    {
      ++reads;
      if (reads == 2U) {
        const auto drain = ScopeGuard([this] noexcept { drained = true; });
        cancellation.request_stop();
        co_await pending_;
      }
      constexpr auto text = std::string_view { R"({"name":"Observed"})" };
      auto bytes = std::vector<std::byte>(text.size());
      std::memcpy(bytes.data(), text.data(), text.size());
      co_return Ok(std::move(bytes));
    }

    auto Exists(const std::filesystem::path&)
      -> co::Co<Result<bool, FileErrorInfo>> override
    {
      co_return Ok(true);
    }

    auto GetFileInfo(const std::filesystem::path&)
      -> co::Co<Result<FileInfo, FileErrorInfo>> override
    {
      co_return Ok(FileInfo {});
    }

  private:
    co::Event pending_;
  };

  class ImportSourceAnalysisTest : public pak::test::TempDirFixture {
  protected:
    auto Write(const std::string_view name, const std::string_view text)
      -> std::filesystem::path
    {
      const auto path = Path(name);
      auto file = std::ofstream(path, std::ios::binary);
      file << text;
      file.close();
      EXPECT_TRUE(file.good());
      return path;
    }
  };

  NOLINT_TEST_F(ImportSourceAnalysisTest,
    MixedDescriptorsExposeNativeReferencesAndReadProofs)
  {
    const auto material = Write("material.json", R"({"name":"Stone","textures":{
      "base_color":{"virtual_path":"/.cooked/Textures/color.otex"},
      "thickness":{"virtual_path":"/.cooked/Textures/thickness.otex"}}})");
    const auto scene = Write("scene.json",
      R"({"version":)" + std::to_string(data::pak::world::kSceneAssetVersion)
        + R"(,"name":"Scene.v2","nodes":[{}],
      "renderables":[{"node":0,"geometry_ref":"/.cooked/Geometry/body.ogeo"}],
      "references":{"scripts":["/.cooked/Scripts/move.oscript"]},
      "environment":{"post_process_volume":{"auto_exposure_metering_mask":"/.cooked/Textures/mask.otex"}}})");
    auto manifest = ImportManifest {};
    auto material_job = ImportManifestJob {};
    material_job.id = "material";
    material_job.job_type = "material-descriptor";
    material_job.material_descriptor.descriptor_path = material.string();
    manifest.jobs.push_back(material_job);
    auto scene_job = ImportManifestJob {};
    scene_job.id = "scene";
    scene_job.job_type = "scene-descriptor";
    scene_job.scene_descriptor.descriptor_path = scene.string();
    manifest.jobs.push_back(scene_job);
    const auto report = manifest.AnalyzeSources();
    ASSERT_TRUE(report.complete) << report.ToJson();
    ASSERT_EQ(report.jobs.size(), 2U);
    EXPECT_EQ(report.jobs.front().references.size(), 2U);
    EXPECT_EQ(report.jobs.back().references.size(), 3U);
    EXPECT_EQ(report.jobs.back().outputs.front().virtual_path,
      "/.cooked/Scenes/Scene.oscene");
    for (const auto& job : report.jobs) {
      ASSERT_EQ(job.observations.size(), 1U);
      ASSERT_EQ(job.observations.front().reads.size(), 1U);
      EXPECT_EQ(job.observations.front().reads.front().max_bytes, 0U);
    }
    EXPECT_FALSE(std::filesystem::exists(Path(".cooked")));
    const auto serialized = nlohmann::json::parse(report.ToJson());
    EXPECT_EQ(serialized.at("version"), 1);
    EXPECT_EQ(serialized.at("jobs").size(), 2U);
    const auto schema_path = std::filesystem::path(__FILE__).parent_path()
      / "../../Import/Schemas/oxygen.source-analysis.schema.json";
    std::ifstream schema_file(schema_path);
    auto validator = nlohmann::json_schema::json_validator {};
    validator.set_root_schema(nlohmann::json::parse(schema_file));
    EXPECT_NO_THROW(validator.validate(serialized));
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
        "submeshes":[{"slot_id":"018f8f8f-1111-7111-8111-111111111111",
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
      std::ranges::any_of(result.references, [](const auto& reference) {
        return reference.virtual_path == "/.cooked/Buffers/local.obuf";
      }));
    const auto buffer = std::ranges::find(result.observations,
      Path("vertices.bin"), &ImportSourceObservation::path);
    ASSERT_NE(buffer, result.observations.end());
    EXPECT_TRUE(buffer->reads.empty());
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
    const auto models
      = std::filesystem::path(__FILE__).parent_path() / "Models";
    auto manifest = ImportManifest {};
    for (const auto type : { "gltf", "fbx" }) {
      auto job = ImportManifestJob {};
      job.id = type;
      job.job_type = type;
      auto& settings = job.job_type == "gltf" ? job.gltf : job.fbx;
      settings.source_path
        = (models / (std::string("static_scalar_triangle.") + type)).string();
      settings.material_slot_source_identity
        = "01990000-0000-7000-8000-000000000001";
      manifest.jobs.push_back(job);
    }
    const auto report = manifest.AnalyzeSources();
    ASSERT_TRUE(report.complete) << report.ToJson();
    ASSERT_EQ(report.jobs.size(), 2U);
    for (const auto& job : report.jobs) {
      for (const auto kind : { ImportDependencyKind::kMaterial,
             ImportDependencyKind::kGeometry, ImportDependencyKind::kScene }) {
        EXPECT_TRUE(std::ranges::any_of(job.outputs,
          [kind](const auto& output) { return output.kind == kind; }));
      }
      ASSERT_EQ(job.observations.size(), 1U);
      EXPECT_FALSE(job.observations.front().reads.empty());
    }
  }

  NOLINT_TEST_F(
    ImportSourceAnalysisTest, MissingRequiredInputIsAnObservedFailure)
  {
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "texture";
    job.texture.source_path = Path("missing.png").string();
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    EXPECT_FALSE(report.complete);
    ASSERT_EQ(report.jobs.size(), 1U);
    const auto& result = report.jobs.front();
    ASSERT_EQ(result.observations.size(), 1U);
    EXPECT_FALSE(result.observations.front().exists);
    EXPECT_TRUE(
      std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
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
    manifest.jobs.push_back(job);
    const auto report = manifest.AnalyzeSources();
    EXPECT_FALSE(report.complete);
    ASSERT_EQ(report.jobs.size(), 1U);
    EXPECT_TRUE(std::ranges::any_of(
      report.jobs.front().diagnostics, [](const auto& diagnostic) {
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
    for (const auto suffix :
      { "posx", "negx", "posy", "negy", "posz", "negz" }) {
      static_cast<void>(
        Write(std::string("sky_") + suffix + ".png", "metadata only"));
    }
    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "texture";
    job.texture.source_path = Path("sky.png").string();
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
