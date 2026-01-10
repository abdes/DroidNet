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

#include <Oxygen/Content/api_export.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace oxygen::content::import {

//! Non-owning view into a single 2D surface (one mip of one array layer).
/*!
  `ImageView` provides read-only access to pixel data for a single subresource.
  It is a lightweight view type that does not own the underlying memory.

  ### Usage Pattern

  ```cpp
  ScratchImage scratch = ...;
  ImageView view = scratch.GetImage(0, 0); \/\/ layer 0, mip 0
  // Access pixel data via view.pixels
  ```

  @see ScratchImage
*/
struct ImageView {
  //! Width of the image in pixels.
  uint32_t width = 0;

  //! Height of the image in pixels.
  uint32_t height = 0;

  //! Pixel format of the image data.
  Format format = Format::kUnknown;

  //! Row pitch in bytes (may include padding for alignment).
  uint32_t row_pitch_bytes = 0;

  //! View into the pixel data buffer.
  /*!
    Size equals `row_pitch_bytes * height` for uncompressed formats.
  */
  std::span<const std::byte> pixels;
};

//! Metadata describing a ScratchImage's properties.
/*!
  Contains the complete description of a texture's properties including
  dimensions, format, and subresource counts.
*/
struct ScratchImageMeta {
  //! Type of texture (2D, 3D, Cube, etc.).
  TextureType texture_type = TextureType::kTexture2D;

  //! Base width in pixels (mip 0).
  uint32_t width = 0;

  //! Base height in pixels (mip 0).
  uint32_t height = 0;

  //! Depth for 3D textures (mip 0), otherwise 1.
  uint16_t depth = 1;

  //! Number of array layers (1 for non-arrays, 6 for cubemaps).
  uint16_t array_layers = 1;

  //! Number of mip levels.
  uint16_t mip_levels = 1;

  //! Pixel format.
  Format format = Format::kUnknown;
};

//! Owning container for texture data (all mips, all layers).
/*!
  `ScratchImage` is the in-memory representation of a texture during import
  and cooking. It owns the pixel data and provides access to individual
  subresources via `GetImage()`.

  Inspired by DirectXTex's `ScratchImage`, but adapted for Oxygen's needs.

  ### Key Features

  - **Owns pixel data**: All pixel bytes are stored in a contiguous buffer
  - **Subresource access**: `GetImage(layer, mip)` returns an `ImageView`
  - **Mip chain support**: Stores full mip chains with computed dimensions

  ### Usage Pattern

  ```cpp
  \/\/ Decoders produce a ScratchImage with one mip
  ScratchImage scratch = DecodeToScratchImage(bytes, options);

  \/\/ Access metadata
  const auto& meta = scratch.Meta();

  \/\/ Access specific subresource
  ImageView mip0 = scratch.GetImage(0, 0);
  ```

  ### Subresource Ordering

  Subresources are stored in the order: layer 0 mips 0..N-1, layer 1 mips
  0..N-1, ... Use `ComputeSubresourceIndex(layer, mip)` to compute the linear
  index.

  @see ImageView, ScratchImageMeta
*/
class ScratchImage {
public:
  //! Default constructor creates an empty (invalid) image.
  ScratchImage() = default;

  //! Move constructor.
  ScratchImage(ScratchImage&&) noexcept = default;

  //! Move assignment.
  auto operator=(ScratchImage&&) noexcept -> ScratchImage& = default;

  //! Copy is explicitly disabled to prevent accidental large copies.
  ScratchImage(const ScratchImage&) = delete;

  //! Copy assignment is explicitly disabled.
  auto operator=(const ScratchImage&) -> ScratchImage& = delete;

  //! Destructor.
  ~ScratchImage() = default;

  //=== Static Helpers ===----------------------------------------------------//

  //! Compute the number of mip levels for a texture of given dimensions.
  /*!
    Returns `floor(log2(max(width, height))) + 1` for a full mip chain.

    @param width  Base width in pixels
    @param height Base height in pixels
    @return Number of mip levels in a full chain
  */
  [[nodiscard]] OXGN_CNTT_API static auto ComputeMipCount(
    uint32_t width, uint32_t height) noexcept -> uint32_t;

  //! Compute the linear subresource index.
  /*!
    Subresources are ordered: layer 0 mips 0..N-1, layer 1 mips 0..N-1, ...

    @param array_layer  Array layer index
    @param mip_level    Mip level index
    @param mip_levels   Total number of mip levels
    @return Linear index into subresource array
  */
  [[nodiscard]] static constexpr auto ComputeSubresourceIndex(
    uint16_t array_layer, uint16_t mip_level, uint16_t mip_levels) noexcept
    -> uint32_t
  {
    return static_cast<uint32_t>(array_layer) * mip_levels + mip_level;
  }

  //! Compute mip dimensions for a given level.
  /*!
    Each dimension halves per mip level, clamped to a minimum of 1.

    @param base_dimension Base dimension (width, height, or depth)
    @param mip_level      Mip level index (0 = base)
    @return Dimension at the specified mip level
  */
  [[nodiscard]] static constexpr auto ComputeMipDimension(
    uint32_t base_dimension, uint16_t mip_level) noexcept -> uint32_t
  {
    const auto shifted = base_dimension >> mip_level;
    return shifted > 0 ? shifted : 1;
  }

  //=== Factory Methods ===---------------------------------------------------//

  //! Create and initialize a ScratchImage with the specified metadata.
  /*!
    Allocates storage for all subresources and initializes internal layout.
    Pixel data is zero-initialized.

    @param meta Metadata describing the texture
    @return Initialized ScratchImage, or empty if allocation fails
  */
  [[nodiscard]] OXGN_CNTT_API static auto Create(const ScratchImageMeta& meta)
    -> ScratchImage;

  //! Create a ScratchImage from existing pixel data (single mip, single layer).
  /*!
    Takes ownership of the provided pixel data. Used by decoders to wrap
    decoded image data.

    @param width         Image width in pixels
    @param height        Image height in pixels
    @param format        Pixel format
    @param row_pitch     Row pitch in bytes
    @param pixel_data    Pixel data (moved into the ScratchImage)
    @return Initialized ScratchImage
  */
  [[nodiscard]] OXGN_CNTT_API static auto CreateFromData(uint32_t width,
    uint32_t height, Format format, uint32_t row_pitch,
    std::vector<std::byte> pixel_data) -> ScratchImage;

  //=== Accessors
  //===----------------------------------------------------------//

  //! Get the texture metadata.
  [[nodiscard]] auto Meta() const noexcept -> const ScratchImageMeta&
  {
    return meta_;
  }

  //! Check if the image is valid (has allocated data).
  [[nodiscard]] auto IsValid() const noexcept -> bool
  {
    return !storage_.empty() && meta_.width > 0 && meta_.height > 0;
  }

  //! Get a view to a specific subresource.
  /*!
    @param array_layer Array layer index (0 for non-array textures)
    @param mip_level   Mip level index (0 = highest resolution)
    @return ImageView for the specified subresource
  */
  [[nodiscard]] OXGN_CNTT_API auto GetImage(
    uint16_t array_layer, uint16_t mip_level) const -> ImageView;

  //! Get mutable access to a specific subresource's pixel data.
  /*!
    @param array_layer Array layer index
    @param mip_level   Mip level index
    @return Mutable span of pixel data
  */
  [[nodiscard]] OXGN_CNTT_API auto GetMutablePixels(
    uint16_t array_layer, uint16_t mip_level) -> std::span<std::byte>;

  //! Get the total number of subresources.
  [[nodiscard]] auto GetSubresourceCount() const noexcept -> uint32_t
  {
    if (storage_.empty()) {
      return 0;
    }
    return static_cast<uint32_t>(meta_.array_layers) * meta_.mip_levels;
  }

  //! Get the total size of pixel data in bytes.
  [[nodiscard]] auto GetTotalSizeBytes() const noexcept -> size_t
  {
    return storage_.size();
  }

private:
  //! Internal subresource layout descriptor.
  struct SubresourceInfo {
    uint32_t offset = 0; //!< Offset into storage_
    uint32_t row_pitch = 0; //!< Row pitch in bytes
    uint32_t width = 0; //!< Width at this mip level
    uint32_t height = 0; //!< Height at this mip level
  };

  ScratchImageMeta meta_;
  std::vector<std::byte> storage_;
  std::vector<SubresourceInfo> subresources_;
};

} // namespace oxygen::content::import
