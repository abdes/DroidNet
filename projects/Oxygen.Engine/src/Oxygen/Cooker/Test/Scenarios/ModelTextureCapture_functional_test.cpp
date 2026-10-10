//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/fbx/FbxAdapter.cpp,
//   Import/Internal/gltf/GltfAdapter.cpp,
//   Import/Internal/Pipelines/TexturePipeline.cpp, Import/CapturedInputSet.cpp

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportSourceAnalysis.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Test/Support/ModelImportTestBase.h>
#include <Oxygen/Cooker/Test/Support/TestPaths.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  class ModelTextureAnalysisParityTest : public ModelImportTestBase { };

  NOLINT_TEST_F(ModelTextureAnalysisParityTest,
    MissingTexturePreservesNativeFallbackPolicies)
  {
    const auto root = MakeTempDir("analysis_missing_model_texture");
    const auto source_path = root / "model.gltf";
    std::ifstream original(
      TestModelsDirFromFile() / "static_scalar_triangle.gltf");
    auto document = nlohmann::json::parse(original);
    document.emplace(
      "images", nlohmann::json::array({ { { "uri", "missing.png" } } }));
    document.emplace(
      "textures", nlohmann::json::array({ { { "source", 0 } } }));
    document.at("materials")
      .at(0)
      .at("pbrMetallicRoughness")
      .emplace("baseColorTexture", nlohmann::json { { "index", 0 } });
    std::ofstream saved(source_path, std::ios::binary);
    saved << document;
    saved.close();
    ASSERT_TRUE(saved.good());

    auto manifest = ImportManifest {};
    auto job = ImportManifestJob {};
    job.job_type = "gltf";
    job.gltf.source_path = source_path.string();
    job.gltf.material_slot_source_identity
      = "01990000-0000-7000-8000-000000000001";
    manifest.jobs.push_back(job);
    const auto analysis = manifest.AnalyzeSources();
    ASSERT_TRUE(analysis.complete) << analysis.ToJson();
    const auto& analyzed = analysis.jobs.front();
    const auto texture = std::ranges::find(analyzed.outputs,
      ImportDependencyKind::kTexture, &ImportLogicalDependency::kind);
    ASSERT_NE(texture, analyzed.outputs.end());
    EXPECT_FALSE(texture->required);
    for (const auto placeholder : { false, true }) {
      SCOPED_TRACE(placeholder);
      auto request = ImportRequest {};
      request.source_path = source_path;
      request.cooked_root
        = root / (placeholder ? "placeholder" : "error-index");
      request.options.texture_tuning.placeholder_on_failure = placeholder;
      const auto cooked = RunImport(std::move(request));
      EXPECT_TRUE(cooked.report.success);
      EXPECT_GT(cooked.report.geometry_written, 0U);
      EXPECT_GT(cooked.report.materials_written, 0U);
      EXPECT_EQ(std::ranges::any_of(cooked.report.outputs,
                  [](const auto& output) {
                    return std::string_view(output.path).ends_with(".otex");
                  }),
        placeholder);
    }
  }
  class ModelCapturedInputTest : public ModelImportTestBase {
  protected:
    static auto WriteCapture(const std::filesystem::path& logical,
      const std::filesystem::path& physical, const std::string_view bytes)
      -> CapturedInput
    {
      std::ofstream stream(physical, std::ios::binary);
      stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      stream.close();
      EXPECT_TRUE(stream.good());
      return CapturedInput {
        .logical_path = logical,
        .exists = true,
        .metadata = FileInfo { .size = bytes.size(),
          .last_modified = {},
          .is_directory = false,
          .is_symlink = false },
        .file = CapturedInputFile { .path = physical,
          .size = bytes.size(),
          .digest = base::ComputeSha256(std::as_bytes(std::span(bytes))) },
      };
    }

    auto Triangle() -> nlohmann::json
    {
      std::ifstream original(
        TestModelsDirFromFile() / "static_scalar_triangle.gltf");
      return nlohmann::json::parse(original);
    }
  };

  NOLINT_TEST_F(
    ModelCapturedInputTest, GltfUsesCapturedPrimaryAndExternalBuffer)
  {
    const auto root = MakeTempDir("captured_model_external_buffer");
    const auto logical = root / "authored" / "triangle.gltf";
    auto document = Triangle();
    document.at("buffers").at(0).at("uri") = "mesh%20data.bin";
    constexpr auto vertices = std::array<float, 9> { 0.0F, 0.0F, 0.0F, 1.0F,
      0.0F, 0.0F, 0.0F, 1.0F, 0.0F };
    const auto vertex_bytes = std::as_bytes(std::span(vertices));
    const auto buffer = root / "buffer.capture";
    {
      std::ofstream stream(buffer, std::ios::binary);
      for (const auto byte : vertex_bytes) {
        stream.put(static_cast<char>(byte));
      }
      stream.close();
      ASSERT_TRUE(stream.good());
    }
    const auto entries = std::array {
      WriteCapture(logical, root / "model.capture", document.dump()),
      CapturedInput {
        .logical_path = logical.parent_path() / "mesh data.bin",
        .exists = true,
        .metadata = FileInfo { .size = vertex_bytes.size(),
          .last_modified = {},
          .is_directory = false,
          .is_symlink = false },
        .file = CapturedInputFile { .path = buffer,
          .size = vertex_bytes.size(),
          .digest = base::ComputeSha256(vertex_bytes) },
      },
    };
    auto request = ImportRequest {};
    request.source_path = logical;
    request.cooked_root = root / "cooked";
    request.captured_inputs
      = std::make_shared<const CapturedInputSet>(std::span(entries));
    EXPECT_FALSE(std::filesystem::exists(logical));
    const auto cooked = RunImport(std::move(request));
    EXPECT_TRUE(cooked.report.success);
    EXPECT_GT(cooked.report.geometry_written, 0U);
    EXPECT_GT(cooked.report.scenes_written, 0U);
  }

  NOLINT_TEST_F(ModelCapturedInputTest, TextureFallbackCannotHideUndeclaredRead)
  {
    const auto root = MakeTempDir("captured_model_undeclared_texture");
    const auto logical = root / "model.gltf";
    auto document = Triangle();
    document.emplace(
      "images", nlohmann::json::array({ { { "uri", "unreported.png" } } }));
    document.emplace(
      "textures", nlohmann::json::array({ { { "source", 0 } } }));
    document.at("materials")
      .at(0)
      .at("pbrMetallicRoughness")
      .emplace("baseColorTexture", nlohmann::json { { "index", 0 } });
    const auto entry
      = WriteCapture(logical, root / "model.capture", document.dump());
    auto request = ImportRequest {};
    request.source_path = logical;
    request.cooked_root = root / "cooked";
    request.captured_inputs
      = std::make_shared<const CapturedInputSet>(std::span(&entry, 1));
    request.options.texture_tuning.placeholder_on_failure = true;
    const auto cooked = RunImport(std::move(request));
    EXPECT_FALSE(cooked.report.success);
  }
} // namespace
} // namespace oxygen::content::import::test
