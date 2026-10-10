//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/StaticSourceValidation.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/StaticSourceValidation.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxMaterialTextures.h>
#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>
#include <Oxygen/Cooker/Import/Internal/gltf/cgltf.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::internal::ValidateStaticSource;

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
  EXPECT_THAT(diagnostics.front().message, ::testing::HasSubstr(extension));
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

} // namespace
