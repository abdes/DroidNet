//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>

namespace oxygen::vortex {

namespace {
  using profiling::ProfileColor;

  constexpr auto kBuckets = std::array {
    GpuWorkBucketInfo { .bucket = GpuWorkBucket::kVisibility,
      .id = "visibility",
      .display_name = "Visibility",
      .color = ProfileColor::Rgb(0x8E, 0x9A, 0xAF) },
    GpuWorkBucketInfo { .bucket = GpuWorkBucket::kDepthPrepass,
      .id = "depth_prepass",
      .display_name = "Depth prepass",
      .color = ProfileColor::Rgb(0x5B, 0x6C, 0x8F) },
    GpuWorkBucketInfo { .bucket = GpuWorkBucket::kShadows,
      .id = "shadows",
      .display_name = "Shadows",
      .color = ProfileColor::Rgb(0x7B, 0x5E, 0xA7) },
    GpuWorkBucketInfo { .bucket = GpuWorkBucket::kBasePass,
      .id = "base_pass",
      .display_name = "Base pass",
      .color = ProfileColor::Rgb(0x4D, 0x96, 0xFF) },
    GpuWorkBucketInfo { .bucket = GpuWorkBucket::kLighting,
      .id = "lighting",
      .display_name = "Lighting",
      .color = ProfileColor::Rgb(0xF2, 0xC1, 0x4E) },
    GpuWorkBucketInfo { .bucket = GpuWorkBucket::kEnvironment,
      .id = "environment",
      .display_name = "Environment",
      .color = ProfileColor::Rgb(0x4E, 0xC9, 0xB0) },
    GpuWorkBucketInfo { .bucket = GpuWorkBucket::kTranslucency,
      .id = "translucency",
      .display_name = "Translucency",
      .color = ProfileColor::Rgb(0x9F, 0xD8, 0xF5) },
    GpuWorkBucketInfo { .bucket = GpuWorkBucket::kPostProcess,
      .id = "post_process",
      .display_name = "Post-process",
      .color = ProfileColor::Rgb(0xFF, 0x8C, 0x42) },
    GpuWorkBucketInfo { .bucket = GpuWorkBucket::kOverlays,
      .id = "overlays",
      .display_name = "Overlays",
      .color = ProfileColor::Rgb(0xE0, 0x6C, 0x9F) },
    GpuWorkBucketInfo { .bucket = GpuWorkBucket::kComposition,
      .id = "composition",
      .display_name = "Composition",
      .color = ProfileColor::Rgb(0x00, 0xB8, 0x5C) },
    GpuWorkBucketInfo { .bucket = GpuWorkBucket::kBackground,
      .id = "background",
      .display_name = "Background",
      .color = ProfileColor::Rgb(0xB5, 0x89, 0x5A) },
    GpuWorkBucketInfo { .bucket = GpuWorkBucket::kOther,
      .id = "other",
      .display_name = "Other",
      .color = ProfileColor::Rgb(0xD0, 0x45, 0x45) },
  };

  static_assert(
    [] -> bool {
      for (std::size_t i = 0; i < kBuckets.size(); ++i) {
        if (static_cast<std::size_t>(kBuckets.at(i).bucket) != i) {
          return false;
        }
      }
      return true;
    }(),
    "the catalog lists every bucket in enum order");
} // namespace

auto EnumerateGpuWorkBuckets() noexcept -> std::span<const GpuWorkBucketInfo>
{
  return kBuckets;
}

auto GetGpuWorkBucketInfo(const GpuWorkBucket bucket) noexcept
  -> const GpuWorkBucketInfo&
{
  const auto index = static_cast<std::size_t>(bucket);
  return index < kBuckets.size() ? kBuckets.at(index) : kBuckets.back();
}

auto to_string(const GpuWorkBucket bucket) noexcept -> std::string_view
{
  return GetGpuWorkBucketInfo(bucket).id;
}

} // namespace oxygen::vortex
