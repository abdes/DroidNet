//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include <Oxygen/Cooker/Pak/PakPlan.h>

namespace oxygen::content::pak::internal {

//! Places the resource row `record` where the plan lays `resource` out: its
//! data offset, its size and, for a row that records one, its alignment.
/*!
 @return False, `record` unchanged, when the size or the alignment does not
   fit the row's field.
*/
template <typename Desc>
[[nodiscard]] auto PlaceResourceRow(
  const PakResourcePlacementPlan& resource, Desc& record) -> bool
{
  using SizeT = decltype(record.size_bytes);
  if (!std::in_range<SizeT>(resource.size_bytes)) {
    return false;
  }
  if constexpr (requires { record.alignment; }) {
    using AlignmentT = decltype(record.alignment);
    if (!std::in_range<AlignmentT>(resource.alignment)) {
      return false;
    }
    record.alignment = static_cast<AlignmentT>(resource.alignment);
  }
  record.data_offset = resource.offset;
  record.size_bytes = static_cast<SizeT>(resource.size_bytes);
  return true;
}

} // namespace oxygen::content::pak::internal
