//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerSeam.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPageAllocationPlanner.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::vsm {

class VsmCacheManager {
public:
  OXGN_RNDR_API explicit VsmCacheManager(
    Graphics* gfx, const VsmCacheManagerConfig& config = {}) noexcept;

  OXYGEN_MAKE_NON_COPYABLE(VsmCacheManager)
  OXYGEN_MAKE_NON_MOVABLE(VsmCacheManager)

  OXGN_RNDR_API ~VsmCacheManager();

  OXGN_RNDR_API auto Reset() -> void;

  OXGN_RNDR_API auto BeginFrame(const VsmCacheManagerSeam& seam,
    const VsmCacheManagerFrameConfig& config = {}) -> void;
  OXGN_RNDR_API auto SetPageRequests(std::span<const VsmPageRequest> requests)
    -> void;
  OXGN_RNDR_NDAPI auto BuildPageAllocationPlan()
    -> const VsmPageAllocationPlan&;
  OXGN_RNDR_NDAPI auto CommitPageAllocationFrame()
    -> const VsmPageAllocationFrame&;
  OXGN_RNDR_API auto PublishVisibleShadowPrimitives(
    std::span<const VsmPrimitiveIdentity> primitives) -> void;
  OXGN_RNDR_API auto PublishStaticPrimitivePageFeedback(
    std::span<const VsmStaticPrimitivePageFeedbackRecord> feedback) -> void;

  OXGN_RNDR_API auto ExtractFrameData() -> void;
  OXGN_RNDR_API auto InvalidateAll(VsmCacheInvalidationReason reason) -> void;
  OXGN_RNDR_API auto InvalidateLocalLights(const VsmRemapKeyList& remap_keys,
    VsmCacheInvalidationScope scope, VsmCacheInvalidationReason reason) -> void;
  OXGN_RNDR_API auto InvalidateDirectionalClipmaps(
    const VsmRemapKeyList& remap_keys, VsmCacheInvalidationScope scope,
    VsmCacheInvalidationReason reason) -> void;

  [[nodiscard]] OXGN_RNDR_NDAPI auto DescribeCacheDataState() const noexcept
    -> VsmCacheDataState;
  [[nodiscard]] OXGN_RNDR_NDAPI auto DescribeBuildState() const noexcept
    -> VsmCacheBuildState;
  [[nodiscard]] OXGN_RNDR_NDAPI auto IsCacheDataAvailable() const noexcept
    -> bool;
  [[nodiscard]] OXGN_RNDR_NDAPI auto IsHzbDataAvailable() const noexcept
    -> bool;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetCurrentFrame() const noexcept
    -> const VsmPageAllocationFrame*;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPreviousFrame() const noexcept
    -> const VsmExtractedCacheFrame*;

private:
  struct PendingInvalidation {
    VsmLightCacheKind kind { VsmLightCacheKind::kLocal };
    VsmRemapKeyList remap_keys {};
    VsmCacheInvalidationScope scope {
      VsmCacheInvalidationScope::kStaticAndDynamic
    };
    VsmCacheInvalidationReason reason {
      VsmCacheInvalidationReason::kTargetedInvalidate
    };
  };

  struct FrameWorkingSetResources {
    std::size_t page_table_entry_capacity { 0 };
    std::size_t physical_page_capacity { 0 };
    std::shared_ptr<graphics::Buffer> page_table_buffer {};
    std::shared_ptr<graphics::Buffer> page_flags_buffer {};
    std::shared_ptr<graphics::Buffer> dirty_flags_buffer {};
    std::shared_ptr<graphics::Buffer> physical_page_list_buffer {};
    std::shared_ptr<graphics::Buffer> page_rect_bounds_buffer {};
  };

  struct RuntimeState {
    VsmCacheDataState cache_data_state { VsmCacheDataState::kUnavailable };
    VsmCacheBuildState build_state { VsmCacheBuildState::kIdle };
    bool is_hzb_data_available { false };
    VsmCacheInvalidationReason invalidation_reason {
      VsmCacheInvalidationReason::kNone
    };
    std::optional<VsmCacheManagerSeam> captured_seam {};
    VsmCacheManagerFrameConfig frame_config {};
    VsmPageRequestSet page_requests {};
    VsmPageAllocationPlan current_plan {};
    std::optional<VsmPageAllocationSnapshot> planned_snapshot {};
    // Current-frame publication owns all cache-manager continuity products,
    // including retained unreferenced-entry state. The captured seam remains
    // immutable after BeginFrame().
    std::optional<VsmPageAllocationFrame> current_frame {};
    std::optional<VsmExtractedCacheFrame> previous_frame {};
    // Targeted invalidations are queued here and applied to a planning copy of
    // the previous extracted frame so GetPreviousFrame() remains the canonical
    // extracted snapshot until the next ExtractFrameData().
    std::vector<PendingInvalidation> pending_invalidations {};
    FrameWorkingSetResources working_set_resources {};
  };

  [[nodiscard]] auto IsFrameShapeCompatible(
    const VsmCacheManagerSeam& seam) const noexcept -> bool;
  [[nodiscard]] auto ComputeHzbAvailability(
    const VsmCacheManagerSeam& seam) const noexcept -> bool;
  auto QueueTargetedInvalidation(VsmLightCacheKind kind,
    const VsmRemapKeyList& remap_keys, VsmCacheInvalidationScope scope,
    VsmCacheInvalidationReason reason) -> void;
  auto ApplyPendingInvalidations(VsmExtractedCacheFrame& snapshot) const
    -> void;
  auto ApplyRetainedEntryContinuity(const VsmCacheManagerSeam& seam,
    VsmPageAllocationPlan& plan, VsmPageAllocationSnapshot& snapshot) -> void;
  auto EnsureWorkingSetResources(const VsmPageAllocationSnapshot& snapshot)
    -> const FrameWorkingSetResources&;
  auto MarkCacheDataUnavailable() noexcept -> void;

  Graphics* gfx_ { nullptr };
  VsmCacheManagerConfig config_ {};
  VsmPageAllocationPlanner planner_ {};
  RuntimeState runtime_state_ {};
};

} // namespace oxygen::renderer::vsm
