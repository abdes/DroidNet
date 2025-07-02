//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Specifies the intended rendering domain or pipeline for the material
enum class MaterialDomain : uint8_t {
  // clang-format off
  kUnknown           = 0, //!< Unknown or uninitialized domain

  kOpaque            = 1, //!< Standard surface, fully opaque, rendered in main pass
  kAlphaBlended      = 2, //!< Transparent/semi-transparent, rendered with blending
  kMasked            = 3, //!< Alpha test/cutout for hard-edged transparency (foliage)
  kDecal             = 4, //!< Projected or mesh decals
  kUserInterface     = 5, //!< User interface elements
  kPostProcess       = 6, //!< Post-processing effects

  //!< Maximum value sentinel
  kMaxMaterialDomain = kPostProcess
  // clang-format on
};

static_assert(sizeof(std::underlying_type_t<MaterialDomain>) <= sizeof(uint8_t),
  "MaterialDomain enum fit in `uint8_t` for compatibility with PAK format");

//! String representation of enum values in `MaterialDomain`.
OXGN_DATA_NDAPI auto to_string(MaterialDomain value) noexcept -> const char*;

} // namespace oxygen::data
