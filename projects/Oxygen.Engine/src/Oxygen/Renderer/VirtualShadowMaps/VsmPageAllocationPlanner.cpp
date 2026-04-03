//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPageAllocationPlanner.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPageAllocationSnapshotHelpers.h>

#include <Oxygen/Base/Logging.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace oxygen::renderer::vsm {

using detail::BuildBaseSnapshot;

namespace {

  struct RequestKey {
    VsmVirtualShadowMapId map_id { 0 };
    VsmVirtualPageCoord page {};

    auto operator==(const RequestKey&) const -> bool = default;
  };

  struct RequestKeyHasher {
    auto operator()(const RequestKey& key) const noexcept -> std::size_t
    {
      auto hash = std::size_t { key.map_id };
      hash ^= static_cast<std::size_t>(key.page.level) << 8U;
      hash ^= static_cast<std::size_t>(key.page.page_x) << 20U;
      hash ^= static_cast<std::size_t>(key.page.page_y) << 32U;
      return hash;
    }
  };

  struct PendingRequest {
    VsmPageAllocationDecision decision {};
    std::uint32_t page_table_index { 0 };
    std::uint32_t target_slice { 0U };
    std::optional<VsmPageInitializationAction> initialization_action {};
    bool is_resolved { false };
    bool is_duplicate { false };
  };

  auto IsDynamicRequest(const VsmPageRequestFlags flags) noexcept -> bool
  {
    return (flags & VsmPageRequestFlags::kStaticOnly)
      != VsmPageRequestFlags::kStaticOnly;
  }

  auto IsPageInvalidated(const VsmPhysicalPageMeta& meta,
    const VsmPageRequest& request) noexcept -> bool
  {
    if (IsDynamicRequest(request.flags)) {
      return static_cast<bool>(meta.dynamic_invalidated)
        || static_cast<bool>(meta.static_invalidated);
    }
    return static_cast<bool>(meta.static_invalidated);
  }

  auto HasStaticSlice(const VsmPhysicalPoolSnapshot& pool) noexcept -> bool
  {
    return std::ranges::find(
             pool.slice_roles, VsmPhysicalPoolSliceRole::kStaticDepth)
      != pool.slice_roles.end();
  }

  auto FindSliceIndex(const VsmPhysicalPoolSnapshot& pool,
    const VsmPhysicalPoolSliceRole role) noexcept
    -> std::optional<std::uint32_t>
  {
    for (std::uint32_t i = 0; i < pool.slice_roles.size(); ++i) {
      if (pool.slice_roles[i] == role) {
        return i;
      }
    }
    return std::nullopt;
  }

  auto ResolveTargetSlice(const VsmPhysicalPoolSnapshot& pool,
    const VsmPageRequestFlags flags) noexcept -> std::optional<std::uint32_t>
  {
    const auto dynamic_slice
      = FindSliceIndex(pool, VsmPhysicalPoolSliceRole::kDynamicDepth);
    if (!dynamic_slice.has_value()) {
      return std::nullopt;
    }

    if (IsDynamicRequest(flags)) {
      return dynamic_slice;
    }

    if (const auto static_slice
      = FindSliceIndex(pool, VsmPhysicalPoolSliceRole::kStaticDepth);
      static_slice.has_value()) {
      return static_slice;
    }

    return dynamic_slice;
  }

  auto TryResolvePhysicalPageSlice(const VsmPhysicalPoolSnapshot& pool,
    const VsmPhysicalPageIndex physical_page) noexcept
    -> std::optional<std::uint32_t>
  {
    const auto coord = TryConvertToCoord(
      physical_page, pool.tile_capacity, pool.tiles_per_axis, pool.slice_count);
    if (!coord.has_value()) {
      return std::nullopt;
    }
    return coord->slice;
  }

  auto DetermineInitializationAction(const VsmCacheManagerSeam& seam,
    const VsmPhysicalPageMeta& previous_meta,
    const VsmPageRequest& request) noexcept -> VsmPageInitializationAction
  {
    if (HasStaticSlice(seam.physical_pool) && IsDynamicRequest(request.flags)
      && previous_meta.dynamic_invalidated
      && !previous_meta.static_invalidated) {
      return VsmPageInitializationAction::kCopyStaticSlice;
    }

    return VsmPageInitializationAction::kClearDepth;
  }

  auto AppendInitializationWork(VsmPageAllocationPlan& plan,
    const VsmPhysicalPageIndex physical_page,
    const VsmPageInitializationAction action) -> void
  {
    plan.initialization_work.push_back(VsmPageInitializationWorkItem {
      .physical_page = physical_page,
      .action = action,
    });
    ++plan.initialized_page_count;
  }

  auto TranslatePreviousOwner(const VsmVirtualAddressSpaceFrame& current_frame,
    const VsmPhysicalPageMeta& previous_meta, const VsmVirtualRemapEntry& remap)
    -> std::optional<RequestKey>
  {
    if (remap.current_id == 0) {
      return std::nullopt;
    }

    const auto translated_x
      = static_cast<std::int64_t>(previous_meta.owner_page.page_x)
      + remap.page_offset.x;
    const auto translated_y
      = static_cast<std::int64_t>(previous_meta.owner_page.page_y)
      + remap.page_offset.y;
    if (translated_x < 0 || translated_y < 0) {
      return std::nullopt;
    }

    const auto translated_request = VsmPageRequest {
      .map_id = remap.current_id,
      .page
      = {
          .level = previous_meta.owner_page.level,
          .page_x = static_cast<std::uint32_t>(translated_x),
          .page_y = static_cast<std::uint32_t>(translated_y),
        },
    };
    if (!detail::TryResolvePageTableEntryIndex(
          current_frame, translated_request)
          .has_value()) {
      return std::nullopt;
    }
    return RequestKey {
      .map_id = translated_request.map_id,
      .page = translated_request.page,
    };
  }

  auto PushEvictionDecision(VsmPageAllocationPlan& plan,
    std::vector<VsmPageAllocationDecision>& eviction_decisions,
    const VsmPhysicalPageIndex physical_page) -> void
  {
    eviction_decisions.push_back(VsmPageAllocationDecision {
      .action = VsmAllocationAction::kEvict,
      .previous_physical_page = physical_page,
    });
    ++plan.evicted_page_count;
  }

} // namespace

VsmPageAllocationPlanner::~VsmPageAllocationPlanner() = default;

auto VsmPageAllocationPlanner::Build(const VsmCacheManagerSeam& seam,
  const VsmExtractedCacheFrame* previous_frame,
  const VsmCacheDataState cache_data_state,
  std::span<const VsmPageRequest> requests) const -> Result
{
  auto result = Result { .snapshot = BuildBaseSnapshot(seam) };
  auto& plan = result.plan;
  auto& snapshot = result.snapshot;
  auto effective_previous_frame = previous_frame;

  if (effective_previous_frame != nullptr
    && effective_previous_frame->physical_pages.size()
      != snapshot.physical_pages.size()) {
    LOG_F(WARNING,
      "VsmPageAllocationPlanner: ignoring previous extracted frame due to "
      "physical-page capacity mismatch (previous={}, current={}, "
      "cache_state={})",
      effective_previous_frame->physical_pages.size(),
      snapshot.physical_pages.size(),
      static_cast<std::uint32_t>(cache_data_state));
    effective_previous_frame = nullptr;
  }

  auto pending_requests = std::vector<PendingRequest> {};
  pending_requests.resize(requests.size());

  auto request_index_by_key
    = std::unordered_map<RequestKey, std::size_t, RequestKeyHasher> {};
  auto remap_by_previous_id
    = std::unordered_map<VsmVirtualShadowMapId, VsmVirtualRemapEntry> {};
  auto eviction_decisions = std::vector<VsmPageAllocationDecision> {};
  auto available_physical_pages_by_slice
    = std::vector<std::vector<VsmPhysicalPageIndex>>(
      seam.physical_pool.slice_count);

  for (std::size_t i = 0; i < requests.size(); ++i) {
    auto& pending = pending_requests[i];
    pending.decision.request = requests[i];

    if (!IsValid(requests[i])) {
      pending.decision.action = VsmAllocationAction::kReject;
      pending.decision.failure_reason
        = VsmAllocationFailureReason::kInvalidRequest;
      ++plan.rejected_page_count;
      continue;
    }

    const auto page_table_index
      = detail::TryResolvePageTableEntryIndex(seam.current_frame, requests[i]);
    if (!page_table_index.has_value()) {
      pending.decision.action = VsmAllocationAction::kReject;
      pending.decision.failure_reason
        = VsmAllocationFailureReason::kInvalidRequest;
      ++plan.rejected_page_count;
      continue;
    }

    pending.page_table_index = *page_table_index;
    const auto target_slice
      = ResolveTargetSlice(seam.physical_pool, requests[i].flags);
    if (!target_slice.has_value()) {
      pending.decision.action = VsmAllocationAction::kReject;
      pending.decision.failure_reason
        = VsmAllocationFailureReason::kInvalidRequest;
      ++plan.rejected_page_count;
      continue;
    }
    pending.target_slice = *target_slice;
    if (!request_index_by_key
          .emplace(RequestKey { .map_id = requests[i].map_id,
                     .page = requests[i].page },
            i)
          .second) {
      pending.decision.action = VsmAllocationAction::kReject;
      pending.decision.failure_reason
        = VsmAllocationFailureReason::kInvalidRequest;
      pending.is_duplicate = true;
      ++plan.rejected_page_count;
      continue;
    }
  }

  if (cache_data_state == VsmCacheDataState::kAvailable
    && effective_previous_frame != nullptr) {
    for (const auto& remap_entry : seam.previous_to_current_remap.entries) {
      remap_by_previous_id.try_emplace(remap_entry.previous_id, remap_entry);
    }
  }

  if (effective_previous_frame != nullptr) {
    for (std::uint32_t page_index = 0;
      page_index < snapshot.physical_pages.size(); ++page_index) {
      const auto physical_page = VsmPhysicalPageIndex { .value = page_index };
      const auto physical_page_slice
        = TryResolvePhysicalPageSlice(seam.physical_pool, physical_page);
      CHECK_F(physical_page_slice.has_value(),
        "VsmPageAllocationPlanner: physical page {} must resolve to a valid "
        "slice for the active pool layout",
        physical_page.value);
      const auto& previous_meta
        = effective_previous_frame->physical_pages[page_index];
      if (!previous_meta.is_allocated) {
        available_physical_pages_by_slice[*physical_page_slice].push_back(
          physical_page);
        continue;
      }

      if (cache_data_state != VsmCacheDataState::kAvailable) {
        PushEvictionDecision(plan, eviction_decisions, physical_page);
        available_physical_pages_by_slice[*physical_page_slice].push_back(
          physical_page);
        continue;
      }

      const auto remap_it = remap_by_previous_id.find(previous_meta.owner_id);
      if (remap_it == remap_by_previous_id.end()
        || remap_it->second.rejection_reason
          != VsmReuseRejectionReason::kNone) {
        PushEvictionDecision(plan, eviction_decisions, physical_page);
        available_physical_pages_by_slice[*physical_page_slice].push_back(
          physical_page);
        continue;
      }

      const auto translated = TranslatePreviousOwner(
        seam.current_frame, previous_meta, remap_it->second);
      if (!translated.has_value()) {
        PushEvictionDecision(plan, eviction_decisions, physical_page);
        available_physical_pages_by_slice[*physical_page_slice].push_back(
          physical_page);
        continue;
      }

      const auto request_it = request_index_by_key.find(*translated);
      if (request_it == request_index_by_key.end()) {
        PushEvictionDecision(plan, eviction_decisions, physical_page);
        available_physical_pages_by_slice[*physical_page_slice].push_back(
          physical_page);
        continue;
      }

      auto& pending = pending_requests[request_it->second];
      if ((pending.decision.action == VsmAllocationAction::kReject
            && pending.decision.failure_reason
              != VsmAllocationFailureReason::kNone)
        || pending.decision.action == VsmAllocationAction::kReuseExisting
        || pending.decision.action == VsmAllocationAction::kInitializeOnly
        || pending.decision.action == VsmAllocationAction::kAllocateNew) {
        PushEvictionDecision(plan, eviction_decisions, physical_page);
        available_physical_pages_by_slice[*physical_page_slice].push_back(
          physical_page);
        continue;
      }

      if (pending.target_slice != *physical_page_slice) {
        PushEvictionDecision(plan, eviction_decisions, physical_page);
        available_physical_pages_by_slice[*physical_page_slice].push_back(
          physical_page);
        continue;
      }

      if (IsPageInvalidated(previous_meta, pending.decision.request)) {
        pending.initialization_action = DetermineInitializationAction(
          seam, previous_meta, pending.decision.request);
        PushEvictionDecision(plan, eviction_decisions, physical_page);
        available_physical_pages_by_slice[*physical_page_slice].push_back(
          physical_page);
        continue;
      }

      const auto initialize_only = previous_meta.view_uncached;
      pending.decision.action = initialize_only
        ? VsmAllocationAction::kInitializeOnly
        : VsmAllocationAction::kReuseExisting;
      pending.decision.previous_physical_page = physical_page;
      pending.decision.current_physical_page = physical_page;
      pending.is_resolved = true;

      auto current_meta = previous_meta;
      current_meta.used_this_frame = true;
      current_meta.owner_id = pending.decision.request.map_id;
      current_meta.owner_mip_level = pending.decision.request.page.level;
      current_meta.owner_page = pending.decision.request.page;
      current_meta.age = 0;
      current_meta.last_touched_frame = seam.current_frame.frame_generation;

      snapshot.page_table[pending.page_table_index]
        = { .is_mapped = true, .physical_page = physical_page };
      snapshot.physical_pages[page_index] = current_meta;

      ++plan.reused_page_count;
      if (initialize_only) {
        AppendInitializationWork(
          plan, physical_page, VsmPageInitializationAction::kClearDepth);
      }
    }
  } else {
    for (std::uint32_t page_index = 0;
      page_index < snapshot.physical_pages.size(); ++page_index) {
      const auto physical_page = VsmPhysicalPageIndex { .value = page_index };
      const auto physical_page_slice
        = TryResolvePhysicalPageSlice(seam.physical_pool, physical_page);
      CHECK_F(physical_page_slice.has_value(),
        "VsmPageAllocationPlanner: physical page {} must resolve to a valid "
        "slice for the active pool layout",
        physical_page.value);
      available_physical_pages_by_slice[*physical_page_slice].push_back(
        physical_page);
    }
  }

  auto next_available_page_by_slice
    = std::vector<std::size_t>(seam.physical_pool.slice_count, 0U);
  for (auto& pending : pending_requests) {
    if (pending.is_resolved
      || (pending.decision.action == VsmAllocationAction::kReject
        && pending.decision.failure_reason
          != VsmAllocationFailureReason::kNone)) {
      continue;
    }

    auto& next_available_page
      = next_available_page_by_slice[pending.target_slice];
    const auto& available_physical_pages
      = available_physical_pages_by_slice[pending.target_slice];
    if (next_available_page >= available_physical_pages.size()) {
      pending.decision.action = VsmAllocationAction::kReject;
      pending.decision.failure_reason
        = VsmAllocationFailureReason::kNoAvailablePhysicalPages;
      ++plan.rejected_page_count;
      continue;
    }

    const auto physical_page = available_physical_pages[next_available_page++];
    pending.decision.action = VsmAllocationAction::kAllocateNew;
    pending.decision.current_physical_page = physical_page;
    pending.is_resolved = true;

    snapshot.page_table[pending.page_table_index]
      = { .is_mapped = true, .physical_page = physical_page };
    snapshot.physical_pages[physical_page.value] = {
      .is_allocated = true,
      .is_dirty = false,
      .used_this_frame = true,
      .view_uncached = true,
      .static_invalidated = true,
      .dynamic_invalidated = true,
      .age = 0,
      .owner_id = pending.decision.request.map_id,
      .owner_mip_level = pending.decision.request.page.level,
      .owner_page = pending.decision.request.page,
      .last_touched_frame = seam.current_frame.frame_generation,
    };

    ++plan.allocated_page_count;
    AppendInitializationWork(plan, physical_page,
      pending.initialization_action.value_or(
        VsmPageInitializationAction::kClearDepth));
  }

  plan.decisions.reserve(pending_requests.size() + eviction_decisions.size());
  for (const auto& pending : pending_requests) {
    plan.decisions.push_back(pending.decision);
  }
  for (const auto& eviction : eviction_decisions) {
    plan.decisions.push_back(eviction);
  }

  return result;
}

} // namespace oxygen::renderer::vsm
