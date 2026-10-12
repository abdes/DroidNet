//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>
#include <Oxygen/Vortex/Internal/GpuTimelineProfiler.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::internal {

//! Turns GPU timeline frames into windowed `GpuWorkSummary` snapshots.
/*!
 Each valid frame is split by view (the `Vortex.View` scope open when a
 scope began) and by item: the outermost scope under the frame or view span
 counts, labelled by
 its stage-table entry, or under `kOther` by its own label. Nested scopes
 never add time, so nothing is counted twice. `kBackground` items are frame
 work wherever they were recorded. A window publishes when
 `kWindow` has elapsed since its first frame.

 Frames arrive on the render thread; `Latest` may be called from any thread
 and never sees a snapshot change after publication.
*/
class GpuWorkSummarizer final : public GpuTimelineSink {
public:
  using Clock = std::chrono::steady_clock;
  using NowFn = std::function<Clock::time_point()>;

  static constexpr auto kWindow = std::chrono::milliseconds { 500 };
  static constexpr std::size_t kHistoryLength { 120U };

  OXGN_VRTX_API explicit GpuWorkSummarizer(NowFn now = &Clock::now);
  OXGN_VRTX_API ~GpuWorkSummarizer() override;

  OXYGEN_MAKE_NON_COPYABLE(GpuWorkSummarizer)
  OXYGEN_MAKE_NON_MOVABLE(GpuWorkSummarizer)

  //! Starts summarizing frames from `first_frame` on, from an empty window.
  OXGN_VRTX_API auto Activate(std::uint64_t first_frame) -> void;
  //! Stops summarizing and drops the window and the published snapshot.
  OXGN_VRTX_API auto Deactivate() -> void;

  [[nodiscard]] OXGN_VRTX_API auto ConsumeFrame(const GpuTimelineFrame& frame)
    -> bool override;

  //! Null while inactive or before the first window closes.
  [[nodiscard]] OXGN_VRTX_API auto Latest() const
    -> std::shared_ptr<const GpuWorkSummary>;

private:
  struct ItemSample {
    std::string name;
    float ms { 0.0F };
  };
  using ItemKey = std::pair<GpuWorkBucket, std::string>;
  using GroupSamples = std::map<ItemKey, ItemSample>;
  struct ViewSample {
    std::string name;
    GroupSamples items;
  };
  struct FrameSample {
    float frame_gpu_ms { 0.0F };
    //! Frame span with no command list running.
    float idle_ms { 0.0F };
    std::map<std::uint64_t, ViewSample> views;
    GroupSamples frame_work;
  };

  [[nodiscard]] static auto Sample(const GpuTimelineFrame& frame)
    -> std::optional<FrameSample>;
  auto Publish(Clock::time_point now) -> void;

  NowFn now_;
  mutable std::mutex mutex_;
  bool active_ { false };
  std::uint64_t first_frame_ { 0U };
  std::optional<Clock::time_point> window_start_;
  std::vector<FrameSample> window_;
  std::uint32_t invalid_frames_ { 0U };
  std::uint64_t last_frame_ { 0U };
  std::vector<float> history_;
  std::uint64_t revision_ { 0U };
  std::shared_ptr<const GpuWorkSummary> latest_;
};

} // namespace oxygen::vortex::internal
