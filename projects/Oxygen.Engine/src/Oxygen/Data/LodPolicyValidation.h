//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

#include <Oxygen/Data/PakFormat_geometry.h>

namespace oxygen::data {
namespace detail {
  //! True when `values` are finite and non-decreasing.
  inline auto IsLodBoundaryList(std::span<const float> values) -> bool
  {
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (!std::isfinite(values[i]) || (i > 0 && values[i] < values[i - 1])) {
        return false;
      }
    }
    return true;
  }

  //! True when every value is zero, as the fields of other policy types are.
  inline auto IsZeroList(std::span<const float> values) -> bool
  {
    return std::ranges::all_of(
      values, [](const float value) { return value == 0.0F; });
  }
} // namespace detail

//! True when `record` is a LOD policy as LodPolicyRecord describes it.
inline auto IsValidLodPolicy(const pak::geometry::LodPolicyRecord& record)
  -> bool
{
  using pak::geometry::kMaxLodBoundaryCount;
  using pak::geometry::kMaxLodCount;
  using pak::geometry::LodPolicyType;

  constexpr auto kMaxHysteresisRatio = 0.99F;
  if (record.boundary_count > kMaxLodBoundaryCount) {
    return false;
  }
  const auto count = std::size_t { record.boundary_count };
  const auto thresholds = std::span<const float> { record.thresholds };
  const auto enter = std::span<const float> { record.enter_finer_sse };
  const auto exit = std::span<const float> { record.exit_coarser_sse };
  switch (record.type) {
  case LodPolicyType::kFixed:
    return record.lod_index < kMaxLodCount && count == 0
      && record.hysteresis_ratio == 0.0F && detail::IsZeroList(thresholds)
      && detail::IsZeroList(enter) && detail::IsZeroList(exit);
  case LodPolicyType::kDistance:
    return record.lod_index == 0
      && detail::IsLodBoundaryList(thresholds.first(count))
      && detail::IsZeroList(thresholds.subspan(count))
      && record.hysteresis_ratio >= 0.0F
      && record.hysteresis_ratio <= kMaxHysteresisRatio
      && detail::IsZeroList(enter) && detail::IsZeroList(exit);
  case LodPolicyType::kScreenSpaceError:
    return record.lod_index == 0 && record.hysteresis_ratio == 0.0F
      && detail::IsZeroList(thresholds)
      && detail::IsLodBoundaryList(enter.first(count))
      && detail::IsZeroList(enter.subspan(count))
      && detail::IsLodBoundaryList(exit.first(count))
      && detail::IsZeroList(exit.subspan(count));
  }
  return false;
}

} // namespace oxygen::data
