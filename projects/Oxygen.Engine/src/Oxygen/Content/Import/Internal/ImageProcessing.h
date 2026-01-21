//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <Oxygen/Content/Import/ScratchImage.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Core/Types/ColorSpace.h>

namespace oxygen::content::import {

//===----------------------------------------------------------------------===//
// image::color - Color Space Conversion Utilities
//===----------------------------------------------------------------------===//

namespace image::color {

  //! Convert a single sRGB component to linear space.
  /*!
    Applies the sRGB transfer function inverse:
    - For values <= 0.04045: linear = sRGB / 12.92
    - For values > 0.04045: linear = ((sRGB + 0.055) / 1.055)^2.4

    @param srgb sRGB component value in [0, 1]
    @return Linear component value
  */
  OXGN_CNTT_NDAPI auto SrgbToLinear(float srgb) noexcept -> float;

  //! Convert a single linear component to sRGB space.
  /*!
    Applies the sRGB transfer function:
    - For values <= 0.0031308: sRGB = linear * 12.92
    - For values > 0.0031308: sRGB = 1.055 * linear^(1/2.4) - 0.055

    @param linear Linear component value in [0, 1]
    @return sRGB component value
  */
  OXGN_CNTT_NDAPI auto LinearToSrgb(float linear) noexcept -> float;

  //! Convert RGBA from sRGB to linear space (alpha unchanged).
  /*!
    @param rgba sRGB RGBA values, each in [0, 1]
    @return Linear RGBA values
  */
  OXGN_CNTT_NDAPI auto SrgbToLinear(std::array<float, 4> rgba) noexcept
    -> std::array<float, 4>;

  //! Convert RGBA from linear to sRGB space (alpha unchanged).
  /*!
    @param rgba Linear RGBA values, each in [0, 1]
    @return sRGB RGBA values
  */
  OXGN_CNTT_NDAPI auto LinearToSrgb(std::array<float, 4> rgba) noexcept
    -> std::array<float, 4>;

  //! Convert an entire ScratchImage from sRGB to linear space.
  /*!
    Operates in-place on RGBA32Float or RGBA8UNorm images.
    Alpha channel is preserved unchanged.

    @param image Image to convert (modified in-place)
  */
  OXGN_CNTT_API void ConvertSrgbToLinear(ScratchImage& image);

  //! Convert an entire ScratchImage from linear to sRGB space.
  /*!
    Operates in-place on RGBA32Float or RGBA8UNorm images.
    Alpha channel is preserved unchanged.

    @param image Image to convert (modified in-place)
  */
  OXGN_CNTT_API void ConvertLinearToSrgb(ScratchImage& image);

} // namespace image::color

//===----------------------------------------------------------------------===//
// image::hdr - HDR Processing Utilities
//===----------------------------------------------------------------------===//

namespace image::hdr {

  //! Apply exposure adjustment to HDR pixel values.
  /*!
    Multiplies RGB by 2^exposure. Alpha is unchanged.

    @param rgba RGBA pixel values
    @param exposure Exposure value in stops (EV)
    @return Exposure-adjusted RGBA values
  */
  OXGN_CNTT_NDAPI auto ApplyExposure(
    std::array<float, 4> rgba, float exposure) noexcept -> std::array<float, 4>;

  //! Apply ACES fitted tonemapping to HDR pixel values.
  /*!
    Uses the fitted ACES curve from Krzysztof Narkowicz:
    https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/

    @param rgba Linear HDR RGBA values
    @return Tonemapped LDR RGBA values in [0, 1]
  */
  OXGN_CNTT_NDAPI auto AcesTonemap(std::array<float, 4> rgba) noexcept
    -> std::array<float, 4>;

  //! Bake HDR image to LDR using exposure and tonemapping.
  /*!
    Pipeline: exposure adjustment → ACES tonemap → quantize to 8-bit

    @param hdr_image  HDR image (RGBA32Float)
    @param exposure   Exposure value in stops (EV)
    @return LDR image (RGBA8UNorm)
  */
  OXGN_CNTT_NDAPI auto BakeToLdr(const ScratchImage& hdr_image, float exposure)
    -> ScratchImage;

} // namespace image::hdr

//===----------------------------------------------------------------------===//
// image::mip - Mip Generation and Filter Kernels
//===----------------------------------------------------------------------===//

namespace image::mip {

  //! Compute modified Bessel function of the first kind, order 0.
  /*!
    Used for Kaiser window computation.

    @param x Input value
    @return I0(x)
  */
  OXGN_CNTT_NDAPI auto BesselI0(float x) noexcept -> float;

  //! Compute Kaiser window value.
  /*!
    @param x     Position in [-1, 1]
    @param alpha Shape parameter (higher = sharper cutoff)
    @return Window value
  */
  OXGN_CNTT_NDAPI auto KaiserWindow(float x, float alpha) noexcept -> float;

  //! Compute Lanczos kernel value.
  /*!
    @param x Position
    @param a Lanczos parameter (typically 2 or 3)
    @return Kernel value
  */
  OXGN_CNTT_NDAPI auto LanczosKernel(float x, int a) noexcept -> float;

  //! Compute the number of mip levels for given dimensions.
  /*!
    @param width  Base width in pixels
    @param height Base height in pixels
    @return Number of mip levels for a full chain
  */
  OXGN_CNTT_NDAPI auto ComputeMipCount(uint32_t width, uint32_t height) noexcept
    -> uint32_t;

  //! Generate a mip chain for a 2D texture.
  /*!
    Creates a new ScratchImage with all mip levels generated.
    Performs filtering in linear space when color_space is sRGB.

    @param source      Source image (single mip)
    @param filter      Mip filter to use
    @param color_space Color space of the source image
    @param target_mip_levels Number of mip levels to generate (0 for full chain)
    @return New ScratchImage with mip chain
  */
  OXGN_CNTT_NDAPI auto GenerateChain2D(const ScratchImage& source,
    MipFilter filter, ColorSpace color_space, uint32_t target_mip_levels = 0)
    -> ScratchImage;

  //! Generate a mip chain for a 3D texture.
  /*!
    Creates a new ScratchImage with all mip levels generated.
    Downsamples in all three dimensions.

    @param source      Source image (single mip)
    @param filter      Mip filter to use
    @param color_space Color space of the source image
    @param target_mip_levels Number of mip levels to generate (0 for full chain)
    @return New ScratchImage with mip chain
  */
  OXGN_CNTT_NDAPI auto GenerateChain3D(const ScratchImage& source,
    MipFilter filter, ColorSpace color_space, uint32_t target_mip_levels = 0)
    -> ScratchImage;

} // namespace image::mip

//===----------------------------------------------------------------------===//
// image::content - Content-Specific Processing
//===----------------------------------------------------------------------===//

namespace image::content {

  //! Renormalize a normal map texel to unit length.
  /*!
    Unpacks from [0,1] to [-1,1], normalizes, repacks to [0,1].

    @param rgba RGBA values with normal in RGB
    @return Renormalized RGBA values
  */
  OXGN_CNTT_NDAPI auto RenormalizeNormal(std::array<float, 4> rgba) noexcept
    -> std::array<float, 4>;

  //! Generate mip chain for a normal map with optional renormalization.
  /*!
    Uses box filter for averaging, then renormalizes each texel.

    @param source      Source normal map (single mip)
    @param renormalize Whether to renormalize after filtering
    @param target_mip_levels Number of mip levels to generate (0 for full chain)
    @return New ScratchImage with mip chain
  */
  OXGN_CNTT_NDAPI auto GenerateNormalMapMips(const ScratchImage& source,
    bool renormalize, uint32_t target_mip_levels = 0) -> ScratchImage;

  //! Flip the green channel of a normal map.
  /*!
    Converts between OpenGL (Y-up) and DirectX (Y-down) conventions.
    Operates in-place.

    @param image Normal map image (modified in-place)
  */
  OXGN_CNTT_API void FlipNormalGreen(ScratchImage& image);

} // namespace image::content

} // namespace oxygen::content::import
