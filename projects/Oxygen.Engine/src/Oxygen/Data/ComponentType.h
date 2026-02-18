//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Component types identifiers (FourCC)
/*!
  Values are 4-byte character codes (FourCC) packed into a 32-bit integer.
  This makes the binary file readable in a hex editor (e.g. 'MESH', 'PCAM')
  and avoids collisions between component types without a central registry.
*/
enum class ComponentType : uint32_t {
// NOLINTNEXTLINE(*-macro-*)
#define OXDAT_COMPONENT_TYPE(name, value) name = value,
#include <Oxygen/Core/Meta/Data/ComponentType.inc>
#undef OXDAT_COMPONENT_TYPE
};

static_assert(sizeof(ComponentType) == sizeof(uint32_t),
  "ComponentType enum must be 32-bit for compatibility with PAK format");

//! String representation of enum values in `ComponentType`.
OXGN_DATA_NDAPI auto to_string(ComponentType value) noexcept -> const char*;

} // namespace oxygen::data
