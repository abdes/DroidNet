//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>

#include <Oxygen/Cooker/Import/Internal/fbx/FbxShadingModel.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxTextureSlot.h>
#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>

namespace oxygen::content::import::adapters {

namespace {

  //! The specular level a shader that states none has: F0 = 0.04.
  constexpr auto kDefaultSpecularLevel = 0.5F;

  [[nodiscard]] auto Clamp01(const double value) noexcept -> float
  {
    return std::clamp(static_cast<float>(value), 0.0F, 1.0F);
  }

  //! A PBR shader: 3ds Max's Physical Material, Autodesk's Standard Surface,
  //! Arnold and the other shaders ufbx reads as PBR.
  /*!
   Its specular is a reflectance weight, 1.0 the default reflectance. Its
   transmission is light passing through the surface, and transparency when
   it passes straight through. Its opacity, such as 3ds Max's cut-out map, is
   how much of the surface is there; a shader that states none is opaque,
   whatever its legacy FBX transparency says.
  */
  class PbrShading : public FbxShadingModel {
  public:
    [[nodiscard]] auto SpecularSlot(
      const ufbx_material& material) const noexcept -> FbxTextureSlot override
    {
      return {
        .primary = &material.pbr.specular_factor,
        .fallback = nullptr,
        .feature = &material.features.specular,
      };
    }

    [[nodiscard]] auto SpecularLevel(const ufbx_material& material,
      const bool /*textured*/) const noexcept -> float override
    {
      if (!material.features.specular.enabled) {
        return kDefaultSpecularLevel;
      }
      const auto weight = material.pbr.specular_factor.has_value
        ? material.pbr.specular_factor.value_real
        : 1.0;
      return Clamp01(kDefaultSpecularLevel * weight);
    }

    [[nodiscard]] auto HasTransmission(
      const ufbx_material& material) const noexcept -> bool override
    {
      return material.features.transmission.enabled
        && !PassesStraightThrough(material);
    }

    [[nodiscard]] auto TransmissionSlot(
      const ufbx_material& material) const noexcept -> FbxTextureSlot override
    {
      if (PassesStraightThrough(material)) {
        return {};
      }
      // A file may give only the colour map.
      return TransmissionMaps(material);
    }

    //! The shader's own opacity, times the see-through part of a transmission
    //! that passes light straight through. The opacity map replaces the
    //! shader's opacity value; without one, a straight-through transmission
    //! map is the map of a transparency.
    [[nodiscard]] auto Opacity(const ufbx_material& material) const noexcept
      -> FbxOpacity final
    {
      const auto& own = material.pbr.opacity;
      auto opacity = FbxOpacity {
        .value = own.has_value ? Clamp01(own.value_real) : 1.0F,
        .map = OpacitySlot(material),
        .mapping = {},
        .unscaled_map = {},
      };
      // The map replaces the shader's opacity value, as every FBX slot's map
      // replaces its value in this importer. ufbx notes that most shaders
      // multiply value and map instead, so a file stating, say, Standard
      // Surface opacity 0.5 beside a map imports the map alone. FBX keeps no
      // flag saying which a shader does, and multiplying would make any file
      // whose exporter wrote a stale value beside its map partly transparent.
      if (opacity.map.Texture() != nullptr) {
        opacity.value = 1.0F;
      } else {
        opacity.map = {};
      }
      if (!PassesStraightThrough(material)) {
        return opacity;
      }
      const auto& factor = material.pbr.transmission_factor;
      const auto& color = material.pbr.transmission_color;
      const auto amount = factor.has_value ? Clamp01(factor.value_real) : 0.0F;
      const auto tint = color.has_value
        ? Clamp01((color.value_vec3.x + color.value_vec3.y + color.value_vec3.z)
            / 3.0)
        : 1.0F;
      const auto transmission_map = TransmissionMaps(material);
      if (opacity.map.Texture() == nullptr
        && transmission_map.Texture() != nullptr) {
        opacity.map = transmission_map;
        opacity.mapping = {
          .factor
          = transmission_map.primary->texture != nullptr ? tint : amount,
          .inverted = true,
        };
        return opacity;
      }
      opacity.value *= Clamp01(1.0 - (static_cast<double>(amount) * tint));
      return opacity;
    }

    [[nodiscard]] auto Restated(const ufbx_material& /*material*/,
      const ufbx_material_map& map) const noexcept
      -> const ufbx_material_map* final
    {
      return &map;
    }

  protected:
    //! Whether `material`'s transmission passes light straight through: it
    //! does not refract (IOR 1, which 3ds Max's Physical Material shares with
    //! its specular), blur or absorb with depth. Such light shows what is
    //! behind the surface as it is, which is transparency, as 3ds Max's own
    //! glTF export states it.
    [[nodiscard]] static auto PassesStraightThrough(
      const ufbx_material& material) noexcept -> bool
    {
      constexpr auto kTolerance = 1e-4;
      const auto unstated_or_zero
        = [](const ufbx_material_map& map) noexcept -> bool {
        return !map.has_value || std::abs(map.value_real) <= kTolerance;
      };
      const auto& ior = material.pbr.specular_ior;
      return material.features.transmission.enabled && ior.has_value
        && std::abs(ior.value_real - 1.0) <= kTolerance
        && unstated_or_zero(material.pbr.transmission_roughness)
        && unstated_or_zero(material.pbr.transmission_depth);
    }

    //! The maps of the light `material` lets through.
    [[nodiscard]] static auto TransmissionMaps(
      const ufbx_material& material) noexcept -> FbxTextureSlot
    {
      return {
        .primary = &material.pbr.transmission_factor,
        .fallback = &material.pbr.transmission_color,
        .feature = &material.features.transmission,
      };
    }

    //! The map of `material`'s opacity, which holds it as it is.
    [[nodiscard]] virtual auto OpacitySlot(
      const ufbx_material& material) const noexcept -> FbxTextureSlot
    {
      return {
        .primary = &material.pbr.opacity,
        .fallback = nullptr,
        .feature = nullptr,
      };
    }
  };

  //! Blender's Principled BSDF as Blender writes it to FBX: a PBR shader whose
  //! specular is Blender's own level, which a map holds whole.
  /*!
   Blender writes its alpha map on `TransparencyFactor`, which ufbx gives as
   the opacity map, and reads one on `TransparentColor` as well; either holds
   the alpha as it is.
  */
  class BlenderPhongShading final : public PbrShading {
  public:
    [[nodiscard]] auto SpecularSlot(
      const ufbx_material& material) const noexcept -> FbxTextureSlot override
    {
      return {
        .primary = &material.fbx.specular_factor,
        .fallback = nullptr,
        .feature = nullptr,
      };
    }

    [[nodiscard]] auto SpecularLevel(const ufbx_material& material,
      const bool textured) const noexcept -> float override
    {
      if (textured) {
        return 1.0F;
      }
      return material.fbx.specular_factor.has_value
        ? Clamp01(material.fbx.specular_factor.value_real)
        : kDefaultSpecularLevel;
    }

  protected:
    [[nodiscard]] auto OpacitySlot(const ufbx_material& material) const noexcept
      -> FbxTextureSlot override
    {
      return {
        .primary = &material.pbr.opacity,
        .fallback = &material.fbx.transparency_color,
        .feature = nullptr,
      };
    }
  };

  //! A classic FBX material: Lambert, Phong, or a shader ufbx reads as Phong.
  /*!
   Its specular shapes a highlight; it is no reflectance, so none is imported.
   Its `TransparentColor` T and `TransparencyFactor` f state how much of the
   surface is missing, never light passing through it: its opacity is
   1 - f x T, each defaulting like the FBX material template (f 0, T white).
   A map replaces the value it is on and the other value scales it, so its
   alpha is one minus it, scaled. ufbx also gives T and f as the material's
   PBR transmission, which restates them.

   A `TransparentColor` map is read as FBX defines it: transparency, white
   where the surface is missing. Exporters disagree: some write an opacity
   map there, and Blender's importer reads one as alpha, so a decal from
   such a tool imports inverted. The importer keeps the definition, because
   FBX states no polarity to tell them apart; Blender's own files are read
   by `BlenderPhongShading`, which follows Blender.
  */
  class ClassicShading final : public FbxShadingModel {
  public:
    [[nodiscard]] auto SpecularSlot(
      const ufbx_material& /*material*/) const noexcept
      -> FbxTextureSlot override
    {
      return {};
    }

    [[nodiscard]] auto SpecularLevel(const ufbx_material& /*material*/,
      const bool /*textured*/) const noexcept -> float override
    {
      return kDefaultSpecularLevel;
    }

    [[nodiscard]] auto HasTransmission(
      const ufbx_material& /*material*/) const noexcept -> bool override
    {
      return false;
    }

    [[nodiscard]] auto TransmissionSlot(
      const ufbx_material& /*material*/) const noexcept
      -> FbxTextureSlot override
    {
      return {};
    }

    [[nodiscard]] auto Opacity(const ufbx_material& material) const noexcept
      -> FbxOpacity override
    {
      const auto& color = material.fbx.transparency_color;
      const auto& factor = material.fbx.transparency_factor;
      const auto amount = factor.has_value ? Clamp01(factor.value_real) : 0.0F;
      const auto tint = color.has_value
        ? Clamp01((color.value_vec3.x + color.value_vec3.y + color.value_vec3.z)
            / 3.0)
        : 1.0F;
      const auto color_map = FbxTextureSlot {
        .primary = &color,
        .fallback = nullptr,
        .feature = nullptr,
      };
      const auto factor_map = FbxTextureSlot {
        .primary = &factor,
        .fallback = nullptr,
        .feature = nullptr,
      };
      // A map scaled by nothing takes nothing away: 1 - 0 x T is 1 whatever
      // T holds, so the material is opaque. This is FBX's definition, kept
      // even though an exporter may connect a map without writing a factor
      // and mean the map to apply: guessing a factor would make other files'
      // opaque materials transparent. The map is kept as `unscaled_map` so
      // the import reports why it is not used.
      if (color_map.Texture() != nullptr) {
        return amount > 0.0F ? Covered(color_map, amount) : Unscaled(color_map);
      }
      if (factor_map.Texture() != nullptr) {
        return tint > 0.0F ? Covered(factor_map, tint) : Unscaled(factor_map);
      }
      return {
        .value = Clamp01(1.0 - (static_cast<double>(amount) * tint)),
        .map = {},
        .mapping = {},
        .unscaled_map = {},
      };
    }

    [[nodiscard]] auto Restated(const ufbx_material& material,
      const ufbx_material_map& map) const noexcept
      -> const ufbx_material_map* override
    {
      if (&map == &material.pbr.transmission_factor) {
        return &material.fbx.transparency_factor;
      }
      if (&map == &material.pbr.transmission_color) {
        return &material.fbx.transparency_color;
      }
      return &map;
    }

  private:
    //! The opacity of a transparency map that `scale` scales.
    [[nodiscard]] static auto Covered(
      const FbxTextureSlot& map, const float scale) noexcept -> FbxOpacity
    {
      return {
        .value = 1.0F,
        .map = map,
        .mapping = { .factor = scale, .inverted = true },
        .unscaled_map = {},
      };
    }

    //! The opacity of a transparency map that nothing scales: opaque.
    [[nodiscard]] static auto Unscaled(const FbxTextureSlot& map) noexcept
      -> FbxOpacity
    {
      return {
        .value = 1.0F,
        .map = {},
        .mapping = {},
        .unscaled_map = map,
      };
    }
  };

} // namespace

auto ShadingModelOf(const ufbx_material& material) noexcept
  -> const FbxShadingModel&
{
  static const auto classic = ClassicShading {};
  static const auto blender = BlenderPhongShading {};
  static const auto pbr = PbrShading {};
  switch (material.shader_type) {
  case UFBX_SHADER_UNKNOWN:
  case UFBX_SHADER_FBX_LAMBERT:
  case UFBX_SHADER_FBX_PHONG:
    return classic;
  case UFBX_SHADER_BLENDER_PHONG:
    return blender;
  default:
    return pbr;
  }
}

} // namespace oxygen::content::import::adapters
