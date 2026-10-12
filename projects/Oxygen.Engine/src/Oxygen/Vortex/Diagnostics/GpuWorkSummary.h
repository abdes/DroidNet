//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// The editor's C++/CLI interop consumes this header: C++20 only, no Vortex
// internal type.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

//! The fixed groups of renderer GPU work, in display order.
enum class GpuWorkBucket : std::uint8_t {
  kVisibility, //!< View init, occlusion, HZB.
  kDepthPrepass,
  kShadows,
  kBasePass,
  kLighting, //!< Light data, deferred and indirect lighting.
  kEnvironment, //!< Sky, atmosphere, fog.
  kTranslucency,
  kPostProcess, //!< Scene-colour resolve, exposure, tonemap.
  kOverlays, //!< Grid, selection, view overlays, debug views.
  kComposition, //!< Surface composition and surface layers.
  kBackground, //!< Frame work outside views: IBL, LUT bakes.
  kOther, //!< Unknown scopes, and command list work no stage covers.
};

//! Milliseconds over the summary window.
struct GpuWorkStat {
  float avg_ms { 0.0F };
  float max_ms { 0.0F };
};

struct GpuWorkItem {
  //! Stable, e.g. `lighting.deferred`.
  std::string id;
  //! Display text, e.g. `Deferred lights`.
  std::string name;
  GpuWorkStat time;
};

struct GpuWorkGroup {
  GpuWorkBucket bucket { GpuWorkBucket::kOther };
  GpuWorkStat time;
  //! At most `GpuWorkSummary::kMaxItemsPerGroup`, by `avg_ms` descending.
  std::vector<GpuWorkItem> items;
};

struct GpuWorkView {
  std::uint64_t view_id { 0U };
  std::string name;
  //! The sum of the groups.
  GpuWorkStat time;
  //! Catalog order; empty buckets omitted.
  std::vector<GpuWorkGroup> groups;
};

//! Where the renderer spent GPU time, averaged over a short window.
/*!
 `idle` is the part of `frame_gpu` when no graphics command list runs: the
 GPU waits for the CPU or for presentation. Work in a command list that no
 stage scope covers is not idle; it is a `kOther` item named after the list.
*/
struct GpuWorkSummary {
  static constexpr std::uint32_t kSchemaVersion { 1U };
  static constexpr std::size_t kMaxItemsPerGroup { 4U };

  //! Increments per publication.
  std::uint64_t revision { 0U };
  //! Newest frame in the window.
  frame::SequenceNumber last_frame { 0U };
  float window_ms { 0.0F };
  std::uint32_t valid_frames { 0U };
  //! Overflowed, incomplete or failed-resolve frames.
  std::uint32_t invalid_frames { 0U };
  //! Whole-frame GPU span.
  GpuWorkStat frame_gpu;
  GpuWorkStat idle;
  std::vector<GpuWorkView> views;
  //! Work outside any view, and its total.
  std::vector<GpuWorkGroup> frame_work;
  GpuWorkStat frame_work_time;
  //! Whole-frame spans of the last valid frames, newest last.
  std::vector<float> frame_gpu_history_ms;
};

//! Label and colour of a bucket, shared by every consumer.
struct GpuWorkBucketInfo {
  GpuWorkBucket bucket { GpuWorkBucket::kOther };
  //! Stable id for serialized forms, e.g. `base_pass`.
  std::string_view id;
  std::string_view display_name;
  //! Presentation hint.
  profiling::ProfileColor color;
};

//! Every bucket in display order.
OXGN_VRTX_NDAPI auto EnumerateGpuWorkBuckets() noexcept
  -> std::span<const GpuWorkBucketInfo>;
OXGN_VRTX_NDAPI auto GetGpuWorkBucketInfo(GpuWorkBucket bucket) noexcept
  -> const GpuWorkBucketInfo&;
OXGN_VRTX_NDAPI auto to_string(GpuWorkBucket bucket) noexcept
  -> std::string_view;

} // namespace oxygen::vortex
