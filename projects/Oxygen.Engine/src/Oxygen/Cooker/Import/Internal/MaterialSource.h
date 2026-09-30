//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/MaterialDomain.h>

namespace oxygen::content::import {

//! UV transform for a material texture slot.
struct MaterialUvTransform {
  float scale[2] = { 1.0F, 1.0F };
  float offset[2] = { 0.0F, 0.0F };
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
  bool assigned = false;
  std::string source_id;
  uint8_t uv_set = 0;
  MaterialUvTransform uv_transform;
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
  float base_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
  float normal_scale = 1.0f;
  float metalness = 0.0f;
  float roughness = 1.0f;
  float ambient_occlusion = 1.0f;
  float emissive_factor[3] = { 0.0f, 0.0f, 0.0f };
  float alpha_cutoff = 0.5f;
  float ior = 1.5f;
  float specular_factor = 1.0f;
  float sheen_color_factor[3] = { 0.0f, 0.0f, 0.0f };
  float clearcoat_factor = 0.0f;
  float clearcoat_roughness = 0.0f;
  float transmission_factor = 0.0f;
  float thickness_factor = 0.0f;
  float attenuation_color[3] = { 1.0f, 1.0f, 1.0f };
  float attenuation_distance = 0.0f;
  bool double_sided = false;
  bool unlit = false;
  bool roughness_as_glossiness = false;
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
  std::vector<ShaderRequest> shader_requests;
  std::optional<bool> content_hashing {};

  struct TextureSlot final {
    std::string_view name {};
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

  OXGN_COOK_NDAPI auto CanPackOrm() const noexcept -> bool;
};

} // namespace oxygen::content::import
