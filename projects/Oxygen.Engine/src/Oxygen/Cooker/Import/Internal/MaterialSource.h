//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/TextureSampling.h>
#include <Oxygen/Data/MaterialDomain.h>

namespace oxygen::content::import {

//! UV transform for a material texture slot.
struct MaterialUvTransform {
  std::array<float, 2> scale = { 1.0F, 1.0F };
  std::array<float, 2> offset = { 0.0F, 0.0F };
  float rotation_radians = 0.0F;
};

//! Material alpha mode from authoring.
enum class MaterialAlphaMode : uint8_t {
  kOpaque,
  kMasked,
  kBlended,
};

//! Shader request for material pipelines.
struct ShaderRequest {
  uint8_t shader_type = 0; // ShaderType enum value
  std::string source_path;
  std::string entry_point;
  std::string defines;
  uint64_t shader_hash = 0;
};

//! Texture binding for a single material slot.
struct MaterialTextureBinding {
  uint32_t index = 0;
  //! Set when another cooked source holds the texture: the material binds it
  //! by this key instead of `index`.
  std::optional<data::AssetKey> external_key;
  bool assigned = false;
  std::string source_id;
  uint8_t uv_set = 0;
  MaterialUvTransform uv_transform;
  TextureSamplerState sampler;
};

//! Texture bindings for all material slots.
struct MaterialTextureBindings {
  MaterialTextureBinding base_color;
  MaterialTextureBinding normal;
  MaterialTextureBinding metallic;
  MaterialTextureBinding roughness;
  MaterialTextureBinding ambient_occlusion;
  MaterialTextureBinding emissive;
  MaterialTextureBinding specular;
  MaterialTextureBinding sheen_color;
  MaterialTextureBinding clearcoat;
  MaterialTextureBinding clearcoat_normal;
  MaterialTextureBinding transmission;
  MaterialTextureBinding thickness;
};

//! Scalar material inputs.
struct MaterialInputs {
  std::array<float, 4> base_color = { 1.0F, 1.0F, 1.0F, 1.0F };
  float normal_scale = 1.0F;
  float metalness = 0.0F;
  float roughness = 1.0F;
  float ambient_occlusion = 1.0F;
  std::array<float, 3> emissive_factor = { 0.0F, 0.0F, 0.0F };
  float alpha_cutoff = 0.5F;
  float ior = 1.5F;
  //! Specular level: F0 = 0.08 x level (see `MaterialAssetDesc`).
  float specular_factor = 0.5F;
  std::array<float, 3> sheen_color_factor = { 0.0F, 0.0F, 0.0F };
  float clearcoat_factor = 0.0F;
  float clearcoat_roughness = 0.0F;
  float transmission_factor = 0.0F;
  float thickness_factor = 0.0F;
  std::array<float, 3> attenuation_color = { 1.0F, 1.0F, 1.0F };
  float attenuation_distance = 0.0F;
  bool double_sided = false;
  bool unlit = false;
  bool roughness_as_glossiness = false;
};

//! A material's procedural grid, shaded in world space on any surface.
struct MaterialProceduralGrid {
  //! World units between minor lines, along each axis of a plane.
  std::array<float, 2> spacing = { 1.0F, 1.0F };
  //! Minor cells per major line.
  uint32_t major_every = 10;
  //! Line widths in world units, as the ground grid's: a minor line as wide as
  //! the spacing fills the surface.
  float line_thickness = 0.02F;
  float major_thickness = 0.04F;
  float axis_thickness = 0.06F;
  //! View distances over which the grid fades out; none when the end is not
  //! past the start.
  float fade_start = 0.0F;
  float fade_end = 0.0F;
  std::array<float, 4> minor_color = { 0.35F, 0.35F, 0.35F, 1.0F };
  std::array<float, 4> major_color = { 0.55F, 0.55F, 0.55F, 1.0F };
  std::array<float, 4> axis_color_x = { 0.90F, 0.20F, 0.20F, 1.0F };
  std::array<float, 4> axis_color_y = { 0.20F, 0.60F, 0.90F, 1.0F };
  std::array<float, 4> origin_color = { 1.0F, 1.0F, 1.0F, 1.0F };
};

//! ORM packing policy for metallic/roughness/AO.
enum class OrmPolicy : uint8_t {
  kAuto,
  kForcePacked,
  kForceSeparate,
};

//! Interpretation of the ambient-occlusion scalar when a texture is assigned.
enum class AmbientOcclusionMode : uint8_t {
  kFactor,
  kStrength,
};

//! Material semantics shared by descriptors, model adapters and analysis.
struct MaterialSource final {
  std::string name;
  std::string storage_name;
  data::MaterialDomain domain = data::MaterialDomain::kOpaque;
  MaterialAlphaMode alpha_mode = MaterialAlphaMode::kOpaque;
  MaterialInputs inputs;
  MaterialTextureBindings textures;
  OrmPolicy orm_policy = OrmPolicy::kAuto;
  AmbientOcclusionMode occlusion_mode = AmbientOcclusionMode::kFactor;
  std::optional<MaterialProceduralGrid> procedural_grid;
  std::vector<ShaderRequest> shader_requests;

  struct TextureSlot final {
    std::string_view name;
    MaterialTextureBinding MaterialTextureBindings::* binding = nullptr;
  };

  //! Stable native slot vocabulary used by preparation and reference
  //! resolution.
  OXGN_COOK_NDAPI static auto TextureSlots() noexcept
    -> std::span<const TextureSlot>;

  //! Validate and interpret descriptor bytes; no file or cooked-resource I/O.
  OXGN_COOK_NDAPI static auto FromDescriptor(std::string_view bytes,
    const std::filesystem::path& source_path, std::string_view name_override,
    std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<MaterialSource>;

  //! Checks source semantics before resource indices or binary packing are
  //! needed.
  OXGN_COOK_NDAPI auto Validate(std::string_view source_id,
    std::vector<ImportDiagnostic>& diagnostics) const -> bool;

  //! Brings values a model file states out of range into range, with a
  //! warning, so the material imports; descriptors are validated as authored.
  OXGN_COOK_API auto RepairModelInputs(std::string_view source_id,
    std::vector<ImportDiagnostic>& diagnostics) -> void;

  OXGN_COOK_NDAPI auto CanPackOrm() const noexcept -> bool;
};

} // namespace oxygen::content::import
