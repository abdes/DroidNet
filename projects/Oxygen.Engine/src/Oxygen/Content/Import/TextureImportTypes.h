//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

//! Content-semantic intent for texture import.
/*!
  Specifies how the texture content should be interpreted during import and
  cooking. This affects mip generation, color space handling, and output format
  selection.

  @note The enum uses `uint8_t` as the underlying type for PAK format
  compatibility and compact serialization.
*/
enum class TextureIntent : uint8_t {
  // clang-format off
  kAlbedo         = 0,   //!< Base color / diffuse albedo (sRGB input expected)
  kNormalTS       = 1,   //!< Tangent-space normal map (linear, XY channels)
  kRoughness      = 2,   //!< Roughness map (linear, single channel)
  kMetallic       = 3,   //!< Metallic map (linear, single channel)
  kAO             = 4,   //!< Ambient occlusion map (linear, single channel)
  kEmissive       = 5,   //!< Emissive color map (sRGB or HDR)
  kOpacity        = 6,   //!< Opacity / alpha mask (linear, single channel)
  kORMPacked      = 7,   //!< Packed ORM: R=AO, G=Roughness, B=Metallic (linear)
  kHdrEnvironment = 8,   //!< HDR environment map (linear float)
  kHdrLightProbe  = 9,   //!< HDR light probe (linear float)
  kData           = 10,  //!< Generic data texture (linear, no special handling)
  // clang-format on
};

static_assert(sizeof(std::underlying_type_t<TextureIntent>) == sizeof(uint8_t),
  "TextureIntent enum must be 8 bits for PAK format compatibility");

//! String representation of enum values in `TextureIntent`.
OXGN_CNTT_NDAPI auto to_string(TextureIntent value) -> const char*;

//! Mip chain generation policy.
/*!
  Controls how mip levels are generated during texture cooking.
*/
enum class MipPolicy : uint8_t {
  // clang-format off
  kNone     = 0,  //!< No mip generation (single mip level)
  kFullChain = 1,  //!< Generate full mip chain down to 1x1
  kMaxCount = 2,  //!< Generate up to a specified maximum mip count
  // clang-format on
};

static_assert(sizeof(std::underlying_type_t<MipPolicy>) == sizeof(uint8_t),
  "MipPolicy enum must be 8 bits for PAK format compatibility");

//! String representation of enum values in `MipPolicy`.
OXGN_CNTT_NDAPI auto to_string(MipPolicy value) -> const char*;

//! Mip downsample filter kernel selection.
/*!
  Controls the filter kernel used for mip level generation.

  | Filter     | Quality | Performance | Use case |
  |------------|---------|-------------|----------|
  | `kBox`     | Lowest  | Fastest     | Previews, masks |
  | `kKaiser`  | Good    | Moderate    | General-purpose (default) |
  | `kLanczos` | Best    | Slowest     | High-quality final assets, UI, text |
*/
enum class MipFilter : uint8_t {
  // clang-format off
  kBox     = 0,  //!< 2x2 average — fast, slight aliasing on high-frequency content
  kKaiser  = 1,  //!< Kaiser-windowed sinc (alpha=4, width=6) — good quality, default
  kLanczos = 2,  //!< Lanczos-3 (a=3, width=6) — sharpest, minor ringing artifacts
  // clang-format on
};

static_assert(sizeof(std::underlying_type_t<MipFilter>) == sizeof(uint8_t),
  "MipFilter enum must be 8 bits for PAK format compatibility");

//! String representation of enum values in `MipFilter`.
OXGN_CNTT_NDAPI auto to_string(MipFilter value) -> const char*;

//! BC7 compression quality tier.
/*!
  Controls the quality vs. speed tradeoff for BC7 block compression.
  Use `kNone` to disable BC7 compression entirely.
*/
enum class Bc7Quality : uint8_t {
  // clang-format off
  kNone    = 0,  //!< No BC7 compression (store uncompressed)
  kFast    = 1,  //!< Fast encoding, lower quality
  kDefault = 2,  //!< Balanced quality and speed
  kHigh    = 3,  //!< High quality, slower encoding
  // clang-format on
};

static_assert(sizeof(std::underlying_type_t<Bc7Quality>) == sizeof(uint8_t),
  "Bc7Quality enum must be 8 bits for PAK format compatibility");

//! String representation of enum values in `Bc7Quality`.
OXGN_CNTT_NDAPI auto to_string(Bc7Quality value) -> const char*;

//! HDR content handling policy.
/*!
  Controls how HDR (floating-point) source content is handled when the
  output format is LDR (8-bit).

  This resolves the chicken-and-egg problem where users don't know if
  source content is HDR until after decoding, but must configure the
  import descriptor before calling CookTexture.

  | Mode           | Behavior |
  |----------------|----------|
  | `kError`       | Fail with kHdrRequiresFloatFormat if HDR→LDR mismatch |
  | `kTonemapAuto` | Automatically tonemap HDR to LDR (no error) |
  | `kKeepFloat`   | Force float output regardless of output_format |
*/
enum class HdrHandling : uint8_t {
  // clang-format off
  kError       = 0,  //!< Error if HDR input with LDR output (strict, explicit)
  kTonemapAuto = 1,  //!< Automatically tonemap HDR→LDR when output is LDR
  kKeepFloat   = 2,  //!< Override output_format to float for HDR content
  // clang-format on
};

static_assert(sizeof(std::underlying_type_t<HdrHandling>) == sizeof(uint8_t),
  "HdrHandling enum must be 8 bits for PAK format compatibility");

//! String representation of enum values in `HdrHandling`.
OXGN_CNTT_NDAPI auto to_string(HdrHandling value) -> const char*;

} // namespace oxygen::content::import
