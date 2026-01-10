//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <Oxygen/Core/api_export.h>

namespace oxygen {

//! Color space enumeration for textures and render targets.
/*!
  Represents the color space interpretation for texture data and render outputs.
  This is an engine-wide concept used in shaders, render passes, swapchain
  configuration, and texture import/cooking pipelines.

  @note The enum uses `uint8_t` as the underlying type for PAK format
  compatibility and compact serialization.
*/
enum class ColorSpace : uint8_t {
  // clang-format off
  kLinear = 0,  //!< Linear color space (physical light values)
  kSRGB   = 1,  //!< sRGB color space (perceptual, gamma-encoded)
  // clang-format on
};

static_assert(sizeof(std::underlying_type_t<ColorSpace>) == sizeof(uint8_t),
  "ColorSpace enum must be 8 bits for PAK format compatibility");

//! String representation of enum values in `ColorSpace`.
OXGN_CORE_NDAPI auto to_string(ColorSpace value) -> const char*;

} // namespace oxygen
