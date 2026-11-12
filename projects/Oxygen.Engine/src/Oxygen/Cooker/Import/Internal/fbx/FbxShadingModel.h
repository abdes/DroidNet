//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxTextureSlot.h>
#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import::adapters {

//! How much of a material's surface is there, as its shader states it.
struct FbxOpacity final {
  //! The opacity the material states apart from its map: the alpha factor of
  //! its base colour, which also scales the map's alpha.
  float value = 1.0F;
  //! The opacity map, whose value becomes the alpha of the base colour map.
  FbxTextureSlot map {};
  //! How the map's value becomes that alpha.
  CoverageMapping mapping {};
  //! A transparency map the material scales by nothing, so it takes nothing
  //! away and is not imported; kept so the import can say why.
  FbxTextureSlot unscaled_map {};
};

//! What a family of FBX shaders means by the parameters ufbx reads for it.
/*!
 ufbx names every shader's parameters alike, yet shaders mean different things
 by them: a classic FBX material's transparency is how much of its surface is
 missing, a PBR shader's is the light passing through it; its specular is a
 highlight, a reflectance weight or Blender's level. Each family of shaders
 has one model, and the importer asks the model of a material
 (`ShadingModelOf`) what each of its maps and values means.
*/
class FbxShadingModel {
public:
  virtual ~FbxShadingModel() = default;

  OXYGEN_MAKE_NON_COPYABLE(FbxShadingModel)
  OXYGEN_MAKE_NON_MOVABLE(FbxShadingModel)

  //! The map of `material`'s specular level; none when the shader's specular
  //! is no reflectance.
  [[nodiscard]] virtual auto SpecularSlot(
    const ufbx_material& material) const noexcept -> FbxTextureSlot = 0;

  //! `material`'s specular level: F0 = 0.08 x level, 0.5 by default. With a
  //! specular map (`textured`), the level is the factor the map scales.
  [[nodiscard]] virtual auto SpecularLevel(
    const ufbx_material& material, bool textured) const noexcept -> float = 0;

  //! Whether `material` lets light pass through its surface.
  [[nodiscard]] virtual auto HasTransmission(
    const ufbx_material& material) const noexcept -> bool = 0;

  //! The map of the light `material` lets through; none when it lets none.
  [[nodiscard]] virtual auto TransmissionSlot(
    const ufbx_material& material) const noexcept -> FbxTextureSlot = 0;

  //! How much of `material`'s surface is there: its opacity and opacity map.
  [[nodiscard]] virtual auto Opacity(
    const ufbx_material& material) const noexcept -> FbxOpacity = 0;

  //! The map of `material` whose meaning ufbx restates as `map`, which it
  //! derived from it; `map` itself when it states its own. Never null.
  [[nodiscard]] virtual auto Restated(
    const ufbx_material& material, const ufbx_material_map& map) const noexcept
    -> const ufbx_material_map* = 0;

protected:
  FbxShadingModel() = default;
};

//! The model of the shader of `material`.
OXGN_COOK_NDAPI auto ShadingModelOf(const ufbx_material& material) noexcept
  -> const FbxShadingModel&;

} // namespace oxygen::content::import::adapters
