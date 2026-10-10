//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>

using oxygen::data::LodIndex;
using oxygen::scene::DistancePolicy;
using oxygen::scene::FixedPolicy;
using oxygen::scene::ScreenSpaceErrorPolicy;

auto FixedPolicy::Clamp(std::size_t lod_count) const noexcept -> LodIndex
{
  if (lod_count == 0) {
    return kFinest;
  }
  return (index.get() < lod_count)
    ? index
    : LodIndex { static_cast<std::uint32_t>(lod_count - 1) };
}

void DistancePolicy::NormalizeThresholds() noexcept
{
  if (!thresholds.empty()) {
    for (std::size_t i = 1; i < thresholds.size(); ++i) {
      if (thresholds[i] < thresholds[i - 1])
        thresholds[i] = thresholds[i - 1];
    }
  }
  hysteresis_ratio = (std::clamp)(hysteresis_ratio, 0.0f, 0.99f);
}

auto DistancePolicy::SelectBase(
  float normalized_distance, std::size_t lod_count) const noexcept -> LodIndex
{
  if (thresholds.empty()) {
    return LodIndex {};
  }
  std::uint32_t lod = 0;
  while (lod < lod_count - 1 && lod < thresholds.size()
    && normalized_distance >= thresholds[lod]) {
    ++lod;
  }
  return LodIndex { lod };
}

auto DistancePolicy::ApplyHysteresis(std::optional<LodIndex> current,
  LodIndex base, float normalized_distance,
  std::size_t lod_count) const noexcept -> LodIndex
{
  (void)lod_count;
  if (!current.has_value()) {
    return base;
  }
  const auto last = *current;
  if (base == last) {
    return last;
  }
  const auto boundary = (std::min)(last, base).get();
  if (boundary >= thresholds.size()) {
    return base;
  }
  const float t = thresholds[boundary];
  const float enter = t * (1.0f + hysteresis_ratio);
  const float exit = t * (1.0f - hysteresis_ratio);
  if (base > last) {
    return (normalized_distance >= enter) ? base : last;
  } else {
    return (normalized_distance <= exit) ? base : last;
  }
}

void ScreenSpaceErrorPolicy::NormalizeMonotonic() noexcept
{
  if (!enter_finer_sse.empty()) {
    for (std::size_t i = 1; i < enter_finer_sse.size(); ++i) {
      if (enter_finer_sse[i] < enter_finer_sse[i - 1])
        enter_finer_sse[i] = enter_finer_sse[i - 1];
    }
  }
  if (!exit_coarser_sse.empty()) {
    for (std::size_t i = 1; i < exit_coarser_sse.size(); ++i) {
      if (exit_coarser_sse[i] < exit_coarser_sse[i - 1])
        exit_coarser_sse[i] = exit_coarser_sse[i - 1];
    }
  }
}

auto ScreenSpaceErrorPolicy::ValidateSizes(std::size_t lod_count) const noexcept
  -> bool
{
  if (lod_count == 0)
    return true;
  const auto need = (lod_count > 0) ? (lod_count - 1) : 0u;
  if (!enter_finer_sse.empty() && enter_finer_sse.size() < need)
    return false;
  if (!exit_coarser_sse.empty() && exit_coarser_sse.size() < need)
    return false;
  return true;
}

auto ScreenSpaceErrorPolicy::SelectBase(
  float sse, std::size_t lod_count) const noexcept -> LodIndex
{
  if (enter_finer_sse.empty()) {
    return LodIndex {};
  }
  std::uint32_t lod = 0;
  while (lod < lod_count - 1 && lod < enter_finer_sse.size()
    && sse < enter_finer_sse[lod]) {
    ++lod;
  }
  return LodIndex { lod };
}

auto ScreenSpaceErrorPolicy::ApplyHysteresis(std::optional<LodIndex> current,
  LodIndex base, float sse, std::size_t lod_count) const noexcept -> LodIndex
{
  (void)lod_count;
  if (!current.has_value()) {
    return base;
  }
  const auto last = *current;
  if (base == last) {
    return last;
  }
  const auto boundary = (std::min)(last, base).get();
  if (boundary >= enter_finer_sse.size()
    || boundary >= exit_coarser_sse.size()) {
    return base;
  }
  const float e_in = enter_finer_sse[boundary];
  const float e_out = exit_coarser_sse[boundary];
  if (base > last) {
    return (sse <= e_out) ? base : last;
  } else {
    return (sse >= e_in) ? base : last;
  }
}
