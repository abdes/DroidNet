//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Internal/bc7/Bc7Encoder.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <execution>
#include <mutex>
#include <numeric>

#include <Oxygen/Content/Import/Internal/bc7/bc7enc.h>

namespace oxygen::content::import::bc7 {

namespace {

  //=== Thread-Safe Initialization ===----------------------------------------//

  std::once_flag g_encoder_init_flag;
  std::atomic<bool> g_encoder_initialized { false };

  void DoInitialize()
  {
    bc7enc_compress_block_init();
    g_encoder_initialized.store(true, std::memory_order_release);
  }

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

  void ExtractBlock(const ImageView& source, uint32_t block_x, uint32_t block_y,
    std::array<std::byte, 64>& block_pixels)
  {
    const auto* src_data
      = reinterpret_cast<const uint8_t*>(source.pixels.data());
    auto* dst_data = reinterpret_cast<uint8_t*>(block_pixels.data());

    const uint32_t src_stride = source.row_pitch_bytes;
    const uint32_t start_x = block_x * kBc7BlockDimension;
    const uint32_t start_y = block_y * kBc7BlockDimension;

    for (uint32_t local_y = 0; local_y < kBc7BlockDimension; ++local_y) {
      for (uint32_t local_x = 0; local_x < kBc7BlockDimension; ++local_x) {
        // Clamp to edge for border replication
        const uint32_t src_x = (std::min)(start_x + local_x, source.width - 1);
        const uint32_t src_y = (std::min)(start_y + local_y, source.height - 1);

        const size_t src_offset
          = static_cast<size_t>(src_y) * src_stride + src_x * 4;
        const size_t dst_offset = (local_y * kBc7BlockDimension + local_x) * 4;

        dst_data[dst_offset + 0] = src_data[src_offset + 0];
        dst_data[dst_offset + 1] = src_data[src_offset + 1];
        dst_data[dst_offset + 2] = src_data[src_offset + 2];
        dst_data[dst_offset + 3] = src_data[src_offset + 3];
      }
    }
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

void InitializeEncoder() { std::call_once(g_encoder_init_flag, DoInitialize); }

//=== Single Block Encoding ===-----------------------------------------------//

auto EncodeBlock(const std::span<const std::byte, 64> pixels_rgba8,
  std::span<std::byte, kBc7BlockSizeBytes> output,
  const Bc7EncoderParams& params) -> bool
{
  // Ensure encoder is initialized
  if (!g_encoder_initialized.load(std::memory_order_acquire)) {
    InitializeEncoder();
  }

  bc7enc_compress_block_params bc7_params;
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

  // Ensure encoder is initialized
  if (!g_encoder_initialized.load(std::memory_order_acquire)) {
    InitializeEncoder();
  }

  // Calculate block dimensions
  const uint32_t blocks_x = ComputeBlockCount(source.width);
  const uint32_t blocks_y = ComputeBlockCount(source.height);
  const size_t output_size
    = static_cast<size_t>(blocks_x) * blocks_y * kBc7BlockSizeBytes;

  // Allocate output
  std::vector<std::byte> compressed_data(output_size);

  // Configure bc7enc parameters
  bc7enc_compress_block_params bc7_params;
  ConfigureBlockParams(bc7_params, params);

  // Encode each block (parallelized across block rows).
  std::vector<uint32_t> block_rows(blocks_y);
  std::iota(block_rows.begin(), block_rows.end(), 0U);
  std::for_each(std::execution::par, block_rows.begin(), block_rows.end(),
    [&](const uint32_t by) {
      std::array<std::byte, 64> block_pixels {};

      for (uint32_t bx = 0; bx < blocks_x; ++bx) {
        // Extract 4x4 block with border replication
        ExtractBlock(source, bx, by, block_pixels);

        // Compute output offset
        const size_t block_index = static_cast<size_t>(by) * blocks_x + bx;
        const size_t output_offset = block_index * kBc7BlockSizeBytes;

        // Encode block
        bc7enc_compress_block(compressed_data.data() + output_offset,
          block_pixels.data(), &bc7_params);
      }
    });

  // Create output ScratchImage with BC7 format
  return ScratchImage::CreateFromData(source.width, source.height,
    Format::kBC7UNorm, ComputeBc7RowPitch(source.width),
    std::move(compressed_data));
}

//=== Full Texture Encoding ===-----------------------------------------------//

auto EncodeTexture(const ScratchImage& source, const Bc7EncoderParams& params)
  -> ScratchImage
{
  if (!source.IsValid()) {
    return {};
  }

  const auto& src_meta = source.Meta();

  // Validate input format
  if (src_meta.format != Format::kRGBA8UNorm) {
    return {}; // Only RGBA8 supported
  }

  // Ensure encoder is initialized
  if (!g_encoder_initialized.load(std::memory_order_acquire)) {
    InitializeEncoder();
  }

  // Create output metadata
  ScratchImageMeta dst_meta = src_meta;
  dst_meta.format = Format::kBC7UNorm;

  // Calculate total output size
  size_t total_size = 0;
  for (uint16_t layer = 0; layer < src_meta.array_layers; ++layer) {
    for (uint16_t mip = 0; mip < src_meta.mip_levels; ++mip) {
      const uint32_t mip_width
        = ScratchImage::ComputeMipDimension(src_meta.width, mip);
      const uint32_t mip_height
        = ScratchImage::ComputeMipDimension(src_meta.height, mip);
      total_size += ComputeBc7SurfaceSize(mip_width, mip_height);
    }
  }

  // Allocate output storage
  std::vector<std::byte> output_storage(total_size);

  // Configure bc7enc parameters
  bc7enc_compress_block_params bc7_params;
  ConfigureBlockParams(bc7_params, params);

  // Encode each subresource
  size_t subresource_base_offset = 0;

  for (uint16_t layer = 0; layer < src_meta.array_layers; ++layer) {
    for (uint16_t mip = 0; mip < src_meta.mip_levels; ++mip) {
      const auto src_view = source.GetImage(layer, mip);

      const uint32_t blocks_x = ComputeBlockCount(src_view.width);
      const uint32_t blocks_y = ComputeBlockCount(src_view.height);

      const size_t surface_size
        = ComputeBc7SurfaceSize(src_view.width, src_view.height);
      const size_t surface_base = subresource_base_offset;

      std::vector<uint32_t> block_rows(blocks_y);
      std::iota(block_rows.begin(), block_rows.end(), 0U);
      std::for_each(std::execution::par, block_rows.begin(), block_rows.end(),
        [&](const uint32_t by) {
          std::array<std::byte, 64> block_pixels {};

          for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            // Extract block
            ExtractBlock(src_view, bx, by, block_pixels);

            // Encode block
            const size_t block_index = static_cast<size_t>(by) * blocks_x + bx;
            const size_t output_offset
              = surface_base + block_index * kBc7BlockSizeBytes;
            bc7enc_compress_block(output_storage.data() + output_offset,
              block_pixels.data(), &bc7_params);
          }
        });

      subresource_base_offset += surface_size;
    }
  }

  // Create output ScratchImage
  // For multi-mip textures, we need to use ScratchImage::Create with metadata
  ScratchImage result = ScratchImage::Create(dst_meta);
  if (!result.IsValid()) {
    return {};
  }

  // Copy encoded data to the result
  size_t output_offset = 0;
  for (uint16_t layer = 0; layer < dst_meta.array_layers; ++layer) {
    for (uint16_t mip = 0; mip < dst_meta.mip_levels; ++mip) {
      auto dst_pixels = result.GetMutablePixels(layer, mip);
      const size_t surface_size = dst_pixels.size();

      std::memcpy(
        dst_pixels.data(), output_storage.data() + output_offset, surface_size);

      output_offset += surface_size;
    }
  }

  return result;
}

auto EncodeTexture(const ScratchImage& source, const Bc7EncoderParams& params,
  const std::stop_token stop_token) -> ScratchImage
{
  if (stop_token.stop_requested()) {
    return {};
  }

  if (!source.IsValid()) {
    return {};
  }

  const auto& src_meta = source.Meta();

  // Validate input format
  if (src_meta.format != Format::kRGBA8UNorm) {
    return {}; // Only RGBA8 supported
  }

  // Ensure encoder is initialized
  if (!g_encoder_initialized.load(std::memory_order_acquire)) {
    InitializeEncoder();
  }

  // Create output metadata
  ScratchImageMeta dst_meta = src_meta;
  dst_meta.format = Format::kBC7UNorm;

  // Calculate total output size
  size_t total_size = 0;
  for (uint16_t layer = 0; layer < src_meta.array_layers; ++layer) {
    for (uint16_t mip = 0; mip < src_meta.mip_levels; ++mip) {
      const uint32_t mip_width
        = ScratchImage::ComputeMipDimension(src_meta.width, mip);
      const uint32_t mip_height
        = ScratchImage::ComputeMipDimension(src_meta.height, mip);
      total_size += ComputeBc7SurfaceSize(mip_width, mip_height);
    }
  }

  // Allocate output storage
  std::vector<std::byte> output_storage(total_size);

  // Configure bc7enc parameters
  bc7enc_compress_block_params bc7_params;
  ConfigureBlockParams(bc7_params, params);

  // Encode each subresource
  size_t subresource_base_offset = 0;

  for (uint16_t layer = 0; layer < src_meta.array_layers; ++layer) {
    for (uint16_t mip = 0; mip < src_meta.mip_levels; ++mip) {
      if (stop_token.stop_requested()) {
        return {};
      }

      const auto src_view = source.GetImage(layer, mip);

      const uint32_t blocks_x = ComputeBlockCount(src_view.width);
      const uint32_t blocks_y = ComputeBlockCount(src_view.height);

      const size_t surface_size
        = ComputeBc7SurfaceSize(src_view.width, src_view.height);
      const size_t surface_base = subresource_base_offset;

      std::vector<uint32_t> block_rows(blocks_y);
      std::iota(block_rows.begin(), block_rows.end(), 0U);
      std::for_each(std::execution::par, block_rows.begin(), block_rows.end(),
        [&](const uint32_t by) {
          if (stop_token.stop_requested()) {
            return;
          }

          std::array<std::byte, 64> block_pixels {};

          for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            if (stop_token.stop_requested()) {
              return;
            }

            // Extract block
            ExtractBlock(src_view, bx, by, block_pixels);

            // Encode block
            const size_t block_index = static_cast<size_t>(by) * blocks_x + bx;
            const size_t output_offset
              = surface_base + block_index * kBc7BlockSizeBytes;
            bc7enc_compress_block(output_storage.data() + output_offset,
              block_pixels.data(), &bc7_params);
          }
        });

      subresource_base_offset += surface_size;
    }
  }

  if (stop_token.stop_requested()) {
    return {};
  }

  // Create output ScratchImage
  ScratchImage result = ScratchImage::Create(dst_meta);
  if (!result.IsValid()) {
    return {};
  }

  // Copy encoded data to the result
  size_t output_offset = 0;
  for (uint16_t layer = 0; layer < dst_meta.array_layers; ++layer) {
    for (uint16_t mip = 0; mip < dst_meta.mip_levels; ++mip) {
      auto dst_pixels = result.GetMutablePixels(layer, mip);
      const size_t surface_size = dst_pixels.size();

      std::memcpy(
        dst_pixels.data(), output_storage.data() + output_offset, surface_size);

      output_offset += surface_size;
    }
  }

  return result;
}

auto EncodeTexture(const ScratchImage& source, const Bc7Quality quality)
  -> ScratchImage
{
  if (quality == Bc7Quality::kNone) {
    // No compression requested - return a copy? Or empty?
    // For now, return empty to indicate no BC7 encoding
    return {};
  }

  return EncodeTexture(source, Bc7EncoderParams::FromQuality(quality));
}

auto EncodeTexture(const ScratchImage& source, const Bc7Quality quality,
  const std::stop_token stop_token) -> ScratchImage
{
  if (quality == Bc7Quality::kNone) {
    return {};
  }

  return EncodeTexture(
    source, Bc7EncoderParams::FromQuality(quality), stop_token);
}

} // namespace oxygen::content::import::bc7
