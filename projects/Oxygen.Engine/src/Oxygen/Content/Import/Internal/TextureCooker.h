//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Content/Import/ScratchImage.h>
#include <Oxygen/Content/Import/TextureImportDesc.h>
#include <Oxygen/Content/Import/TextureImportError.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/Import/TexturePackingPolicy.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

//===----------------------------------------------------------------------===//
// Texture Cooker API
//===----------------------------------------------------------------------===//

//! Cook a single-source texture from raw image bytes.
/*!
  Main entry point for cooking textures from a single source image file.
  Handles the complete pipeline:

  1. **Decode**: Source bytes → working format (RGBA8 or RGBA32Float)
  2. **Transform**: Color space conversion, HDR processing
  3. **Mip Generation**: Full chain, limited count, or none
  4. **Content Processing**: Normal map handling, etc.
  5. **Compression**: BC7 encoding if requested
  6. **Packing**: Aligned layout according to packing policy

  @param source_bytes Raw bytes of the source image (PNG, JPG, HDR, EXR, etc.)
  @param desc         Import descriptor specifying how to cook the texture
  @param policy       Packing policy for the target backend
  @return Cooked payload on success, or TextureImportError on failure

  @see ApplyPreset for easy descriptor configuration
  @see TextureSourceSet for multi-source textures (cubemaps, arrays)
*/
OXGN_CNTT_NDAPI auto CookTexture(std::span<const std::byte> source_bytes,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy,
  bool with_content_hashing = true)
  -> Result<CookedTexturePayload, TextureImportError>;

//! Cook a texture from an already-decoded ScratchImage.
/*!
  Use this overload when you have already decoded/processed the image
  (e.g., after equirectangular-to-cubemap conversion).

  Skips the decode stage and proceeds directly to:
  1. **Transform**: Color space conversion, HDR processing
  2. **Mip Generation**: Full chain, limited count, or none
  3. **Content Processing**: Normal map handling, etc.
  4. **Compression**: BC7 encoding if requested
  5. **Packing**: Aligned layout according to packing policy

  @param image  Pre-decoded ScratchImage (takes ownership via move)
  @param desc   Import descriptor specifying how to cook the texture
  @param policy Packing policy for the target backend
  @return Cooked payload on success, or TextureImportError on failure

  @see ConvertEquirectangularToCube for HDR panorama → cubemap workflow
*/
OXGN_CNTT_NDAPI auto CookTexture(ScratchImage&& image,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy,
  bool with_content_hashing = true)
  -> Result<CookedTexturePayload, TextureImportError>;

//! Cook a multi-source texture (cube maps and 2D arrays).
/*!
  Assembles multiple source files into a single texture.

  Each source in the TextureSourceSet is decoded and placed into the
  appropriate subresource position (array layer, mip level).

  @param sources Set of source files mapped to subresources
  @param desc    Import descriptor specifying how to cook the texture
  @param policy  Packing policy for the target backend
  @return Cooked payload on success, or TextureImportError on failure

  ### Example: Cube Map from 6 Face Images

  ```cpp
  TextureSourceSet sources;
  sources.AddCubeFace(CubeFace::kPositiveX, LoadFile("sky_px.hdr"), "px");
  sources.AddCubeFace(CubeFace::kNegativeX, LoadFile("sky_nx.hdr"), "nx");
  // ... add remaining faces

  TextureImportDesc desc;
  ApplyPreset(desc, TexturePreset::kHdrEnvironment);
  desc.texture_type = TextureType::kTextureCube;

  auto result = CookTexture(sources, desc, D3D12PackingPolicy::Instance());
  ```

  @note 3D depth-slice assembly is not supported yet.
  @see TextureSourceSet for source assembly helpers
  @see ApplyPreset for easy descriptor configuration
*/
OXGN_CNTT_NDAPI auto CookTexture(const TextureSourceSet& sources,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy,
  bool with_content_hashing = true)
  -> Result<CookedTexturePayload, TextureImportError>;

//===----------------------------------------------------------------------===//
// Internal Pipeline Stages (Exposed for Testing)
//===----------------------------------------------------------------------===//

namespace detail {

  //! Decode source bytes to a working format ScratchImage.
  /*!
    @param source_bytes Raw source image data
    @param desc         Import descriptor with decode options
    @return Decoded ScratchImage or error
  */
  OXGN_CNTT_NDAPI auto DecodeSource(std::span<const std::byte> source_bytes,
    const TextureImportDesc& desc) -> Result<ScratchImage, TextureImportError>;

  //! Convert image to the working format for processing.
  /*!
    Ensures the image is in a format suitable for processing:
    - LDR content: RGBA8UNorm or RGBA32Float
    - HDR content: RGBA32Float

    @param image Decoded image
    @param desc  Import descriptor
    @return Converted image or error
  */
  OXGN_CNTT_NDAPI auto ConvertToWorkingFormat(ScratchImage&& image,
    const TextureImportDesc& desc) -> Result<ScratchImage, TextureImportError>;

  //! Apply content-specific processing (normal maps, HDR, etc.).
  /*!
    @param image Working format image
    @param desc  Import descriptor
    @return Processed image or error
  */
  OXGN_CNTT_NDAPI auto ApplyContentProcessing(ScratchImage&& image,
    const TextureImportDesc& desc) -> Result<ScratchImage, TextureImportError>;

  //! Generate mip chain according to mip policy.
  /*!
    @param image Single-mip image
    @param desc  Import descriptor with mip settings
    @return Image with mip chain or error
  */
  OXGN_CNTT_NDAPI auto GenerateMips(ScratchImage&& image,
    const TextureImportDesc& desc) -> Result<ScratchImage, TextureImportError>;

  //! Convert to output format (including BC7 compression).
  /*!
    @param image Processed image with mips
    @param desc  Import descriptor with output format settings
    @return Output format image or error
  */
  OXGN_CNTT_NDAPI auto ConvertToOutputFormat(ScratchImage&& image,
    const TextureImportDesc& desc) -> Result<ScratchImage, TextureImportError>;

  //! Pack subresource data according to policy.
  /*!
    @param image  Final image in output format
    @param policy Packing policy for alignment
    @return Packed payload bytes
  */
  OXGN_CNTT_NDAPI auto PackSubresources(const ScratchImage& image,
    const ITexturePackingPolicy& policy) -> std::vector<std::byte>;

  //! Compute content hash for deduplication.
  /*!
    Uses the first 8 bytes of SHA-256 over the complete payload for
    content-based deduplication.

    @param payload Payload bytes to hash
    @return 64-bit content hash
  */
  OXGN_CNTT_NDAPI auto ComputeContentHash(
    std::span<const std::byte> payload) noexcept -> uint64_t;

} // namespace detail

} // namespace oxygen::content::import
