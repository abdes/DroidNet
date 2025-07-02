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

enum class TextureType : uint8_t {
  // clang-format off
  //!< Unknown texture type (Invalid or uninitialized state)
  kUnknown                    = 0,

  kTexture1D                  = 1,
  kTexture1DArray             = 2,
  kTexture2D                  = 3,
  kTexture2DArray             = 4,
  kTextureCube                = 5,
  kTextureCubeArray           = 6,
  kTexture2DMultiSample       = 7,
  kTexture2DMultiSampleArray  = 8,
  kTexture3D                  = 9,

  //! Sentinel value for the maximum texture type.
  kMaxTextureType             = kTexture3D,
  // clang-format on
};

static_assert(sizeof(std::underlying_type_t<TextureType>) == sizeof(uint8_t),
  "TextureType enum must be 8 bits for binary encoding");

//! String representation of enum values in `Format`.
OXGN_CORE_NDAPI auto to_string(TextureType value) -> const char*;

} // namespace oxygen
