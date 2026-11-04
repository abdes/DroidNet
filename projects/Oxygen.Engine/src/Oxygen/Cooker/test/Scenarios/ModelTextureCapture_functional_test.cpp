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

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportConcurrency.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportSourceAnalysis.h>
#include <Oxygen/Cooker/test/Support/ModelImportTestBase.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Serio/ByteReader.h>
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
    job.virtual_path = oxygen::cooker::test::ModelVirtualPath(source_path);
    job.gltf.virtual_path = job.virtual_path;
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
      request.asset_virtual_path
        = oxygen::cooker::test::ModelVirtualPath(request.source_path);
      request.source_mount_path
        = oxygen::cooker::test::SourceMountPath(request.source_path);
      request.cooked_root
        = root / (placeholder ? "placeholder" : "error-index");
      request.options.texture_tuning.placeholder_on_failure = placeholder;
      const auto cooked = RunImport(std::move(request));
      EXPECT_TRUE(cooked.report.success);
      EXPECT_GT(cooked.report.geometry_written, 0U);
      EXPECT_GT(cooked.report.materials_written, 0U);
      EXPECT_EQ(std::ranges::any_of(cooked.report.outputs,
                  [](const auto& output) -> auto {
                    return std::string_view(output.path).ends_with(".otex");
                  }),
        placeholder);
    }
  }
  class ModelConcurrencyTest : public ModelImportTestBase {
  protected:
    //! The content hashes of the textures the model's material binds.
    struct BoundTextures {
      data::pak::core::ContentHashDigest base_color {};
      data::pak::core::ContentHashDigest normal {};
    };

    auto ImportAt(const std::filesystem::path& source,
      const std::string_view folder, const ImportPipelineConcurrency pipeline,
      BoundTextures& bound) -> void
    {
      auto config = AsyncImportService::Config {
        .thread_pool_size = 4U,
        .max_in_flight_jobs = 1U,
      };
      config.concurrency = ImportConcurrency {
        .texture = pipeline,
        .buffer = pipeline,
        .material = pipeline,
        .mesh_build = pipeline,
        .geometry = pipeline,
        .scene = pipeline,
      };
      auto request = ImportRequest {};
      request.source_path = source;
      request.asset_virtual_path
        = oxygen::cooker::test::ModelVirtualPath(request.source_path);
      request.source_mount_path
        = oxygen::cooker::test::SourceMountPath(request.source_path);
      request.cooked_root = source.parent_path() / folder;
      const auto cooked = RunImport(std::move(request), config);
      ASSERT_TRUE(cooked.report.success);
      const auto content = LoadInspection(cooked.report.cooked_root);
      const auto material = FindAssetOfType(content, AssetType::kMaterial);
      ASSERT_HAS_VALUE(material);
      const auto descriptor = content.Descriptor(material->key);
      const auto desc
        = serio::CopyAt<data::pak::render::MaterialAssetDesc>(descriptor);
      ASSERT_HAS_VALUE(desc);
      const auto& textures = content.ResourceTable(cooked::TableKind::kTexture);
      // A descriptor field names a slot of its resource bindings, and the slot
      // names the table row.
      const auto bindings = material->references.Resources();
      const auto hash_of
        = [&textures, bindings](const data::ResourceReferenceIndex slot)
        -> data::pak::core::ContentHashDigest {
        return textures.Row<TextureResourceDesc>(bindings[slot.get()].index)
          .content_hash;
      };
      bound.base_color = hash_of(desc->base_color_texture);
      bound.normal = hash_of(desc->normal_texture);
    }
  };

  //! Row order follows completion order on purpose, so two cooks may number
  //! their rows differently. What must not differ is the content: at any
  //! concurrency, down to one item per queue, each material binds the right
  //! texture, and every queue at capacity one still drains.
  NOLINT_TEST_F(ModelConcurrencyTest, AnyConcurrencyBindsTheSameContent)
  {
    const auto root = MakeTempDir("model_concurrency");
    for (const auto* const image :
      { "static_textured_checker.png", "static_tangent_normal.png" }) {
      std::filesystem::copy_file(TestModelsDirFromFile() / image, root / image);
    }
    auto document = nlohmann::json::parse(
      std::ifstream(TestModelsDirFromFile() / "static_textured_triangle.gltf"));
    document.at("images").push_back({ { "uri", "static_tangent_normal.png" } });
    document.at("textures").push_back({ { "source", 1 } });
    document.at("materials").at(0)["normalTexture"] = { { "index", 1 } };
    const auto source = root / "two_textures.gltf";
    std::ofstream(source) << document.dump();

    auto serial = BoundTextures {};
    ASSERT_NO_FATAL_FAILURE(ImportAt(source, "serial",
      ImportPipelineConcurrency { .workers = 1U, .queue_capacity = 1U },
      serial));
    auto parallel = BoundTextures {};
    ASSERT_NO_FATAL_FAILURE(ImportAt(source, "parallel",
      ImportPipelineConcurrency { .workers = 4U, .queue_capacity = 2U },
      parallel));

    EXPECT_NE(serial.base_color, serial.normal);
    EXPECT_EQ(serial.base_color, parallel.base_color);
    EXPECT_EQ(serial.normal, parallel.normal);
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
          .is_symlink = false, },
        .file = CapturedInputFile { .path = physical,
          .size = bytes.size(),
          .digest = base::ComputeSha256(std::as_bytes(std::span(bytes))), },
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
    constexpr auto vertices = std::array<float, 9> {
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
    };
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
          .is_symlink = false, },
        .file = CapturedInputFile { .path = buffer,
          .size = vertex_bytes.size(),
          .digest = base::ComputeSha256(vertex_bytes), },
      },
    };
    auto request = ImportRequest {};
    request.source_path = logical;
    request.asset_virtual_path
      = oxygen::cooker::test::ModelVirtualPath(request.source_path);
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = root / "cooked";
    request.captured_inputs
      = std::make_shared<const CapturedInputSet>(std::span(entries));
    EXPECT_FALSE(std::filesystem::exists(logical));
    const auto cooked = RunImport(std::move(request));
    EXPECT_TRUE(cooked.report.success);
    EXPECT_GT(cooked.report.geometry_written, 0U);
    EXPECT_GT(cooked.report.scenes_written, 0U);
  }

  //! A texture the capture does not list is missing: the model imports, and
  //! its material is bound to the error texture.
  NOLINT_TEST_F(ModelCapturedInputTest, UncapturedTextureImportsAsMissing)
  {
    const auto root = MakeTempDir("captured_model_uncaptured_texture");
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
    request.asset_virtual_path
      = oxygen::cooker::test::ModelVirtualPath(request.source_path);
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = root / "cooked";
    request.captured_inputs
      = std::make_shared<const CapturedInputSet>(std::span(&entry, 1));
    const auto cooked = RunImport(std::move(request));
    EXPECT_TRUE(cooked.report.success);
    EXPECT_GT(cooked.report.geometry_written, 0U);
    EXPECT_GT(cooked.report.materials_written, 0U);
    EXPECT_TRUE(std::ranges::any_of(
      cooked.report.diagnostics, [](const auto& diagnostic) -> bool {
        return diagnostic.code == import_codes::kTextureLoadFailed
          && diagnostic.severity == ImportSeverity::kWarning;
      }));
  }
} // namespace
} // namespace oxygen::content::import::test
