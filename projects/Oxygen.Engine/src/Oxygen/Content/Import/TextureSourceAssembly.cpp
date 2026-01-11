//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/TextureSourceAssembly.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace oxygen::content::import {

//=== CubeFace to_string
//===-------------------------------------------------===//

auto to_string(const CubeFace face) -> const char*
{
  switch (face) {
  case CubeFace::kPositiveX:
    return "PositiveX";
  case CubeFace::kNegativeX:
    return "NegativeX";
  case CubeFace::kPositiveY:
    return "PositiveY";
  case CubeFace::kNegativeY:
    return "NegativeY";
  case CubeFace::kPositiveZ:
    return "PositiveZ";
  case CubeFace::kNegativeZ:
    return "NegativeZ";
  }
  return "Unknown";
}

//=== TextureSourceSet Implementation
//===-----------------------------------===//

void TextureSourceSet::Add(TextureSource source)
{
  sources_.push_back(std::move(source));
}

void TextureSourceSet::AddArrayLayer(const uint16_t array_layer,
  std::vector<std::byte> bytes, std::string source_id)
{
  sources_.push_back(TextureSource{
    .bytes = std::move(bytes),
    .subresource = SubresourceId{
      .array_layer = array_layer,
      .mip_level = 0,
      .depth_slice = 0,
    },
    .source_id = std::move(source_id),
  });
}

void TextureSourceSet::AddCubeFace(
  const CubeFace face, std::vector<std::byte> bytes, std::string source_id)
{
  const auto face_index = static_cast<uint16_t>(face);
  sources_.push_back(TextureSource{
    .bytes = std::move(bytes),
    .subresource = SubresourceId{
      .array_layer = face_index,
      .mip_level = 0,
      .depth_slice = 0,
    },
    .source_id = std::move(source_id),
  });
}

void TextureSourceSet::AddDepthSlice(const uint16_t slice_index,
  std::vector<std::byte> bytes, std::string source_id)
{
  sources_.push_back(TextureSource{
    .bytes = std::move(bytes),
    .subresource = SubresourceId{
      .array_layer = 0,
      .mip_level = 0,
      .depth_slice = slice_index,
    },
    .source_id = std::move(source_id),
  });
}

void TextureSourceSet::AddMipLevel(const uint16_t array_layer,
  const uint16_t mip_level, std::vector<std::byte> bytes, std::string source_id)
{
  sources_.push_back(TextureSource{
    .bytes = std::move(bytes),
    .subresource = SubresourceId{
      .array_layer = array_layer,
      .mip_level = mip_level,
      .depth_slice = 0,
    },
    .source_id = std::move(source_id),
  });
}

void TextureSourceSet::Clear() noexcept { sources_.clear(); }

auto TextureSourceSet::GetSource(const size_t index) const
  -> const TextureSource&
{
  if (index >= sources_.size()) {
    throw std::out_of_range("TextureSourceSet index out of range");
  }
  return sources_[index];
}

//=== Cube Map Assembly Helpers
//===------------------------------------------===//

auto ComputeCubeDirection(const CubeFace face, const float u,
  const float v) noexcept -> CubeFaceDirection
{
  // Map [0,1] UV coordinates to [-1,+1] face coordinates
  const float s = 2.0F * u - 1.0F; // -1 (left) to +1 (right)
  const float t = 2.0F * v - 1.0F; // -1 (bottom) to +1 (top)

  const auto& basis = GetCubeFaceBasis(face);

  // Compute direction: center + s * right + t * up
  const float x = basis.center.x + s * basis.right.x + t * basis.up.x;
  const float y = basis.center.y + s * basis.right.y + t * basis.up.y;
  const float z = basis.center.z + s * basis.right.z + t * basis.up.z;

  // Normalize the direction
  const float length = std::sqrt(x * x + y * y + z * z);
  return CubeFaceDirection { x / length, y / length, z / length };
}

//! Compute direction for D3D Y-up cubemap convention.
auto ComputeCubeDirectionD3D(const CubeFace face, const float u,
  const float v) noexcept -> CubeFaceDirection
{
  // Map [0,1] UV coordinates to [-1,+1] face coordinates
  // In texture space: u=0 is left, u=1 is right (same as math)
  // In texture space: v=0 is TOP, v=1 is BOTTOM (opposite of math)
  const float s = 2.0F * u - 1.0F; // -1 (left) to +1 (right)
  const float t = 1.0F - 2.0F * v; // +1 at v=0 (top), -1 at v=1 (bottom)

  const auto& basis = kGpuCubeFaceBases[static_cast<size_t>(face)];

  // Compute direction: center + s * right + t * up
  const float x = basis.center.x + s * basis.right.x + t * basis.up.x;
  const float y = basis.center.y + s * basis.right.y + t * basis.up.y;
  const float z = basis.center.z + s * basis.right.z + t * basis.up.z;

  // Normalize the direction
  const float length = std::sqrt(x * x + y * y + z * z);
  return CubeFaceDirection { x / length, y / length, z / length };
}

auto AssembleCubeFromFaces(std::span<const ScratchImage, kCubeFaceCount> faces)
  -> oxygen::Result<ScratchImage, TextureImportError>
{
  // Validate all faces are valid
  for (size_t i = 0; i < kCubeFaceCount; ++i) {
    if (!faces[i].IsValid()) {
      return oxygen::Err(TextureImportError::kInvalidDimensions);
    }
  }

  // Get reference dimensions and format from first face
  const auto& ref = faces[0];
  const auto& ref_meta = ref.Meta();
  const uint32_t face_width = ref_meta.width;
  const uint32_t face_height = ref_meta.height;
  const auto format = ref_meta.format;

  // Cube faces must be square
  if (face_width != face_height) {
    return oxygen::Err(TextureImportError::kInvalidDimensions);
  }

  // Validate all faces have matching dimensions and format
  for (size_t i = 1; i < kCubeFaceCount; ++i) {
    const auto& face_meta = faces[i].Meta();
    if (face_meta.width != face_width || face_meta.height != face_height) {
      return oxygen::Err(TextureImportError::kDimensionMismatch);
    }
    if (face_meta.format != format) {
      return oxygen::Err(TextureImportError::kDimensionMismatch);
    }
    // Each face must have only 1 mip level (mips generated later)
    if (face_meta.mip_levels != 1) {
      return oxygen::Err(TextureImportError::kInvalidMipPolicy);
    }
  }

  // Create cube map metadata
  ScratchImageMeta meta {
    .texture_type = TextureType::kTextureCube,
    .width = face_width,
    .height = face_height,
    .depth = 1,
    .array_layers = kCubeFaceCount,
    .mip_levels = 1,
    .format = format,
  };

  ScratchImage cube = ScratchImage::Create(meta);
  if (!cube.IsValid()) {
    return oxygen::Err(TextureImportError::kOutOfMemory);
  }

  // Copy each face into the cube map
  for (size_t i = 0; i < kCubeFaceCount; ++i) {
    const auto src_image = faces[i].GetImage(0, 0);
    const auto dst_pixels = cube.GetMutablePixels(static_cast<uint16_t>(i), 0);

    if (src_image.pixels.size() != dst_pixels.size()) {
      return oxygen::Err(TextureImportError::kDimensionMismatch);
    }

    std::copy(
      src_image.pixels.begin(), src_image.pixels.end(), dst_pixels.data());
  }

  return oxygen::Ok(std::move(cube));
}

//=== Equirectangular to Cube Conversion
//===---------------------------------===//

namespace {

  //! Bilinear sample from RGBA32F image.
  /*!
    Samples using bilinear interpolation at the given UV coordinates.
    Handles wrapping horizontally and clamping vertically.

    @param pixels  Raw pixel data (RGBA32F, 4 floats per pixel)
    @param width   Image width
    @param height  Image height
    @param u       Horizontal coordinate (0-1, wraps)
    @param v       Vertical coordinate (0-1, clamps)
    @return Interpolated RGBA color
  */
  auto SampleBilinear(std::span<const std::byte> pixels, const uint32_t width,
    const uint32_t height, const float u, const float v) -> std::array<float, 4>
  {
    // Map to pixel coordinates
    const float px = u * static_cast<float>(width) - 0.5F;
    const float py = v * static_cast<float>(height) - 0.5F;

    // Integer coordinates (with horizontal wrap, vertical clamp)
    auto x0 = static_cast<int32_t>(std::floor(px));
    auto y0 = static_cast<int32_t>(std::floor(py));
    int32_t x1 = x0 + 1;
    int32_t y1 = y0 + 1;

    // Wrap horizontal
    x0 = ((x0 % static_cast<int32_t>(width)) + static_cast<int32_t>(width))
      % static_cast<int32_t>(width);
    x1 = ((x1 % static_cast<int32_t>(width)) + static_cast<int32_t>(width))
      % static_cast<int32_t>(width);

    // Clamp vertical
    y0 = std::clamp(y0, 0, static_cast<int32_t>(height) - 1);
    y1 = std::clamp(y1, 0, static_cast<int32_t>(height) - 1);

    // Fractional parts
    const float fx = px - std::floor(px);
    const float fy = py - std::floor(py);

    // Sample 4 pixels (RGBA32F = 16 bytes per pixel)
    const auto* data = reinterpret_cast<const float*>(pixels.data());
    const auto stride = width * 4U; // floats per row

    auto sample
      = [&](const int32_t x, const int32_t y) -> std::array<float, 4> {
      const size_t idx
        = static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4U;
      return { data[idx], data[idx + 1], data[idx + 2], data[idx + 3] };
    };

    const auto p00 = sample(x0, y0);
    const auto p10 = sample(x1, y0);
    const auto p01 = sample(x0, y1);
    const auto p11 = sample(x1, y1);

    // Bilinear interpolation
    std::array<float, 4> result {};
    for (size_t i = 0; i < 4; ++i) {
      const float top = p00[i] * (1.0F - fx) + p10[i] * fx;
      const float bottom = p01[i] * (1.0F - fx) + p11[i] * fx;
      result[i] = top * (1.0F - fy) + bottom * fy;
    }

    return result;
  }

  //! Cubic interpolation helper (Catmull-Rom spline).
  auto CubicWeight(const float t) -> float
  {
    const float at = std::fabs(t);
    if (at <= 1.0F) {
      return ((1.5F * at - 2.5F) * at) * at + 1.0F;
    }
    if (at < 2.0F) {
      return ((-0.5F * at + 2.5F) * at - 4.0F) * at + 2.0F;
    }
    return 0.0F;
  }

  //! Bicubic sample from RGBA32F image.
  /*!
    Samples using bicubic (Catmull-Rom) interpolation at the given UV
    coordinates. Handles wrapping horizontally and clamping vertically.

    @param pixels  Raw pixel data (RGBA32F, 4 floats per pixel)
    @param width   Image width
    @param height  Image height
    @param u       Horizontal coordinate (0-1, wraps)
    @param v       Vertical coordinate (0-1, clamps)
    @return Interpolated RGBA color
  */
  auto SampleBicubic(std::span<const std::byte> pixels, const uint32_t width,
    const uint32_t height, const float u, const float v) -> std::array<float, 4>
  {
    // Map to pixel coordinates
    const float px = u * static_cast<float>(width) - 0.5F;
    const float py = v * static_cast<float>(height) - 0.5F;

    const auto x0 = static_cast<int32_t>(std::floor(px));
    const auto y0 = static_cast<int32_t>(std::floor(py));
    const float fx = px - static_cast<float>(x0);
    const float fy = py - static_cast<float>(y0);

    const auto* data = reinterpret_cast<const float*>(pixels.data());
    const auto stride = width * 4U;

    auto sample = [&](int32_t x, int32_t y) -> std::array<float, 4> {
      // Wrap horizontal
      x = ((x % static_cast<int32_t>(width)) + static_cast<int32_t>(width))
        % static_cast<int32_t>(width);
      // Clamp vertical
      y = std::clamp(y, 0, static_cast<int32_t>(height) - 1);
      const size_t idx
        = static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4U;
      return { data[idx], data[idx + 1], data[idx + 2], data[idx + 3] };
    };

    // Sample 4x4 grid
    std::array<float, 4> result { 0.0F, 0.0F, 0.0F, 0.0F };
    float weight_sum = 0.0F;

    for (int32_t j = -1; j <= 2; ++j) {
      const float wy = CubicWeight(fy - static_cast<float>(j));
      for (int32_t i = -1; i <= 2; ++i) {
        const float wx = CubicWeight(fx - static_cast<float>(i));
        const float weight = wx * wy;
        weight_sum += weight;

        const auto s = sample(x0 + i, y0 + j);
        for (size_t c = 0; c < 4; ++c) {
          result[c] += s[c] * weight;
        }
      }
    }

    // Normalize
    if (weight_sum > 0.0F) {
      for (size_t c = 0; c < 4; ++c) {
        result[c] /= weight_sum;
      }
    }

    return result;
  }

  //! Convert 3D direction to equirectangular UV coordinates.
  /*!
    Maps a normalized direction vector to UV coordinates in an equirectangular
    (latitude-longitude) projection.

    @param dir Normalized direction vector in D3D/OpenGL Y-up convention
    (X=right, Y=up, Z=forward)
    @return UV coordinates where u=[0,1] is longitude and v=[0,1] is latitude
  */
  auto DirectionToEquirectUV(const CubeFaceDirection& dir)
    -> std::pair<float, float>
  {
    // Input is in D3D Y-up convention (X=right, Y=up, Z=forward)

    // Standard equirectangular mapping:
    // theta (longitude) = atan2(x, z) in [-π, π], wrapping around Y axis
    // phi (latitude) = asin(y) in [-π/2, π/2], elevation from XZ plane
    const float theta = std::atan2(dir.x, dir.z);
    const float phi = std::asin(std::clamp(dir.y, -1.0F, 1.0F));

    // Map to [0, 1] UV coordinates
    // u: 0 = -π (left edge), 1 = +π (right edge)
    // In texture coordinates, v=0 is TOP of image (north pole/sky)
    // and v=1 is BOTTOM (south pole/ground), so we flip:
    // phi = +π/2 (looking up) → v = 0 (top of texture)
    // phi = -π/2 (looking down) → v = 1 (bottom of texture)
    const float u = (theta / std::numbers::pi_v<float> + 1.0F) * 0.5F;
    const float v = 0.5F - phi / std::numbers::pi_v<float>;

    return { u, v };
  }

} // namespace

auto ConvertEquirectangularToCube(
  const ScratchImage& equirect, const EquirectToCubeOptions& options)
  -> oxygen::Result<ScratchImage, TextureImportError>
{
  // Validate input
  if (!equirect.IsValid()) {
    return ::oxygen::Err(TextureImportError::kDecodeFailed);
  }

  const auto& src_meta = equirect.Meta();

  // Equirectangular should be 2:1 aspect ratio (or close to it)
  // Allow some tolerance for non-standard panoramas
  const float aspect
    = static_cast<float>(src_meta.width) / static_cast<float>(src_meta.height);
  if (aspect < 1.5F || aspect > 2.5F) {
    return ::oxygen::Err(TextureImportError::kInvalidDimensions);
  }

  // Only support float formats for HDR sampling
  // For LDR input, caller should convert to float first
  if (src_meta.format != Format::kRGBA32Float) {
    return ::oxygen::Err(TextureImportError::kInvalidOutputFormat);
  }

  if (options.face_size == 0) {
    return ::oxygen::Err(TextureImportError::kInvalidDimensions);
  }

  // Create output cube map
  ScratchImageMeta cube_meta {
    .texture_type = TextureType::kTextureCube,
    .width = options.face_size,
    .height = options.face_size,
    .depth = 1,
    .array_layers = kCubeFaceCount,
    .mip_levels = 1,
    .format = Format::kRGBA32Float,
  };

  ScratchImage cube = ScratchImage::Create(cube_meta);
  if (!cube.IsValid()) {
    return ::oxygen::Err(TextureImportError::kOutOfMemory);
  }

  // Get source pixels
  const auto src_image = equirect.GetImage(0, 0);
  const auto src_pixels = src_image.pixels;

  // Choose sampling function based on filter
  const bool use_bicubic = (options.sample_filter == MipFilter::kKaiser
    || options.sample_filter == MipFilter::kLanczos);

  // Process each face
  for (uint16_t face_idx = 0; face_idx < kCubeFaceCount; ++face_idx) {
    const auto face = static_cast<CubeFace>(face_idx);
    auto dst_pixels = cube.GetMutablePixels(face_idx, 0);
    auto* dst_data = reinterpret_cast<float*>(dst_pixels.data());

    const auto face_size = options.face_size;
    for (uint32_t y = 0; y < face_size; ++y) {
      for (uint32_t x = 0; x < face_size; ++x) {
        // Compute UV for this texel (center of texel)
        const float u
          = (static_cast<float>(x) + 0.5F) / static_cast<float>(face_size);
        const float v
          = (static_cast<float>(y) + 0.5F) / static_cast<float>(face_size);

        // Compute 3D direction using D3D Y-up cubemap convention
        const auto dir = ComputeCubeDirectionD3D(face, u, v);

        // Convert to equirect UV
        const auto [eq_u, eq_v] = DirectionToEquirectUV(dir);

        // Sample equirectangular image
        std::array<float, 4> color {};
        if (use_bicubic) {
          color = SampleBicubic(
            src_pixels, src_meta.width, src_meta.height, eq_u, eq_v);
        } else {
          color = SampleBilinear(
            src_pixels, src_meta.width, src_meta.height, eq_u, eq_v);
        }

        // Write to output
        const size_t dst_idx
          = (static_cast<size_t>(y) * face_size + static_cast<size_t>(x)) * 4;
        dst_data[dst_idx + 0] = color[0];
        dst_data[dst_idx + 1] = color[1];
        dst_data[dst_idx + 2] = color[2];
        dst_data[dst_idx + 3] = color[3];
      }
    }
  }

  return ::oxygen::Ok(std::move(cube));
}

} // namespace oxygen::content::import
