//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "AsyncImporterFullTestBase.h"

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/StaticSourceValidation.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxMaterialTextures.h>
#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>
#include <Oxygen/Cooker/Import/Internal/gltf/cgltf.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::SceneContentPolicy;
using oxygen::content::import::internal::ValidateStaticSource;

class StaticImportTest
  : public oxygen::content::import::test::AsyncImporterFullTestBase { };

NOLINT_TEST(StaticSourceValidationTest, DisabledFbxCoreMapsAreNotSelected)
{
  auto texture = ufbx_texture {};
  auto fallback_texture = ufbx_texture {};
  auto material = ufbx_material {};
  const auto mapped
    = oxygen::content::import::adapters::FbxMaterialTextures::From(material);
  for (const auto& slot :
    std::array { mapped.base_color, mapped.normal, mapped.emissive }) {
    ASSERT_NE(slot.primary, nullptr);
    ASSERT_NE(slot.fallback, nullptr);
  }
  const auto primary_maps = std::array { &material.pbr.base_color,
    &material.pbr.normal_map, &material.pbr.emission_color };
  const auto fallback_maps = std::array { &material.fbx.diffuse_color,
    &material.fbx.normal_map, &material.fbx.emission_color };
  const auto slots
    = std::array { mapped.base_color, mapped.normal, mapped.emissive };
  for (size_t index = 0; index < slots.size(); ++index) {
    auto& primary = *primary_maps.at(index);
    auto& fallback = *fallback_maps.at(index);
    const auto& slot = slots.at(index);
    fallback.texture = &fallback_texture;
    fallback.texture_enabled = true;
    EXPECT_EQ(slot.Texture(), &fallback_texture);
    primary.texture = &texture;
    EXPECT_EQ(slot.Texture(), nullptr);
    primary.texture_enabled = true;
    EXPECT_EQ(slot.Texture(), &texture);
    primary.feature_disabled = true;
    EXPECT_EQ(slot.Texture(), nullptr);
    primary.texture = nullptr;
    EXPECT_EQ(slot.Texture(), nullptr);
  }
}

NOLINT_TEST(StaticSourceValidationTest, ActiveUnmappedFbxChannelsAreRejected)
{
  auto texture = ufbx_texture {};
  auto material = ufbx_material {};
  material.pbr.opacity.texture = &texture;
  material.pbr.opacity.texture_enabled = true;
  auto* material_pointer = &material;
  auto source = ufbx_scene {};
  source.materials = { &material_pointer, 1 };
  auto diagnostics = std::vector<ImportDiagnostic> {};
  EXPECT_FALSE(ValidateStaticSource(source, "opacity.fbx", diagnostics));
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& issue) {
    return issue.code == "import.static.unsupported"
      && issue.object_path
      == "/Materials/0/pbr/" + std::to_string(UFBX_MATERIAL_PBR_OPACITY);
  }));
}

NOLINT_TEST(StaticSourceValidationTest, FbxInstanceMaterialMustUseEmittedUvSet)
{
  auto texture = ufbx_texture {};
  texture.uv_set = { "Second", 6 };
  auto material = ufbx_material {};
  material.pbr.base_color.texture = &texture;
  material.pbr.base_color.texture_enabled = true;
  auto* material_pointer = &material;
  auto uv_set = ufbx_uv_set {};
  uv_set.name = { "First", 5 };
  auto mesh = ufbx_mesh {};
  mesh.uv_sets = { &uv_set, 1 };
  auto node = ufbx_node {};
  node.mesh = &mesh;
  node.materials = { &material_pointer, 1 };
  auto* node_pointer = &node;
  auto source = ufbx_scene {};
  source.materials = { &material_pointer, 1 };
  source.nodes = { &node_pointer, 1 };
  auto diagnostics = std::vector<ImportDiagnostic> {};
  EXPECT_FALSE(ValidateStaticSource(source, "uv.fbx", diagnostics));
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& issue) {
    return issue.object_path == "/Nodes/0" && issue.message.contains("Second");
  }));
}

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
    ASSERT_NE(
      material.base_color_texture, oxygen::data::pak::core::kNoResourceIndex);

    const auto table_path = result.report.cooked_root
      / std::filesystem::path(
        oxygen::content::import::LooseCookedLayout {}.TexturesTableRelPath());
    using TextureDesc = oxygen::data::pak::core::TextureResourceDesc;
    const auto offset
      = static_cast<size_t>(material.base_color_texture) * sizeof(TextureDesc);
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

NOLINT_TEST_F(
  StaticImportTest, NativeImportPreservesPerspectiveCameraAndDirectionalLight)
{
  for (const auto* extension : { "gltf", "fbx" }) {
    SCOPED_TRACE(extension);
    ImportRequest request {};
    request.source_path = TestModelsDirFromFile()
      / (std::string("static_scalar_camera_sun.") + extension);
    ASSERT_TRUE(std::filesystem::exists(request.source_path));
    request.cooked_root
      = MakeTempDir(std::string("static_scalar_camera_sun_") + extension);
    request.options.scene_content_policy = SceneContentPolicy::kStatic;
    request.options.coordinate.bake_transforms_into_meshes = false;
    const auto result = RunImport(std::move(request));
    ASSERT_TRUE(result.report.success);
    const auto scene = LoadSceneReadback(result.report);
    EXPECT_EQ(scene.directional_lights.size(), 1U);
    EXPECT_TRUE(std::ranges::any_of(
      scene.component_entries, [](const auto& entry) -> bool {
        return static_cast<oxygen::data::ComponentType>(entry.component_type)
          == oxygen::data::ComponentType::kPerspectiveCamera
          && entry.table.count == 1U;
      }));
  }
}

NOLINT_TEST_F(
  StaticImportTest, NativeImportRejectsUnsupportedComponentsBeforeEmission)
{
  for (const auto* extension : { "gltf", "fbx" }) {
    SCOPED_TRACE(extension);
    const auto source
      = TestModelsDirFromFile() / (std::string("light_overrides.") + extension);
    ASSERT_TRUE(std::filesystem::exists(source));
    ImportRequest request {};
    request.source_path = source;
    request.cooked_root
      = MakeTempDir(std::string("static_scalar_reject_") + extension);
    request.options.scene_content_policy = SceneContentPolicy::kStatic;
    const auto result = RunImport(std::move(request));
    EXPECT_FALSE(result.report.success);
    EXPECT_EQ(result.report.geometry_written, 0U);
    EXPECT_EQ(result.report.materials_written, 0U);
    EXPECT_EQ(result.report.scenes_written, 0U);
    EXPECT_TRUE(std::ranges::any_of(
      result.report.diagnostics, [](const auto& diagnostic) -> bool {
        return diagnostic.code == "import.static.unsupported"
          && diagnostic.message.find("light components") != std::string::npos;
      }));
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

NOLINT_TEST(StaticSourceValidationTest, ReportsAllUnsupportedSourceFeatures)
{
  cgltf_primitive primitive {};
  primitive.type = cgltf_primitive_type_lines;
  primitive.targets_count = 1U;
  cgltf_mesh mesh {};
  mesh.primitives = &primitive;
  mesh.primitives_count = 1U;
  cgltf_data source {};
  source.meshes = &mesh;
  source.meshes_count = 1U;
  source.animations_count = 1U;
  source.skins_count = 1U;
  cgltf_camera camera {};
  camera.type = cgltf_camera_type_orthographic;
  cgltf_light light {};
  light.type = cgltf_light_type_point;
  source.cameras = &camera;
  source.cameras_count = 1U;
  source.lights = &light;
  source.lights_count = 1U;
  std::vector<ImportDiagnostic> diagnostics;

  EXPECT_FALSE(ValidateStaticSource(source, "animated.gltf", diagnostics));
  EXPECT_EQ(diagnostics.size(), 6U);
  for (const auto& diagnostic : diagnostics) {
    EXPECT_EQ(diagnostic.source_path, "animated.gltf");
    EXPECT_FALSE(diagnostic.object_path.empty());
  }
}

NOLINT_TEST(StaticSourceValidationTest, RejectsRequiredUnknownExtensions)
{
  std::string extension = "EXT_physics";
  std::array extensions { extension.data() };
  cgltf_data source {};
  source.extensions_required = extensions.data();
  source.extensions_required_count = 1U;
  std::vector<ImportDiagnostic> diagnostics;
  EXPECT_FALSE(ValidateStaticSource(source, "physics.gltf", diagnostics));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_NE(diagnostics.front().message.find(extension), std::string::npos);
}

NOLINT_TEST(StaticSourceValidationTest, AcceptsMappedScalarExtensions)
{
  cgltf_material material {};
  material.has_clearcoat = 1;
  material.has_volume = 1;
  material.has_transmission = 1;
  cgltf_data source {};
  source.materials = &material;
  source.materials_count = 1U;
  std::vector<ImportDiagnostic> diagnostics;
  EXPECT_TRUE(ValidateStaticSource(source, "scalar.gltf", diagnostics));
  EXPECT_TRUE(diagnostics.empty());
}

NOLINT_TEST(StaticSourceValidationTest, RejectsUnemittedTextureChannels)
{
  auto texture = cgltf_texture {};
  auto material = cgltf_material {};
  material.has_clearcoat = true;
  material.clearcoat.clearcoat_texture.texture = &texture;
  auto source = cgltf_data {};
  source.materials = &material;
  source.materials_count = 1U;
  auto diagnostics = std::vector<ImportDiagnostic> {};
  EXPECT_FALSE(ValidateStaticSource(source, "coat.gltf", diagnostics));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().object_path,
    "/materials/0/extensions/KHR_materials_clearcoat");
}

NOLINT_TEST(StaticSourceValidationTest, RejectsConflictingTextureUvMappings)
{
  auto texture = cgltf_texture {};
  auto material = cgltf_material {};
  material.has_pbr_metallic_roughness = true;
  material.pbr_metallic_roughness.base_color_texture.texture = &texture;
  material.normal_texture.texture = &texture;
  material.normal_texture.texcoord = 1;
  auto source = cgltf_data {};
  source.materials = &material;
  source.materials_count = 1U;
  auto diagnostics = std::vector<ImportDiagnostic> {};
  EXPECT_FALSE(ValidateStaticSource(source, "uv.gltf", diagnostics));
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().object_path, "/materials/0");
}

NOLINT_TEST(StaticSourceValidationTest, RejectsUnmappedScalarFields)
{
  cgltf_material material {};
  material.has_specular = 1;
  material.specular.specular_color_factor[0] = 1.0F;
  material.has_sheen = 1;
  material.sheen.sheen_roughness_factor = 1.0F;
  cgltf_data source {};
  source.materials = &material;
  source.materials_count = 1U;
  std::vector<ImportDiagnostic> diagnostics;
  EXPECT_FALSE(ValidateStaticSource(source, "scalar.gltf", diagnostics));
  EXPECT_EQ(diagnostics.size(), 2U);
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
