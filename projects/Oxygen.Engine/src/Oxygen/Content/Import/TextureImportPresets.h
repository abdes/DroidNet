//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Content/Import/TextureImportDesc.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

//! Preset identifiers for common texture import configurations.
/*!
  Presets provide sensible defaults for typical authoring workflows. Select a
  preset first, then apply minimal overrides for specific requirements.

  ### LDR Material Presets

  - `kAlbedo`: Base color with sRGB, Kaiser mips, BC7 compression
  - `kNormal`: Tangent-space normal map with renormalization
  - `kRoughness`, `kMetallic`, `kAO`: Single-channel masks, linear
  - `kORMPacked`: Combined ORM texture (R=AO, G=Roughness, B=Metallic)
  - `kEmissive`: Emissive color with sRGB, BC7 compression
  - `kHeightMap`: Displacement/parallax map with R16UNorm for precision
  - `kUI`: Sharp text/icons with Lanczos filter

  ### HDR Presets

  - `kHdrEnvironment`: Skybox in RGBA16Float
  - `kHdrLightProbe`: IBL source in RGBA16Float

  @see ApplyPreset, TextureImportDesc
*/
enum class TexturePreset : uint8_t {
  // clang-format off
  kAlbedo         = 0,   //!< Base color / diffuse albedo
  kNormal         = 1,   //!< Tangent-space normal map
  kRoughness      = 2,   //!< Roughness map (single channel)
  kMetallic       = 3,   //!< Metallic map (single channel)
  kAO             = 4,   //!< Ambient occlusion map
  kORMPacked      = 5,   //!< Packed ORM (R=AO, G=Roughness, B=Metallic)
  kEmissive       = 6,   //!< Emissive color
  kUI             = 7,   //!< UI / Text (high-frequency detail)
  kHdrEnvironment = 8,   //!< HDR environment skybox
  kHdrLightProbe  = 9,   //!< HDR light probe for IBL
  kData           = 10,  //!< Generic data texture
  kHeightMap      = 11,  //!< Height / displacement map (high precision)
  // clang-format on
};

//! String representation of enum values in `TexturePreset`.
OXGN_CNTT_NDAPI auto to_string(TexturePreset value) -> const char*;

//! Metadata describing a texture preset.
/*!
  Contains display information about a preset for editor UI and diagnostics.
*/
struct TexturePresetMetadata {
  //! Human-readable name for the preset.
  const char* name = nullptr;

  //! Brief description of the preset's purpose.
  const char* description = nullptr;

  //! Whether this preset is for HDR content.
  bool is_hdr = false;

  //! Whether this preset uses BC7 compression by default.
  bool uses_bc7 = false;
};

//! Get metadata for a texture preset.
/*!
  Returns display information about the preset for editor UI and diagnostics.

  @param preset The preset to query
  @return Metadata describing the preset
*/
OXGN_CNTT_NDAPI auto GetPresetMetadata(TexturePreset preset) noexcept
  -> TexturePresetMetadata;

//! Apply a preset to a TextureImportDesc.
/*!
  Populates the descriptor with sensible defaults for the specified preset.
  The `source_id`, `width`, `height`, `depth`, and `array_layers` fields are
  NOT modified — these must be set by the caller based on the source image.

  After applying a preset, callers may override individual fields as needed.

  ### Usage Example

  ```cpp
  TextureImportDesc desc;
  desc.source_id = "textures\/brick_albedo.png";
  desc.width = 1024;
  desc.height = 1024;

  ApplyPreset(desc, TexturePreset::kAlbedo);

  \/\/ Optional override
  desc.bc7_quality = Bc7Quality::kHigh;
  ```

  @param desc   Descriptor to modify (in-place)
  @param preset Preset to apply

  @see TexturePreset, TextureImportDesc
*/
OXGN_CNTT_API void ApplyPreset(
  TextureImportDesc& desc, TexturePreset preset) noexcept;

//! Create a TextureImportDesc from a preset.
/*!
  Convenience function that creates a new descriptor and applies the preset.
  The caller must still set identity and shape fields.

  @param preset Preset to apply
  @return New descriptor with preset applied
*/
OXGN_CNTT_NDAPI auto MakeDescFromPreset(TexturePreset preset) noexcept
  -> TextureImportDesc;

} // namespace oxygen::content::import
