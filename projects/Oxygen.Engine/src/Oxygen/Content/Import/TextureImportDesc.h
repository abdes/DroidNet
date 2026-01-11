//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <Oxygen/Content/Import/TextureImportError.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace oxygen::content::import {

//! Complete import and cook contract for texture processing.
/*!
  This descriptor contains all parameters needed to decode, assemble, transform,
  generate mips, and select the final stored format for a texture.

  ### Key Concepts

  - **Identity**: `source_id` identifies the source asset for diagnostics
  - **Shape**: `texture_type`, `width`, `height`, `depth`, `array_layers` define
  geometry
  - **Intent**: `intent` guides content-specific processing (normal maps, HDR,
  etc.)
  - **Mip Policy**: Controls mip chain generation via `mip_policy`,
  `max_mip_levels`, `mip_filter`
  - **Output Format**: `output_format` specifies the final stored format
  - **BC7 Quality**: `bc7_quality` controls optional BC7 compression

  ### Usage Pattern

  ```cpp
  TextureImportDesc desc;
  desc.source_id = "textures\/brick_albedo.png";
  desc.texture_type = TextureType::kTexture2D;
  desc.width = 1024;
  desc.height = 1024;
  desc.intent = TextureIntent::kAlbedo;
  desc.source_color_space = ColorSpace::kSRGB;
  desc.output_format = Format::kBC7UNormSRGB;
  desc.bc7_quality = Bc7Quality::kDefault;

  if (auto error = desc.Validate()) {
    // Handle validation error
  }
  ```

  @note Packing policy (D3D12 vs TightPacked) is NOT part of this descriptor.
  Packing is a cook-time strategy selected per target backend and expressed
  in the payload header for the runtime loader.

  @see TextureIntent, MipPolicy, Bc7Quality, TextureImportError
*/
struct TextureImportDesc {
  //=== Identity ===----------------------------------------------------------//

  //! Source identifier for diagnostics and asset tracking.
  std::string source_id;

  //=== Shape / Dimensionality ===--------------------------------------------//

  //! Type of texture (2D, 3D, Cube, Array, etc.).
  TextureType texture_type = TextureType::kTexture2D;

  //! Texture width in pixels.
  uint32_t width = 0;

  //! Texture height in pixels.
  uint32_t height = 0;

  //! Depth for 3D textures, otherwise 1.
  uint16_t depth = 1;

  //! Number of array layers (1 for non-array textures, 6 for cubemaps).
  uint16_t array_layers = 1;

  //=== Content Intent ===----------------------------------------------------//

  //! Semantic intent for content-specific processing.
  TextureIntent intent = TextureIntent::kData;

  //=== Decode Options ===----------------------------------------------------//

  //! Flip image vertically during decode (common for OpenGL textures).
  bool flip_y_on_decode = false;

  //! Force RGBA output from decoder (expand grayscale/RGB to RGBA).
  bool force_rgba_on_decode = true;

  //=== Color / Sampling Policy ===-------------------------------------------//

  //! Color space of the source image data.
  /*!
    Specifies how the pixel values in the source image should be interpreted.
    This is authoring intent, not metadata extracted from the file.

    - `kSRGB`: Source pixels are in sRGB gamma space (typical for albedo,
      emissive, UI textures). Processing may convert to linear for filtering.
    - `kLinear`: Source pixels are linear (typical for normal maps, roughness,
      metallic, data textures).

    @note This field cannot be reliably inferred from image files. PNG/JPEG
    do not always encode color space metadata, and even when present it may
    be incorrect. The preset or user must specify the correct value.

    @note The `output_format` field specifies both the bit format AND the
    color space interpretation for the final stored texture (e.g.,
    `kRGBA8UNormSRGB` vs `kRGBA8UNorm`).
  */
  ColorSpace source_color_space = ColorSpace::kLinear;

  //=== Normal Map Options ===------------------------------------------------//

  //! Flip the green (Y) channel for normal maps (DirectX vs OpenGL convention).
  bool flip_normal_green = false;

  //! Renormalize normals in each mip level after downsampling.
  bool renormalize_normals_in_mips = true;

  //=== Mip Policy ===--------------------------------------------------------//

  //! Mip chain generation policy.
  MipPolicy mip_policy = MipPolicy::kFullChain;

  //! Maximum mip levels when `mip_policy == kMaxCount`.
  uint8_t max_mip_levels = 1;

  //! Filter kernel for mip generation.
  MipFilter mip_filter = MipFilter::kKaiser;

  //! Color space for mip filtering (typically linear for correct results).
  ColorSpace mip_filter_space = ColorSpace::kLinear;

  //=== Output Format ===-----------------------------------------------------//

  //! Final stored format for the texture data.
  Format output_format = Format::kRGBA8UNorm;

  //=== BC7 Compression ===---------------------------------------------------//

  //! BC7 compression quality tier (kNone to disable).
  Bc7Quality bc7_quality = Bc7Quality::kNone;

  //=== HDR Handling ===------------------------------------------------------//

  //! HDR content handling policy.
  /*!
    Controls what happens when HDR (float) content is encountered with an
    LDR (8-bit) output format:

    - `kError`: Fail with kHdrRequiresFloatFormat (explicit, strict)
    - `kTonemapAuto`: Automatically tonemap HDR→LDR (convenient, forgiving)
    - `kKeepFloat`: Override output_format to float (preserve HDR)

    When `hdr_handling` is `kTonemapAuto` or `kKeepFloat`, the `bake_hdr_to_ldr`
    field may be auto-adjusted based on the actual decoded format.

    @note Default is `kTonemapAuto` for convenience. Use `kError` for strict
    workflows where HDR/LDR mismatch should be an explicit error.
  */
  HdrHandling hdr_handling = HdrHandling::kTonemapAuto;

  //! Bake HDR content to LDR via tonemap + exposure.
  /*!
    When true, HDR content (RGBA32Float) is tonemapped to LDR (RGBA8).
    This is auto-set when `hdr_handling == kTonemapAuto` and HDR content
    is detected. Can also be set explicitly for manual control.
  */
  bool bake_hdr_to_ldr = false;

  //! Exposure adjustment in EV (applied before tonemapping).
  float exposure_ev = 0.0F;

  //=== Validation ===--------------------------------------------------------//

  //! Validate the descriptor for consistency and correctness.
  /*!
    Checks for common configuration errors such as:
    - Zero dimensions
    - Invalid texture type / array layer combinations
    - Depth specified for non-3D textures
    - HDR content with non-float output format (when bake_hdr_to_ldr is false)
    - Intent / format compatibility

    @return `std::nullopt` if valid, otherwise the first error encountered.
  */
  OXGN_CNTT_NDAPI auto Validate() const noexcept
    -> std::optional<TextureImportError>;
};

} // namespace oxygen::content::import
