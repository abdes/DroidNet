//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <stop_token>

#include <Oxygen/Content/Import/ScratchImage.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import::bc7 {

//! Size of a BC7 compressed block in bytes.
constexpr uint32_t kBc7BlockSizeBytes = 16;

//! Dimensions of a BC7 block in pixels.
constexpr uint32_t kBc7BlockDimension = 4;

//! Parameters for BC7 block encoding.
/*!
  Provides fine-grained control over BC7 compression behavior.
  Use the static factory methods for common configurations.
*/
struct Bc7EncoderParams {
  //! Maximum partition count (0-64). Higher = slower but better quality.
  uint32_t max_partitions = 64;

  //! Uber level (0-4). Higher = slower but better quality.
  uint32_t uber_level = 0;

  //! Whether to use perceptual (YCbCr) weighting.
  bool perceptual = true;

  //! Whether to enable least-squares optimization.
  bool try_least_squares = true;

  //! Whether to use partition estimation filterbank for modes 1/7.
  bool use_partition_filterbank = true;

  //=== Factory Methods ===---------------------------------------------------//

  //! Create parameters for fast encoding.
  OXGN_CNTT_NDAPI static auto Fast() noexcept -> Bc7EncoderParams;

  //! Create parameters for default (balanced) encoding.
  OXGN_CNTT_NDAPI static auto Default() noexcept -> Bc7EncoderParams;

  //! Create parameters for high quality encoding.
  OXGN_CNTT_NDAPI static auto High() noexcept -> Bc7EncoderParams;

  //! Create parameters from a Bc7Quality tier.
  OXGN_CNTT_NDAPI static auto FromQuality(Bc7Quality quality) noexcept
    -> Bc7EncoderParams;
};

//! Initialize the BC7 encoder.
/*!
  Must be called at least once before any encoding operations.
  Thread-safe; subsequent calls are no-ops.
*/
OXGN_CNTT_API void InitializeEncoder();

//! Encode a single 4x4 block to BC7.
/*!
  Input must be exactly 16 RGBA8 pixels (64 bytes) in row-major order.
  Output is a 16-byte BC7 compressed block.

  @param pixels_rgba8 Input 4x4 block (16 pixels × 4 bytes = 64 bytes)
  @param output       Output BC7 block (16 bytes)
  @param params       Encoding parameters
  @return true if the block contained alpha < 255, false otherwise
*/
OXGN_CNTT_API auto EncodeBlock(std::span<const std::byte, 64> pixels_rgba8,
  std::span<std::byte, kBc7BlockSizeBytes> output,
  const Bc7EncoderParams& params) -> bool;

//! Encode a single surface (one mip level) to BC7.
/*!
  Encodes all blocks in the surface. Edge blocks are handled by replicating
  border pixels when dimensions are not multiples of 4.

  @param source  Source image view (RGBA8 format expected)
  @param params  Encoding parameters
  @return BC7-compressed image, or invalid ScratchImage on error
*/
OXGN_CNTT_NDAPI auto EncodeSurface(
  const ImageView& source, const Bc7EncoderParams& params) -> ScratchImage;

//! Encode a full texture (all mip levels, all array layers) to BC7.
/*!
  Encodes all subresources of the input texture. The input should be RGBA8
  format. Output will be BC7 format with the same dimensions and subresource
  layout.

  @param source  Source texture (RGBA8 format expected)
  @param params  Encoding parameters
  @return BC7-compressed texture, or invalid ScratchImage on error
*/
OXGN_CNTT_NDAPI auto EncodeTexture(
  const ScratchImage& source, const Bc7EncoderParams& params) -> ScratchImage;

//! Encode a full texture with cooperative cancellation.
/*! @see EncodeTexture(const ScratchImage&, const Bc7EncoderParams&) */
OXGN_CNTT_NDAPI auto EncodeTexture(const ScratchImage& source,
  const Bc7EncoderParams& params, std::stop_token stop_token) -> ScratchImage;

//! Encode a full texture using a Bc7Quality preset.
/*!
  Convenience overload that maps Bc7Quality to Bc7EncoderParams.

  @param source  Source texture (RGBA8 format expected)
  @param quality Quality tier
  @return BC7-compressed texture, or invalid ScratchImage on error
*/
OXGN_CNTT_NDAPI auto EncodeTexture(
  const ScratchImage& source, Bc7Quality quality) -> ScratchImage;

//! Encode a full texture using a Bc7Quality preset with cancellation.
/*! @see EncodeTexture(const ScratchImage&, Bc7Quality) */
OXGN_CNTT_NDAPI auto EncodeTexture(const ScratchImage& source,
  Bc7Quality quality, std::stop_token stop_token) -> ScratchImage;

//! Compute the number of BC7 blocks in one dimension.
/*!
  @param dimension Dimension in pixels
  @return Number of 4x4 blocks (rounded up)
*/
[[nodiscard]] constexpr auto ComputeBlockCount(uint32_t dimension) noexcept
  -> uint32_t
{
  return (dimension + kBc7BlockDimension - 1) / kBc7BlockDimension;
}

//! Compute the row pitch for a BC7-compressed surface.
/*!
  @param width Width in pixels
  @return Row pitch in bytes (blocks_x * 16)
*/
[[nodiscard]] constexpr auto ComputeBc7RowPitch(uint32_t width) noexcept
  -> uint32_t
{
  return ComputeBlockCount(width) * kBc7BlockSizeBytes;
}

//! Compute the total size of a BC7-compressed surface.
/*!
  @param width  Width in pixels
  @param height Height in pixels
  @return Total size in bytes
*/
[[nodiscard]] constexpr auto ComputeBc7SurfaceSize(
  uint32_t width, uint32_t height) noexcept -> size_t
{
  const auto blocks_x = ComputeBlockCount(width);
  const auto blocks_y = ComputeBlockCount(height);
  return static_cast<size_t>(blocks_x) * blocks_y * kBc7BlockSizeBytes;
}

} // namespace oxygen::content::import::bc7
