//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/DroppedSourceFeatures.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/DroppedSourceFeatures.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxMaterialTextures.h>
#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>
#include <Oxygen/Cooker/Import/Internal/gltf/cgltf.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
using oxygen::content::import::ImportSeverity;
using oxygen::content::import::internal::ReportDroppedSourceFeatures;

constexpr auto kDropped = "import.source_feature_dropped";

auto AllDropWarnings(const std::vector<ImportDiagnostic>& diagnostics) -> bool
{
  return std::ranges::all_of(diagnostics, [](const auto& issue) -> bool {
    return issue.code == kDropped && issue.severity == ImportSeverity::kWarning;
  });
}

NOLINT_TEST(DroppedSourceFeaturesTest, DisabledFbxCoreMapsAreNotSelected)
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
  const auto primary_maps = std::array {
    &material.pbr.base_color,
    &material.pbr.normal_map,
    &material.pbr.emission_color,
  };
  const auto fallback_maps = std::array {
    &material.fbx.diffuse_color,
    &material.fbx.normal_map,
    &material.fbx.emission_color,
  };
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

NOLINT_TEST(DroppedSourceFeaturesTest, ActiveUnmappedFbxChannelsAreReported)
{
  auto texture = ufbx_texture {};
  auto material = ufbx_material {};
  material.pbr.opacity.texture = &texture;
  material.pbr.opacity.texture_enabled = true;
  auto* material_pointer = &material;
  auto source = ufbx_scene {};
  source.materials = { .data = &material_pointer, .count = 1 };
  auto diagnostics = std::vector<ImportDiagnostic> {};
  ReportDroppedSourceFeatures(source, "opacity.fbx", diagnostics);
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& issue) -> auto {
    return issue.code == kDropped && issue.severity == ImportSeverity::kWarning
      && issue.object_path
      == "/Materials/0/pbr/" + std::to_string(UFBX_MATERIAL_PBR_OPACITY);
  }));
}

//! A file texture that repeats, so its own import reports nothing.
auto FileTexture(const char* name) -> ufbx_texture
{
  auto texture = ufbx_texture {};
  texture.name = {
    .data = name,
    .length = std::char_traits<char>::length(name),
  };
  texture.type = UFBX_TEXTURE_FILE;
  texture.wrap_u = UFBX_WRAP_REPEAT;
  texture.wrap_v = UFBX_WRAP_REPEAT;
  return texture;
}

auto Bind(ufbx_material_map& map, ufbx_texture& texture) -> void
{
  map.texture = &texture;
  map.texture_enabled = true;
}

auto Report(ufbx_material& material) -> std::vector<ImportDiagnostic>
{
  auto* material_pointer = &material;
  auto source = ufbx_scene {};
  source.materials = { .data = &material_pointer, .count = 1 };
  auto diagnostics = std::vector<ImportDiagnostic> {};
  ReportDroppedSourceFeatures(source, "material.fbx", diagnostics);
  return diagnostics;
}

//! The dropped-feature warnings that name `texture`.
auto Naming(const std::vector<ImportDiagnostic>& diagnostics,
  const std::string& texture) -> std::ptrdiff_t
{
  return std::ranges::count_if(diagnostics, [&](const auto& issue) -> bool {
    return issue.code == kDropped && issue.severity == ImportSeverity::kWarning
      && issue.message.contains("'" + texture + "'");
  });
}

//! A legacy FBX channel that binds a texture a PBR slot imports drops nothing:
//! the texture is imported, so nothing is reported for it.
NOLINT_TEST(
  DroppedSourceFeaturesTest, ChannelsOfAnImportedTextureAreNotReported)
{
  auto roughness = FileTexture("Rough");
  auto specular = FileTexture("Spec");
  auto material = ufbx_material {};
  Bind(material.pbr.roughness, roughness);
  Bind(material.fbx.specular_exponent, roughness);
  // Blender's specular level, which the specular slot takes as it is.
  material.shader_type = UFBX_SHADER_BLENDER_PHONG;
  Bind(material.fbx.specular_factor, specular);

  const auto diagnostics = Report(material);

  EXPECT_EQ(Naming(diagnostics, "Rough"), 0);
  EXPECT_EQ(Naming(diagnostics, "Spec"), 0);
}

//! A texture no slot imports is reported once, at its first channel, however
//! many channels bind it.
NOLINT_TEST(DroppedSourceFeaturesTest, AnUnimportedTextureIsReportedOnce)
{
  auto opacity = FileTexture("Cutout");
  auto material = ufbx_material {};
  Bind(material.pbr.opacity, opacity);
  Bind(material.fbx.transparency_factor, opacity);

  const auto diagnostics = Report(material);

  EXPECT_EQ(Naming(diagnostics, "Cutout"), 1);
}

//! A PBR shader's specular and clear coat import their weight maps, never
//! their colour maps; transmission falls back to its colour map when a file
//! has only that.
NOLINT_TEST(DroppedSourceFeaturesTest, FeatureSlotsReadTheirOwnMapsFirst)
{
  auto weight = FileTexture("Weight");
  auto colour = FileTexture("Colour");
  auto material = ufbx_material {};
  material.shader_type = UFBX_SHADER_OSL_STANDARD_SURFACE;
  material.features.specular.enabled = true;
  material.features.transmission.enabled = true;
  material.features.coat.enabled = true;
  Bind(material.pbr.specular_color, colour);
  Bind(material.pbr.transmission_color, colour);
  Bind(material.pbr.coat_color, colour);
  const auto colour_only
    = oxygen::content::import::adapters::FbxMaterialTextures::From(material);
  EXPECT_EQ(colour_only.specular.Texture(), nullptr);
  EXPECT_EQ(colour_only.transmission.Texture(), &colour);
  EXPECT_EQ(colour_only.clearcoat.Texture(), nullptr);

  Bind(material.pbr.specular_factor, weight);
  Bind(material.pbr.transmission_factor, weight);
  Bind(material.pbr.coat_factor, weight);
  const auto both
    = oxygen::content::import::adapters::FbxMaterialTextures::From(material);
  EXPECT_EQ(both.specular.Texture(), &weight);
  EXPECT_EQ(both.transmission.Texture(), &weight);
  EXPECT_EQ(both.clearcoat.Texture(), &weight);
}

//! An FBX Phong material's specular shapes a highlight; it is no reflectance,
//! so its texture is not imported and is reported.
NOLINT_TEST(DroppedSourceFeaturesTest, PhongSpecularIsReported)
{
  auto highlight = FileTexture("Highlight");
  auto material = ufbx_material {};
  material.shader_type = UFBX_SHADER_FBX_PHONG;
  material.features.specular.enabled = true;
  Bind(material.pbr.specular_factor, highlight);
  Bind(material.fbx.specular_factor, highlight);

  EXPECT_EQ(
    oxygen::content::import::adapters::FbxMaterialTextures::From(material)
      .specular.Texture(),
    nullptr);
  EXPECT_EQ(Naming(Report(material), "Highlight"), 1);
}

//! A material feature the material does not use imports none of its
//! textures, so they are reported.
NOLINT_TEST(DroppedSourceFeaturesTest, ADisabledFeatureImportsNoTexture)
{
  auto sheen = FileTexture("Sheen");
  auto material = ufbx_material {};
  Bind(material.pbr.sheen_color, sheen);
  material.features.sheen.enabled = false;

  EXPECT_EQ(
    oxygen::content::import::adapters::FbxMaterialTextures::From(material)
      .sheen_color.Texture(),
    nullptr);
  EXPECT_EQ(Naming(Report(material), "Sheen"), 1);
}

NOLINT_TEST(DroppedSourceFeaturesTest, FbxTextureWrapModesAreReported)
{
  auto texture = ufbx_texture {};
  texture.type = UFBX_TEXTURE_FILE;
  texture.wrap_u = UFBX_WRAP_CLAMP;
  texture.wrap_v = UFBX_WRAP_REPEAT;
  auto material = ufbx_material {};
  material.name = { .data = "Bun", .length = 3 };
  material.pbr.base_color.texture = &texture;
  material.pbr.base_color.texture_enabled = true;
  auto* material_pointer = &material;
  auto source = ufbx_scene {};
  source.materials = { .data = &material_pointer, .count = 1 };
  auto diagnostics = std::vector<ImportDiagnostic> {};
  ReportDroppedSourceFeatures(source, "wrap.fbx", diagnostics);
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& issue) -> auto {
    return issue.code == kDropped && issue.object_path == "/Materials/0"
      && issue.object_name == "Bun";
  }));
}

NOLINT_TEST(DroppedSourceFeaturesTest, FbxTextureOnAnotherUvSetIsReported)
{
  auto texture = ufbx_texture {};
  texture.uv_set = { .data = "Second", .length = 6 };
  auto material = ufbx_material {};
  material.pbr.base_color.texture = &texture;
  material.pbr.base_color.texture_enabled = true;
  auto* material_pointer = &material;
  auto uv_set = ufbx_uv_set {};
  uv_set.name = { .data = "First", .length = 5 };
  auto mesh = ufbx_mesh {};
  mesh.uv_sets = { .data = &uv_set, .count = 1 };
  auto node = ufbx_node {};
  node.mesh = &mesh;
  node.materials = { .data = &material_pointer, .count = 1 };
  auto* node_pointer = &node;
  auto source = ufbx_scene {};
  source.materials = { .data = &material_pointer, .count = 1 };
  source.nodes = { .data = &node_pointer, .count = 1 };
  auto diagnostics = std::vector<ImportDiagnostic> {};
  ReportDroppedSourceFeatures(source, "uv.fbx", diagnostics);
  EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& issue) -> auto {
    return issue.code == kDropped && issue.object_path == "/Nodes/0"
      && issue.message.contains("Second");
  }));
}

NOLINT_TEST(DroppedSourceFeaturesTest, ReportsEachDroppedGltfFeature)
{
  cgltf_primitive primitive {};
  primitive.targets_count = 1U;
  cgltf_mesh mesh {};
  mesh.primitives = &primitive;
  mesh.primitives_count = 1U;
  cgltf_data source {};
  source.meshes = &mesh;
  source.meshes_count = 1U;
  source.animations_count = 1U;
  source.skins_count = 1U;
  std::vector<ImportDiagnostic> diagnostics;

  ReportDroppedSourceFeatures(source, "animated.gltf", diagnostics);

  EXPECT_EQ(diagnostics.size(), 3U);
  EXPECT_TRUE(AllDropWarnings(diagnostics));
  for (const auto& diagnostic : diagnostics) {
    EXPECT_EQ(diagnostic.source_path, "animated.gltf");
    EXPECT_FALSE(diagnostic.object_path.empty());
  }
}

NOLINT_TEST(DroppedSourceFeaturesTest, ImportedComponentsAreNotReported)
{
  cgltf_camera camera {};
  camera.type = cgltf_camera_type_orthographic;
  auto lights = std::array<cgltf_light, 2> {};
  lights.at(0).type = cgltf_light_type_point;
  lights.at(1).type = cgltf_light_type_spot;
  cgltf_data source {};
  source.cameras = &camera;
  source.cameras_count = 1U;
  source.lights = lights.data();
  source.lights_count = lights.size();
  std::vector<ImportDiagnostic> diagnostics;

  ReportDroppedSourceFeatures(source, "components.gltf", diagnostics);

  EXPECT_TRUE(diagnostics.empty());
}

NOLINT_TEST(DroppedSourceFeaturesTest, RequiredUnreadExtensionIsReported)
{
  std::string extension = "EXT_physics";
  std::array extensions { extension.data() };
  cgltf_data source {};
  source.extensions_used = extensions.data();
  source.extensions_used_count = 1U;
  source.extensions_required = extensions.data();
  source.extensions_required_count = 1U;
  std::vector<ImportDiagnostic> diagnostics;

  ReportDroppedSourceFeatures(source, "physics.gltf", diagnostics);

  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics.front().code, "import.source_extension_required");
  EXPECT_EQ(diagnostics.front().severity, ImportSeverity::kWarning);
  EXPECT_THAT(diagnostics.front().message, ::testing::HasSubstr(extension));
}

NOLINT_TEST(DroppedSourceFeaturesTest, UsedUnreadExtensionIsReported)
{
  std::string extension = "EXT_physics";
  std::array extensions { extension.data() };
  cgltf_data source {};
  source.extensions_used = extensions.data();
  source.extensions_used_count = 1U;
  std::vector<ImportDiagnostic> diagnostics;

  ReportDroppedSourceFeatures(source, "physics.gltf", diagnostics);

  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_TRUE(AllDropWarnings(diagnostics));
  EXPECT_EQ(diagnostics.front().object_path, "/extensions/EXT_physics");
}

NOLINT_TEST(DroppedSourceFeaturesTest, MaterialTextureChannelsAreNotReported)
{
  auto texture = cgltf_texture {};
  auto material = cgltf_material {};
  material.has_clearcoat = 1;
  material.clearcoat.clearcoat_texture.texture = &texture;
  material.clearcoat.clearcoat_normal_texture.texture = &texture;
  material.has_transmission = 1;
  material.transmission.transmission_texture.texture = &texture;
  material.has_volume = 1;
  material.volume.thickness_texture.texture = &texture;
  auto source = cgltf_data {};
  source.materials = &material;
  source.materials_count = 1U;
  auto diagnostics = std::vector<ImportDiagnostic> {};

  ReportDroppedSourceFeatures(source, "coat.gltf", diagnostics);

  EXPECT_TRUE(diagnostics.empty());
}

NOLINT_TEST(DroppedSourceFeaturesTest, ConflictingTextureUvMappingsAreReported)
{
  auto texture = cgltf_texture {};
  auto material = cgltf_material {};
  material.has_pbr_metallic_roughness = 1;
  material.pbr_metallic_roughness.base_color_texture.texture = &texture;
  material.normal_texture.texture = &texture;
  material.normal_texture.texcoord = 1;
  auto source = cgltf_data {};
  source.materials = &material;
  source.materials_count = 1U;
  auto diagnostics = std::vector<ImportDiagnostic> {};

  ReportDroppedSourceFeatures(source, "uv.gltf", diagnostics);

  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_TRUE(AllDropWarnings(diagnostics));
  EXPECT_EQ(diagnostics.front().object_path, "/materials/0");
}

NOLINT_TEST(DroppedSourceFeaturesTest, CustomSamplingIsReported)
{
  auto sampler = cgltf_sampler {};
  sampler.wrap_s = cgltf_wrap_mode_clamp_to_edge;
  sampler.wrap_t = cgltf_wrap_mode_repeat;
  auto texture = cgltf_texture {};
  texture.sampler = &sampler;
  auto name = std::string("Bun");
  auto material = cgltf_material {};
  material.name = name.data();
  material.pbr_metallic_roughness.base_color_texture.texture = &texture;
  auto source = cgltf_data {};
  source.materials = &material;
  source.materials_count = 1U;
  auto diagnostics = std::vector<ImportDiagnostic> {};

  ReportDroppedSourceFeatures(source, "clamp.gltf", diagnostics);

  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_TRUE(AllDropWarnings(diagnostics));
  // A material's report names the material as the file does, so a person can
  // tell which of a file's materials it is about.
  EXPECT_EQ(diagnostics.front().object_name, "Bun");
  EXPECT_EQ(diagnostics.front().object_path, "/materials/0");
}

NOLINT_TEST(
  DroppedSourceFeaturesTest, UnsupportedMaterialExtensionsAreNamedInOneReport)
{
  auto material = cgltf_material {};
  material.has_iridescence = 1;
  material.has_emissive_strength = 1;
  auto source = cgltf_data {};
  source.materials = &material;
  source.materials_count = 1U;
  auto diagnostics = std::vector<ImportDiagnostic> {};

  ReportDroppedSourceFeatures(source, "glow.gltf", diagnostics);

  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_TRUE(AllDropWarnings(diagnostics));
  EXPECT_THAT(diagnostics.front().message,
    ::testing::HasSubstr("iridescence and emissive strength"));
  EXPECT_TRUE(diagnostics.front().object_name.empty())
    << "an unnamed material has no name to report";
}

NOLINT_TEST(DroppedSourceFeaturesTest, UnmappedScalarFieldsAreReported)
{
  cgltf_material material {};
  material.has_specular = 1;
  material.specular.specular_color_factor[0] = 0.5F;
  material.has_sheen = 1;
  material.sheen.sheen_roughness_factor = 1.0F;
  cgltf_data source {};
  source.materials = &material;
  source.materials_count = 1U;
  std::vector<ImportDiagnostic> diagnostics;

  ReportDroppedSourceFeatures(source, "scalar.gltf", diagnostics);

  EXPECT_EQ(diagnostics.size(), 2U);
  EXPECT_TRUE(AllDropWarnings(diagnostics));
}

} // namespace
