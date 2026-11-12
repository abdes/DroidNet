//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/fbx/FbxShadingModel.cpp

#include <Oxygen/Cooker/Import/Internal/fbx/FbxShadingModel.h>
#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::adapters::FbxOpacity;
using oxygen::content::import::adapters::ShadingModelOf;

//! A material of `shader`, stating nothing else.
auto MaterialOf(const ufbx_shader_type shader) -> ufbx_material
{
  auto material = ufbx_material {};
  material.shader_type = shader;
  return material;
}

auto Bind(ufbx_material_map& map, ufbx_texture& texture) -> void
{
  map.texture = &texture;
  map.texture_enabled = true;
}

//! States `value` for `map`, as a number and as a grey colour: ufbx keeps the
//! number in the colour's first component.
auto State(ufbx_material_map& map, const float value) -> void
{
  map.has_value = true;
  map.value_vec3.x = value;
  map.value_vec3.y = value;
  map.value_vec3.z = value;
}

auto OpacityOf(const ufbx_material& material) -> FbxOpacity
{
  return ShadingModelOf(material).Opacity(material);
}

//! A classic material is 1 - f x T opaque, its factor and colour defaulting
//! like the FBX template: f 0, T white.
NOLINT_TEST(FbxShadingModelTest, ClassicOpacityIsOneMinusFactorTimesColour)
{
  auto unstated = MaterialOf(UFBX_SHADER_FBX_PHONG);
  auto stated = MaterialOf(UFBX_SHADER_FBX_PHONG);
  State(stated.fbx.transparency_factor, 0.4F);
  State(stated.fbx.transparency_color, 0.5F);

  EXPECT_FLOAT_EQ(OpacityOf(unstated).value, 1.0F);
  EXPECT_FLOAT_EQ(OpacityOf(stated).value, 0.8F);
  EXPECT_EQ(OpacityOf(stated).map.Texture(), nullptr);
}

//! A classic transparency map replaces the value it is on: its alpha is one
//! minus it, scaled by the other value, and the material's value is 1.
NOLINT_TEST(FbxShadingModelTest, ClassicTransparencyMapIsInvertedAndScaled)
{
  auto mask = ufbx_texture {};
  auto on_colour = MaterialOf(UFBX_SHADER_FBX_LAMBERT);
  State(on_colour.fbx.transparency_factor, 0.39F);
  Bind(on_colour.fbx.transparency_color, mask);
  auto on_factor = MaterialOf(UFBX_SHADER_FBX_LAMBERT);
  State(on_factor.fbx.transparency_color, 0.5F);
  Bind(on_factor.fbx.transparency_factor, mask);

  const auto colour = OpacityOf(on_colour);
  const auto factor = OpacityOf(on_factor);

  EXPECT_EQ(colour.map.Texture(), &mask);
  EXPECT_FLOAT_EQ(colour.value, 1.0F);
  EXPECT_FLOAT_EQ(colour.mapping.factor, 0.39F);
  EXPECT_TRUE(colour.mapping.inverted);
  EXPECT_EQ(factor.map.Texture(), &mask);
  EXPECT_FLOAT_EQ(factor.mapping.factor, 0.5F);
  EXPECT_TRUE(factor.mapping.inverted);
}

//! A transparency map that nothing scales takes nothing away: the material
//! is opaque, has no opacity map, and names the map it does not use.
NOLINT_TEST(FbxShadingModelTest, ClassicMapScaledByNothingIsOpaque)
{
  auto mask = ufbx_texture {};
  auto material = MaterialOf(UFBX_SHADER_FBX_PHONG);
  Bind(material.fbx.transparency_color, mask);

  const auto opacity = OpacityOf(material);

  EXPECT_EQ(opacity.map.Texture(), nullptr);
  EXPECT_FLOAT_EQ(opacity.value, 1.0F);
  EXPECT_EQ(opacity.unscaled_map.Texture(), &mask);
}

//! A PBR shader's opacity is its own: the legacy FBX transparency a 3ds Max
//! Physical Material also writes takes nothing away.
NOLINT_TEST(FbxShadingModelTest, PbrOpacityIgnoresLegacyTransparency)
{
  auto legacy = MaterialOf(UFBX_SHADER_3DS_MAX_PHYSICAL_MATERIAL);
  State(legacy.fbx.transparency_factor, 0.39F);
  auto own = legacy;
  State(own.pbr.opacity, 0.3F);

  EXPECT_FLOAT_EQ(OpacityOf(legacy).value, 1.0F);
  EXPECT_FLOAT_EQ(OpacityOf(own).value, 0.3F);
}

//! A PBR opacity map holds the alpha as it is and replaces the value.
NOLINT_TEST(FbxShadingModelTest, PbrOpacityMapIsTheAlpha)
{
  auto mask = ufbx_texture {};
  auto material = MaterialOf(UFBX_SHADER_3DS_MAX_PHYSICAL_MATERIAL);
  State(material.pbr.opacity, 0.3F);
  Bind(material.pbr.opacity, mask);

  const auto opacity = OpacityOf(material);

  EXPECT_EQ(opacity.map.Texture(), &mask);
  EXPECT_FLOAT_EQ(opacity.value, 1.0F);
  EXPECT_FLOAT_EQ(opacity.mapping.factor, 1.0F);
  EXPECT_FALSE(opacity.mapping.inverted);
}

//! A 3ds Max Physical Material whose transmission passes light straight
//! through: IOR 1, no roughness, no depth.
auto StraightThrough(const float transparency) -> ufbx_material
{
  auto material = MaterialOf(UFBX_SHADER_3DS_MAX_PHYSICAL_MATERIAL);
  material.features.transmission.enabled = true;
  State(material.pbr.transmission_factor, transparency);
  State(material.pbr.specular_ior, 1.0F);
  return material;
}

//! Transmission that passes light straight through is transparency, as
//! 3ds Max's glTF export states it: it scales the opacity, the cut-out map
//! included, and is no transmission.
NOLINT_TEST(FbxShadingModelTest, StraightThroughTransmissionIsTransparency)
{
  auto mask = ufbx_texture {};
  const auto plain = StraightThrough(0.65F);
  auto cut_out = StraightThrough(0.65F);
  Bind(cut_out.pbr.opacity, mask);

  const auto& model = ShadingModelOf(plain);

  EXPECT_FLOAT_EQ(OpacityOf(plain).value, 0.35F);
  EXPECT_FALSE(model.HasTransmission(plain));
  EXPECT_EQ(model.TransmissionSlot(plain).Texture(), nullptr);
  EXPECT_EQ(OpacityOf(cut_out).map.Texture(), &mask);
  EXPECT_FLOAT_EQ(OpacityOf(cut_out).value, 0.35F);
}

//! Without an opacity map, a straight-through transmission map is the map of
//! a transparency: one minus it, scaled by the transmission colour.
NOLINT_TEST(FbxShadingModelTest, StraightThroughTransmissionMapIsTransparency)
{
  auto mask = ufbx_texture {};
  auto material = StraightThrough(0.65F);
  Bind(material.pbr.transmission_factor, mask);

  const auto opacity = OpacityOf(material);

  EXPECT_EQ(opacity.map.Texture(), &mask);
  EXPECT_FLOAT_EQ(opacity.value, 1.0F);
  EXPECT_FLOAT_EQ(opacity.mapping.factor, 1.0F);
  EXPECT_TRUE(opacity.mapping.inverted);
}

//! Transmission that refracts, blurs or has depth stays transmission.
NOLINT_TEST(FbxShadingModelTest, RefractingTransmissionStaysTransmission)
{
  auto refracting = StraightThrough(0.65F);
  State(refracting.pbr.specular_ior, 1.5F);
  auto rough = StraightThrough(0.65F);
  State(rough.pbr.transmission_roughness, 0.2F);
  auto deep = StraightThrough(0.65F);
  State(deep.pbr.transmission_depth, 1.0F);

  for (const auto* material : { &refracting, &rough, &deep }) {
    EXPECT_TRUE(ShadingModelOf(*material).HasTransmission(*material));
    EXPECT_FLOAT_EQ(OpacityOf(*material).value, 1.0F);
  }
}

//! Blender reads an alpha map on `TransparentColor` as it is, as well as
//! the one ufbx gives as its opacity map.
NOLINT_TEST(FbxShadingModelTest, BlenderReadsTransparentColorAsAlpha)
{
  auto mask = ufbx_texture {};
  auto material = MaterialOf(UFBX_SHADER_BLENDER_PHONG);
  Bind(material.fbx.transparency_color, mask);

  const auto opacity = OpacityOf(material);

  EXPECT_EQ(opacity.map.Texture(), &mask);
  EXPECT_FALSE(opacity.mapping.inverted);
}

//! ufbx's PBR transmission of a classic material restates its transparency;
//! a PBR shader's states its own.
NOLINT_TEST(FbxShadingModelTest, ClassicTransmissionRestatesTransparency)
{
  const auto classic = MaterialOf(UFBX_SHADER_FBX_PHONG);
  const auto pbr = MaterialOf(UFBX_SHADER_3DS_MAX_PHYSICAL_MATERIAL);
  const auto& classic_model = ShadingModelOf(classic);

  EXPECT_EQ(classic_model.Restated(classic, classic.pbr.transmission_factor),
    &classic.fbx.transparency_factor);
  EXPECT_EQ(classic_model.Restated(classic, classic.pbr.transmission_color),
    &classic.fbx.transparency_color);
  EXPECT_EQ(classic_model.Restated(classic, classic.pbr.base_color),
    &classic.pbr.base_color);
  EXPECT_EQ(ShadingModelOf(pbr).Restated(pbr, pbr.pbr.transmission_factor),
    &pbr.pbr.transmission_factor);
}

} // namespace
