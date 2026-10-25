//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>
#include <type_traits>

#include <Oxygen/Data/StableId.h>

namespace oxygen::data {

struct SurfaceIdTag {
  static constexpr std::string_view kName = "Surface";
};

//! Opaque identity of a surface, scoped by its geometry asset identity.
/*!
 Producers allocate surface IDs from retained native provenance; labels,
 binding ordinals and material equality do not establish identity. The nil
 value is an unset sentinel, rejected by surface inventory validation.
*/
using SurfaceId = StableId<SurfaceIdTag>;

static_assert(sizeof(SurfaceId) == SurfaceId::kSizeBytes);
static_assert(std::is_trivially_copyable_v<SurfaceId>);
static_assert(std::is_standard_layout_v<SurfaceId>);

} // namespace oxygen::data
