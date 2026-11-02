//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <execution>
#include <numeric>
#include <span>
#include <stop_token>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/bc7/Bc7Encoder.h>
#include <Oxygen/Cooker/Import/Internal/bc7/bc7enc.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace oxygen::content::import::bc7 {

namespace {

  //=== Parameter Conversion ===----------------------------------------------//

  void ConfigureBlockParams(
    bc7enc_compress_block_params& bc7_params, const Bc7EncoderParams& params)
  {
    bc7enc_compress_block_params_init(&bc7_params);

    bc7_params.m_max_partitions = params.max_partitions;
    bc7_params.m_uber_level = params.uber_level;
    bc7_params.m_try_least_squares = params.try_least_squares;
    bc7_params.m_mode17_partition_estimation_filterbank
      = params.use_partition_filterbank;

    if (params.perceptual) {
      bc7enc_compress_block_params_init_perceptual_weights(&bc7_params);
    } else {
      bc7enc_compress_block_params_init_linear_weights(&bc7_params);
    }
  }

  //=== Block Extraction ===--------------------------------------------------//

  //! Copies the 4x4 RGBA8 texels of block (`block_x`, `block_y`) of `source`,
  //! row-major, replicating its last row and column past its edges.
  void ExtractBlock(const ImageView& source, uint32_t block_x, uint32_t block_y,
    std::array<std::byte, 64>& block_pixels)
  {
    const auto texel_bytes = graphics::detail::RowBytes(source.format, 1U);
    const uint32_t start_x = block_x * kBc7BlockDimension;
    const uint32_t start_y = block_y * kBc7BlockDimension;

    for (uint32_t local_y = 0; local_y < kBc7BlockDimension; ++local_y) {
      for (uint32_t local_x = 0; local_x < kBc7BlockDimension; ++local_x) {
        // Clamp to edge for border replication
        const uint32_t src_x = (std::min)(start_x + local_x, source.width - 1);
        const uint32_t src_y = (std::min)(start_y + local_y, source.height - 1);

        const auto src_offset = (uint64_t { src_y } * source.row_pitch_bytes)
          + graphics::detail::RowBytes(source.format, src_x);
        const auto dst_offset
          = uint64_t { (local_y * kBc7BlockDimension) + local_x } * texel_bytes;
        std::ranges::copy(source.pixels.subspan(src_offset, texel_bytes),
          std::span(block_pixels).subspan(dst_offset, texel_bytes).begin());
      }
    }
  }

  //! Encodes `source` into `output`, a BC7 subresource of its extent, rows
  //! of blocks in parallel; leaves the rest unwritten once `stop_token` is
  //! requested.
  void EncodeBlocks(const ImageView& source, const std::span<std::byte> output,
    const bc7enc_compress_block_params& bc7_params,
    const std::stop_token& stop_token)
  {
    const auto row_pitch
      = graphics::detail::RowBytes(Format::kBC7UNorm, source.width);
    const auto blocks_x = row_pitch / kBc7BlockSizeBytes;

    std::vector<uint32_t> block_rows(
      graphics::detail::BlockRows(Format::kBC7UNorm, source.height));
    std::ranges::iota(block_rows, 0U);
    std::for_each(std::execution::par, block_rows.begin(), block_rows.end(),
      [&](const uint32_t by) -> void {
        std::array<std::byte, 64> block_pixels {};
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
          if (stop_token.stop_requested()) {
            return;
          }
          ExtractBlock(source, bx, by, block_pixels);
          const auto block = output.subspan(
            (by * row_pitch) + (uint64_t { bx } * kBc7BlockSizeBytes),
            kBc7BlockSizeBytes);
          bc7enc_compress_block(block.data(), block_pixels.data(), &bc7_params);
        }
      });
  }

} // namespace

//=== Bc7EncoderParams Factory Methods ===------------------------------------//

auto Bc7EncoderParams::Fast() noexcept -> Bc7EncoderParams
{
  return Bc7EncoderParams {
    .max_partitions = 16,
    .uber_level = 0,
    .perceptual = true,
    .try_least_squares = false,
    .use_partition_filterbank = true,
  };
}

auto Bc7EncoderParams::Default() noexcept -> Bc7EncoderParams
{
  return Bc7EncoderParams {
    .max_partitions = 64,
    .uber_level = 1,
    .perceptual = true,
    .try_least_squares = true,
    .use_partition_filterbank = true,
  };
}

auto Bc7EncoderParams::High() noexcept -> Bc7EncoderParams
{
  return Bc7EncoderParams {
    .max_partitions = 64,
    .uber_level = 4,
    .perceptual = true,
    .try_least_squares = true,
    .use_partition_filterbank = false,
  };
}

auto Bc7EncoderParams::FromQuality(const Bc7Quality quality) noexcept
  -> Bc7EncoderParams
{
  switch (quality) {
  case Bc7Quality::kFast:
    return Fast();
  case Bc7Quality::kDefault:
    return Default();
  case Bc7Quality::kHigh:
    return High();
  case Bc7Quality::kNone:
  default:
    return Default();
  }
}

//=== Encoder Initialization ===----------------------------------------------//

void InitializeEncoder()
{
  // A function-local static is initialized once, thread-safely.
  [[maybe_unused]] static const auto initialized = [] -> bool {
    bc7enc_compress_block_init();
    return true;
  }();
}

//=== Single Block Encoding ===-----------------------------------------------//

auto EncodeBlock(const std::span<const std::byte, 64> pixels_rgba8,
  std::span<std::byte, kBc7BlockSizeBytes> output,
  const Bc7EncoderParams& params) -> bool
{
  InitializeEncoder();

  bc7enc_compress_block_params bc7_params {};
  ConfigureBlockParams(bc7_params, params);

  const bool has_alpha
    = bc7enc_compress_block(output.data(), pixels_rgba8.data(), &bc7_params);

  return has_alpha;
}

//=== Surface Encoding ===----------------------------------------------------//

auto EncodeSurface(const ImageView& source, const Bc7EncoderParams& params)
  -> ScratchImage
{
  // Validate input format
  if (source.format != Format::kRGBA8UNorm) {
    return {}; // Invalid format
  }

  InitializeEncoder();

  ScratchImage result = ScratchImage::Create(ScratchImageMeta {
    .texture_type = TextureType::kTexture2D,
    .width = source.width,
    .height = source.height,
    .format = Format::kBC7UNorm,
  });
  if (!result.IsValid()) {
    return {};
  }

  bc7enc_compress_block_params bc7_params {};
  ConfigureBlockParams(bc7_params, params);
  EncodeBlocks(source, result.GetMutablePixels(0, 0), bc7_params, {});
  return result;
}

//=== Full Texture Encoding ===-----------------------------------------------//

auto EncodeTexture(const ScratchImage& source, const Bc7EncoderParams& params)
  -> ScratchImage
{
  return EncodeTexture(source, params, std::stop_token {});
}

auto EncodeTexture(const ScratchImage& source, const Bc7EncoderParams& params,
  const std::stop_token& stop_token) -> ScratchImage
{
  if (stop_token.stop_requested() || !source.IsValid()) {
    return {};
  }

  const auto& src_meta = source.Meta();

  // Validate input format
  if (src_meta.format != Format::kRGBA8UNorm) {
    return {}; // Only RGBA8 supported
  }

  InitializeEncoder();

  // The output has the source's subresources, in BC7.
  ScratchImageMeta dst_meta = src_meta;
  dst_meta.format = Format::kBC7UNorm;
  ScratchImage result = ScratchImage::Create(dst_meta);
  if (!result.IsValid()) {
    return {};
  }

  bc7enc_compress_block_params bc7_params {};
  ConfigureBlockParams(bc7_params, params);

  for (uint16_t layer = 0; layer < src_meta.array_layers; ++layer) {
    for (uint16_t mip = 0; mip < src_meta.mip_levels; ++mip) {
      EncodeBlocks(source.GetImage(layer, mip),
        result.GetMutablePixels(layer, mip), bc7_params, stop_token);
    }
  }

  if (stop_token.stop_requested()) {
    return {};
  }
  return result;
}

auto EncodeTexture(const ScratchImage& source, const Bc7Quality quality)
  -> ScratchImage
{
  return EncodeTexture(source, quality, std::stop_token {});
}

auto EncodeTexture(const ScratchImage& source, const Bc7Quality quality,
  const std::stop_token& stop_token) -> ScratchImage
{
  // No compression requested: there is nothing to encode.
  if (quality == Bc7Quality::kNone) {
    return {};
  }

  return EncodeTexture(
    source, Bc7EncoderParams::FromQuality(quality), stop_token);
}

} // namespace oxygen::content::import::bc7
