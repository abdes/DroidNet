//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_core.h>

// packed structs intentionally embed unaligned NamedType ResourceIndexT fields
OXYGEN_DIAGNOSTIC_PUSH
OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4315)
// NOLINTBEGIN(*-avoid-c-arrays,*-magic-numbers)

//! Oxygen PAK format render domain schema.
/*!
 Owns material, shader-reference, and texture payload/resource contracts.
*/
namespace oxygen::data::pak::render {

//! Material flag indicating that textures must not be sampled.
//!
//! When set, the renderer/shaders must ignore all texture references for the
//! material and use scalar fallbacks only.
//!
//! Per-slot absent references remain distinct; this flag disables every slot.
[[maybe_unused]] constexpr uint32_t kMaterialFlag_NoTextureSampling = (1U << 0);

//! Material flag indicating that the material should be treated as
//! double-sided.
//!
//! When set, the renderer should disable backface culling for this material.
[[maybe_unused]] constexpr uint32_t kMaterialFlag_DoubleSided = (1U << 1);

//! Material flag indicating that the material uses alpha testing (cutout).
//!
//! When set, the renderer/shaders should apply alpha cutoff testing using the
//! material's `alpha_cutoff` parameter.
[[maybe_unused]] constexpr uint32_t kMaterialFlag_AlphaTest = (1U << 2);

//! Material flag indicating that the material is unlit.
//!
//! When set, shading should not apply lighting and should render using
//! base color + emissive only.
[[maybe_unused]] constexpr uint32_t kMaterialFlag_Unlit = (1U << 3);

//! Material flag indicating glTF ORM channel packing semantics.
//!
//! When set, the metallic/roughness texture(s) follow glTF conventions:
//! - Roughness is sampled from the G channel
//! - Metalness is sampled from the B channel
//! Ambient occlusion is typically sampled from the R channel of the AO/ORM
//! texture.
[[maybe_unused]] constexpr uint32_t kMaterialFlag_GltfOrmPacked = (1U << 4);

//! Material flag indicating procedural grid shading.
[[maybe_unused]] constexpr uint32_t kMaterialFlag_ProceduralGrid = (1U << 5);

//! Interpret ambient_occlusion as texture strength: lerp(1, sample, strength).
//! Without a sampled AO binding, this mode is unoccluded.
inline constexpr uint32_t kMaterialFlag_AmbientOcclusionStrength = (1U << 6);

//! Material asset descriptor version for current PAK schema.
[[maybe_unused]] constexpr uint8_t kMaterialAssetVersion
  = version::kMaterialAssetVersion;

//! Maximum finite channel value accepted for material emission.
inline constexpr float kMaxMaterialEmissiveFactor = 65504.0F;

//! Shader descriptor
/*!
  Describes a shader stage for material or pipeline binding. Does not contain
  bytecode; only metadata and lookup information.

  - `shader_type`: Shader stage (ShaderType enum value).
  - `source_path`: Canonical repo-relative shader source path (forward slashes,
    normalized, no absolute paths).
  - `entry_point`: Explicit entry point name.
  - `defines`: Canonical defines string for compilation (sorted, unique names).
  - `shader_hash`: 64-bit hash of shader source for validation.
*/
#pragma pack(push, 1)
struct ShaderReferenceDesc {
  uint8_t shader_type = 0; // ShaderType enum value
  char source_path[120] = {}; // Null-terminated, null-padded
  char entry_point[32] = {}; // Null-terminated, null-padded
  char defines[256] = {}; // Null-terminated, null-padded (may be empty)

  uint64_t shader_hash = 0; // Hash of source for validation
};
#pragma pack(pop)
static_assert(sizeof(ShaderReferenceDesc) == 417);

//! Material asset descriptor with explicit UV transform and
//! procedural grid extensions.
/*!
  This layer replaces the trailing reserved bytes with named UV transform fields
  and appends a procedural grid block.

  @see ShaderReferenceDesc
*/
#pragma pack(push, 1)
// NOLINTNEXTLINE(*-type-member-init) - MUST be initialized by users
struct MaterialAssetDesc {
  core::AssetHeader header;
  uint8_t material_domain; // e.g. Opaque, AlphaBlended
  uint32_t flags; // Bitfield for double-sided, alpha test, etc.
  uint32_t shader_stages; // Bitfield for shaders used; entries that follow
                          // are in ascending bit index order (LSB->MSB)

  // --- Scalar factors (PBR) ---
  float base_color[4] = { 1.0F, 1.0F, 1.0F, 1.0F }; // RGBA fallback
  float normal_scale = 1.0F;
  Unorm16 metalness = Unorm16 { 0.0F };
  Unorm16 roughness = Unorm16 { 1.0F };
  Unorm16 ambient_occlusion = Unorm16 { 1.0F };

  // --- Core texture references (Index into TextureResourceTable,
  // core::kNoResourceIndex = invalid/none) ---
  ResourceReferenceIndex base_color_texture = kNoResourceReference;
  ResourceReferenceIndex normal_texture = kNoResourceReference;
  ResourceReferenceIndex metallic_texture = kNoResourceReference;
  ResourceReferenceIndex roughness_texture = kNoResourceReference;
  ResourceReferenceIndex ambient_occlusion_texture = kNoResourceReference;

  static_assert(core::kNoResourceIndex == 0);

  // --- Additional texture references (optional, Tier 1/2) ---
  ResourceReferenceIndex emissive_texture = kNoResourceReference;
  ResourceReferenceIndex specular_texture = kNoResourceReference;
  ResourceReferenceIndex sheen_color_texture = kNoResourceReference;
  ResourceReferenceIndex clearcoat_texture = kNoResourceReference;
  ResourceReferenceIndex clearcoat_normal_texture = kNoResourceReference;
  ResourceReferenceIndex transmission_texture = kNoResourceReference;
  ResourceReferenceIndex thickness_texture = kNoResourceReference;

  // --- Additional scalar parameters (Tier 1/2) ---
  // Emissive
  float emissive_factor[3] = { 0.0F, 0.0F, 0.0F };
  // Alpha
  Unorm16 alpha_cutoff = Unorm16 { 0.5F };
  // Dielectric response
  float ior = 1.5F;
  Unorm16 specular_factor = Unorm16 { 1.0F };
  // Sheen (KHR_materials_sheen)
  HalfFloat sheen_color_factor[3]
    = { HalfFloat { 0.0F }, HalfFloat { 0.0F }, HalfFloat { 0.0F } };
  // Clearcoat (KHR_materials_clearcoat)
  Unorm16 clearcoat_factor = Unorm16 { 0.0F };
  Unorm16 clearcoat_roughness = Unorm16 { 0.0F };
  // Transmission / Volume (KHR_materials_transmission + KHR_materials_volume)
  Unorm16 transmission_factor = Unorm16 { 0.0F };
  Unorm16 thickness_factor = Unorm16 { 0.0F };
  HalfFloat attenuation_color[3]
    = { HalfFloat { 1.0F }, HalfFloat { 1.0F }, HalfFloat { 1.0F } };
  float attenuation_distance = 0.0F;

  // --- UV transform extension ---
  float uv_scale[2] = { 1.0F, 1.0F };
  float uv_offset[2] = { 0.0F, 0.0F };
  float uv_rotation_radians = 0.0F;
  uint8_t uv_set = 0;

  // --- Procedural grid extension ---
  float grid_spacing[2] = { 1.0F, 1.0F };
  uint32_t grid_major_every = 10;
  float grid_line_thickness = 1.0F;
  float grid_major_thickness = 2.0F;
  float grid_axis_thickness = 2.0F;
  float grid_fade_start = 0.0F;
  float grid_fade_end = 0.0F;

  float grid_minor_color[4] = { 0.35F, 0.35F, 0.35F, 1.0F };
  float grid_major_color[4] = { 0.55F, 0.55F, 0.55F, 1.0F };
  float grid_axis_color_x[4] = { 0.90F, 0.20F, 0.20F, 1.0F };
  float grid_axis_color_y[4] = { 0.20F, 0.60F, 0.90F, 1.0F };
  float grid_origin_color[4] = { 1.0F, 1.0F, 1.0F, 1.0F };
};
// Followed by:
// - Array of ShaderReferenceDesc entries in ascending set-bit order of
//   `shader_stages` (least-significant set bit first). Count is population
//   count of `shader_stages`.
#pragma pack(pop)
static_assert(sizeof(MaterialAssetDesc) == 363);

//=== Texture Payload Structures ===-----------------------------------------//

//! Packing policy identifier stored in texture payload headers.
/*!
  Identifies the alignment and packing strategy used for subresource data
  within a texture payload. The runtime loader uses this to correctly
  interpret offsets and pitches.
*/
enum class TexturePackingPolicyId : uint8_t {
  kD3D12 = 1, //!< D3D12 alignment: 256-byte row pitch, 512-byte subresource
  kTightPacked = 2, //!< Minimal alignment; suitable for Vulkan or offline tools
};

//! Extensibility flags for texture payloads.
/*!
  Provides optional metadata about the texture payload content.
*/
enum class TexturePayloadFlags : uint8_t {
  kNone = 0,
  kPremultipliedAlpha = (1 << 0), //!< Alpha is premultiplied
  kTailMipsUncompressed = (
    1 << 1), //!< Tail mips stored uncompressed (reserved)
  // Bits 2-7 reserved for future use.
};

//! Bitwise OR for TexturePayloadFlags.
[[nodiscard]] constexpr auto operator|(TexturePayloadFlags lhs,
  TexturePayloadFlags rhs) noexcept -> TexturePayloadFlags
{
  return static_cast<TexturePayloadFlags>(
    static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

//! Bitwise AND for TexturePayloadFlags.
[[nodiscard]] constexpr auto operator&(TexturePayloadFlags lhs,
  TexturePayloadFlags rhs) noexcept -> TexturePayloadFlags
{
  return static_cast<TexturePayloadFlags>(
    static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

//! Check if a flag is set.
[[nodiscard]] constexpr auto HasFlag(
  TexturePayloadFlags flags, TexturePayloadFlags flag) noexcept -> bool
{
  return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

//! Per-subresource layout descriptor within a texture payload.
/*!
 Stored as an array immediately after `TexturePayloadHeader`.
 Subresources are ordered: layer 0 mips 0..N-1, layer 1 mips 0..N-1, ...
*/
#pragma pack(push, 1)
struct SubresourceLayout {
  uint32_t offset_bytes = 0; //!< Offset from start of payload data section
  uint32_t row_pitch_bytes = 0; //!< Row pitch (may include alignment padding)
  uint32_t size_bytes = 0; //!< Total bytes of this subresource
};
#pragma pack(pop)
static_assert(sizeof(SubresourceLayout) == 12);

//! Magic value for texture payload headers: 'OTX1' as little-endian.
inline constexpr uint32_t kTexturePayloadMagic = 0x3158544F;

//! Header at the start of each texture's payload data in `textures.data`.
/*!
 The runtime loader reads this header to determine subresource layout without
 hardcoding backend-specific alignment rules.

 ### Memory Layout

 ```
 TexturePayloadHeader
 SubresourceLayout[subresource_count] (12 bytes each)
 [padding to data_offset_bytes]
 Pixel/block data for all subresources
 ```

 @note `layouts_offset_bytes` is typically `sizeof(TexturePayloadHeader)` (28).
 @note `data_offset_bytes` is `layouts_offset_bytes + 12 * subresource_count`,
       possibly padded for alignment.
*/
#pragma pack(push, 1)
struct TexturePayloadHeader {
  uint32_t magic = kTexturePayloadMagic; //!< 'OTX1' for validation
  uint8_t packing_policy = 0; //!< TexturePackingPolicyId
  uint8_t flags = 0; //!< TexturePayloadFlags
  uint16_t subresource_count = 0; //!< array_layers * mip_levels
  uint32_t total_payload_size = 0; //!< Total size including header
  uint32_t layouts_offset_bytes = 0; //!< Offset to SubresourceLayout array
  uint32_t data_offset_bytes = 0; //!< Offset to pixel/block data
  uint64_t content_hash = 0; //!< Content hash for validation
};
#pragma pack(pop)
static_assert(sizeof(TexturePayloadHeader) == 28);

} // namespace oxygen::data::pak::render

// NOLINTEND(*-avoid-c-arrays,*-magic-numbers)
OXYGEN_DIAGNOSTIC_POP
