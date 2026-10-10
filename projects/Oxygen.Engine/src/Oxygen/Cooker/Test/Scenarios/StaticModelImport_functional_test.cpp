//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/AsyncImportService.cpp, Import/Internal/fbx/FbxAdapter.cpp,
//   Import/Internal/gltf/GltfAdapter.cpp

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/Test/Support/ModelImportTestBase.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::ImportRequest;
using oxygen::content::import::SceneContentPolicy;

class StaticImportTest
  : public oxygen::content::import::test::ModelImportTestBase { };

NOLINT_TEST_F(StaticImportTest, NativeImportPreservesCoreTextureBindings)
{
  for (const auto* extension : { "gltf", "fbx" }) {
    SCOPED_TRACE(extension);
    auto request = ImportRequest {};
    request.source_path = TestModelsDirFromFile()
      / (std::string("static_textured_triangle.") + extension);
    request.cooked_root
      = MakeTempDir(std::string("static_textured_") + extension);
    request.options.scene_content_policy = SceneContentPolicy::kStatic;
    request.options.texture_tuning.enabled = true;
    request.options.texture_tuning.color_output_format
      = oxygen::Format::kRGBA8UNormSRGB;
    request.options.texture_tuning.max_mip_levels = 1;
    const auto result = RunImport(std::move(request));
    for (const auto& diagnostic : result.report.diagnostics) {
      EXPECT_NE(
        diagnostic.severity, oxygen::content::import::ImportSeverity::kError)
        << diagnostic.code << ": " << diagnostic.message;
      EXPECT_NE(diagnostic.code, "material.texture_missing");
    }
    ASSERT_TRUE(result.report.success);
    const auto inspection = LoadInspection(result.report.cooked_root);
    const auto material_entry
      = FindAssetOfType(inspection, oxygen::data::AssetType::kMaterial);
    if (!material_entry.has_value()) {
      ADD_FAILURE() << "The textured material was not emitted";
      return;
    }

    auto material_stream = oxygen::serio::FileStream<>(
      result.report.cooked_root / material_entry->descriptor_relpath,
      std::ios::in);
    auto material_reader = oxygen::serio::Reader(material_stream);
    auto material = oxygen::data::pak::render::MaterialAssetDesc {};
    ASSERT_TRUE(material_reader.ReadBlobInto(
      std::as_writable_bytes(std::span(&material, 1))));
    ASSERT_NE(material.base_color_texture, oxygen::data::kNoResourceReference);
    const auto resolved = material_entry->references.ResolveResource(
      material.base_color_texture, oxygen::data::ResourceKind::kTexture);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->has_value());

    const auto table_path = result.report.cooked_root
      / std::filesystem::path(
        oxygen::content::import::LooseCookedLayout {}.TexturesTableRelPath());
    using TextureDesc = oxygen::data::pak::core::TextureResourceDesc;
    const auto offset
      = static_cast<size_t>((**resolved).get()) * sizeof(TextureDesc);
    ASSERT_LE(
      offset + sizeof(TextureDesc), std::filesystem::file_size(table_path));
    auto texture_stream = oxygen::serio::FileStream<>(table_path, std::ios::in);
    auto texture_reader = oxygen::serio::Reader(texture_stream);
    ASSERT_TRUE(texture_reader.Seek(offset));
    auto texture = TextureDesc {};
    ASSERT_TRUE(texture_reader.ReadBlobInto(
      std::as_writable_bytes(std::span(&texture, 1))));
    EXPECT_EQ(texture.width, 8U);
    EXPECT_EQ(texture.height, 4U);
    EXPECT_EQ(
      texture.format, static_cast<uint8_t>(oxygen::Format::kRGBA8UNormSRGB));
    EXPECT_GT(texture.size_bytes, 0U);
  }
}

NOLINT_TEST_F(StaticImportTest, NativeImportAcceptsStaticGltfAndFbx)
{
  for (const auto* extension : { "gltf", "fbx" }) {
    SCOPED_TRACE(extension);
    const auto source = TestModelsDirFromFile()
      / (std::string("static_scalar_triangle.") + extension);
    ASSERT_TRUE(std::filesystem::exists(source));
    ImportRequest request {};
    request.source_path = source;
    request.cooked_root
      = MakeTempDir(std::string("static_scalar_") + extension);
    request.options.scene_content_policy = SceneContentPolicy::kStatic;
    request.options.coordinate.bake_transforms_into_meshes = false;
    const auto result = RunImport(std::move(request));
    for (const auto& diagnostic : result.report.diagnostics) {
      EXPECT_NE(
        diagnostic.severity, oxygen::content::import::ImportSeverity::kError)
        << diagnostic.code << ": " << diagnostic.message;
    }
    ASSERT_TRUE(result.report.success);
    EXPECT_EQ(result.report.geometry_written, 1U);
    EXPECT_EQ(result.report.scenes_written, 1U);
    EXPECT_FALSE(LoadSceneReadback(result.report).renderables.empty());
  }
}

NOLINT_TEST_F(StaticImportTest, FbxWithoutAuthoredUnitsIsRejected)
{
  const auto root = MakeTempDir("static_scalar_missing_units");
  std::ifstream input(TestModelsDirFromFile() / "static_scalar_triangle.fbx");
  ASSERT_TRUE(input);
  const auto source = root / "ambiguous.fbx";
  {
    std::ofstream output(source);
    for (std::string line; std::getline(input, line);) {
      if (!line.contains("UnitScaleFactor")) {
        output << line << '\n';
      }
    }
  }
  ImportRequest request {};
  request.source_path = source;
  request.cooked_root = root / "cooked";
  request.options.scene_content_policy = SceneContentPolicy::kStatic;
  const auto result = RunImport(std::move(request));
  EXPECT_FALSE(result.report.success);
  EXPECT_EQ(result.report.geometry_written, 0U);
  EXPECT_TRUE(std::ranges::any_of(
    result.report.diagnostics, [](const auto& diagnostic) -> bool {
      return diagnostic.code == "import.static.coordinate_metadata";
    }));
}

NOLINT_TEST_F(StaticImportTest, ManifestSelectsPolicyAndRejectsUnknownValues)
{
  const auto root = MakeTempDir("static_scalar_manifest");
  const auto path = root / "import.json";
  for (const auto* policy :
    { "default", "static", "static-scalar", "invented" }) {
    SCOPED_TRACE(policy);
    {
      std::ofstream output(path);
      output
        << "{\"version\":1,\"output\":\"cooked\",\"jobs\":[{\"id\":\"model\","
           "\"type\":\"gltf\",\"source\":\"model.gltf\","
           "\"material_slot_source_identity\":\"01990000-0000-7000-8000-"
           "000000000001\","
           "\"content_policy\":\""
        << policy << "\"}]}";
    }
    std::ostringstream errors;
    const auto manifest
      = oxygen::content::import::ImportManifest::Load(path, root, errors);
    if (std::string_view(policy) == "invented"
      || std::string_view(policy) == "static-scalar") {
      EXPECT_FALSE(manifest.has_value());
      continue;
    }
    if (!manifest.has_value()) {
      ADD_FAILURE() << errors.str();
      continue;
    }
    const auto requests = manifest->BuildRequests(errors);
    ASSERT_EQ(requests.size(), 1U) << errors.str();
    EXPECT_EQ(requests.front().options.scene_content_policy,
      std::string_view(policy) == "static" ? SceneContentPolicy::kStatic
                                           : SceneContentPolicy::kDefault);
  }
}

} // namespace
