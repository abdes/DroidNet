//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPageAllocationSnapshotHelpers.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <glm/vec4.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>

namespace oxygen::renderer::vsm {

namespace {

  constexpr auto kPageTableEntryStrideBytes = sizeof(std::uint32_t);
  constexpr auto kPageFlagsStrideBytes = sizeof(std::uint32_t);
  constexpr auto kDirtyFlagsStrideBytes = sizeof(std::uint32_t);
  constexpr auto kPhysicalPageListStrideBytes = sizeof(std::uint32_t);
  constexpr auto kPageRectBoundsStrideBytes = sizeof(std::uint32_t) * 4U;

  auto BuildWorkingSetDebugName(
    const VsmCacheManagerFrameConfig& config, const char* suffix) -> std::string
  {
    if (config.debug_name.empty()) {
      return std::string("VsmPageAllocation.") + suffix;
    }

    return config.debug_name + "." + suffix;
  }

  auto CreateWorkingSetBuffer(Graphics& gfx, const std::uint64_t size_bytes,
    const std::string& debug_name) -> std::shared_ptr<graphics::Buffer>
  {
    auto desc = graphics::BufferDesc {};
    desc.size_bytes = size_bytes;
    desc.usage = graphics::BufferUsage::kStorage;
    desc.memory = graphics::BufferMemory::kDeviceLocal;
    desc.debug_name = debug_name;

    auto buffer = gfx.CreateBuffer(desc);
    if (!buffer) {
      LOG_F(WARNING,
        "failed to create page-allocation working-set buffer "
        "debug_name=`{}` size_bytes={}",
        debug_name, size_bytes);
      throw std::runtime_error(
        "VsmCacheManager: failed to create page-allocation working-set buffer");
    }

    return buffer;
  }

  template <typename Resource>
  auto RegisterResourceIfNeeded(
    Graphics& gfx, const std::shared_ptr<Resource>& resource) -> void
  {
    if (resource == nullptr) {
      return;
    }

    auto& registry = gfx.GetResourceRegistry();
    if (!registry.Contains(*resource)) {
      registry.Register(resource);
    }
  }

  template <typename Resource>
  auto UnregisterResourceIfPresent(
    Graphics& gfx, const std::shared_ptr<Resource>& resource) -> void
  {
    if (resource == nullptr) {
      return;
    }

    auto& registry = gfx.GetResourceRegistry();
    if (registry.Contains(*resource)) {
      registry.UnRegisterResource(*resource);
    }
  }

  auto ReleaseWorkingSetResources(Graphics* gfx, auto& resources) -> void
  {
    if (gfx != nullptr) {
      UnregisterResourceIfPresent(*gfx, resources.page_table_buffer);
      UnregisterResourceIfPresent(*gfx, resources.page_flags_buffer);
      UnregisterResourceIfPresent(*gfx, resources.dirty_flags_buffer);
      UnregisterResourceIfPresent(*gfx, resources.physical_page_list_buffer);
      UnregisterResourceIfPresent(*gfx, resources.page_rect_bounds_buffer);
    }

    resources = {};
  }

  auto IsRemapKeyReferenced(const VsmVirtualAddressSpaceFrame& frame,
    const std::string_view remap_key) -> bool
  {
    const auto local_it = std::ranges::find(
      frame.local_light_layouts, remap_key, &VsmVirtualMapLayout::remap_key);
    if (local_it != frame.local_light_layouts.end()) {
      return true;
    }

    const auto clipmap_it = std::ranges::find(
      frame.directional_layouts, remap_key, &VsmClipmapLayout::remap_key);
    return clipmap_it != frame.directional_layouts.end();
  }

  auto FindMaxVirtualId(const VsmVirtualAddressSpaceFrame& frame) noexcept
    -> VsmVirtualShadowMapId
  {
    auto max_virtual_id = VsmVirtualShadowMapId { 0 };
    for (const auto& layout : frame.local_light_layouts) {
      max_virtual_id = (std::max)(max_virtual_id, layout.id);
    }
    for (const auto& layout : frame.directional_layouts) {
      if (layout.clip_level_count == 0) {
        continue;
      }
      max_virtual_id = (std::max)(max_virtual_id,
        layout.first_id + layout.clip_level_count - 1);
    }

    return max_virtual_id;
  }

  struct RetainableLayoutRef {
    const VsmVirtualMapLayout* local { nullptr };
    const VsmClipmapLayout* directional { nullptr };
  };

  auto FindRetainableLayout(const VsmPageAllocationSnapshot& snapshot,
    const VsmLightCacheEntryState& entry) -> RetainableLayoutRef
  {
    if (entry.kind == VsmLightCacheKind::kLocal) {
      const auto current_local_it
        = std::ranges::find_if(snapshot.virtual_frame.local_light_layouts,
          [&entry](const auto& layout) {
            return layout.id == entry.current_frame_state.virtual_map_id
              && layout.remap_key == entry.remap_key;
          });
      if (current_local_it
        != snapshot.virtual_frame.local_light_layouts.end()) {
        return { .local = &*current_local_it };
      }

      const auto retained_local_it = std::ranges::find_if(
        snapshot.retained_local_light_layouts, [&entry](const auto& layout) {
          return layout.id == entry.current_frame_state.virtual_map_id
            && layout.remap_key == entry.remap_key;
        });
      if (retained_local_it != snapshot.retained_local_light_layouts.end()) {
        return { .local = &*retained_local_it };
      }

      return {};
    }

    const auto current_directional_it = std::ranges::find_if(
      snapshot.virtual_frame.directional_layouts, [&entry](const auto& layout) {
        return layout.first_id == entry.current_frame_state.virtual_map_id
          && layout.remap_key == entry.remap_key;
      });
    if (current_directional_it
      != snapshot.virtual_frame.directional_layouts.end()) {
      return { .directional = &*current_directional_it };
    }

    const auto retained_directional_it = std::ranges::find_if(
      snapshot.retained_directional_layouts, [&entry](const auto& layout) {
        return layout.first_id == entry.current_frame_state.virtual_map_id
          && layout.remap_key == entry.remap_key;
      });
    if (retained_directional_it
      != snapshot.retained_directional_layouts.end()) {
      return { .directional = &*retained_directional_it };
    }

    return {};
  }

  auto ComputeRetainedEntryAge(const VsmPageAllocationSnapshot& snapshot,
    const VsmLightCacheEntryState& entry) -> std::uint32_t
  {
    const auto first_entry = entry.current_frame_state.first_page_table_entry;
    const auto entry_count = entry.current_frame_state.page_table_entry_count;
    CHECK_F(first_entry + entry_count <= snapshot.page_table.size(),
      "retained entry page-table range must stay within the extracted "
      "snapshot");

    auto max_age = std::uint32_t { 0 };
    auto found_mapping = false;
    for (std::uint32_t i = 0; i < entry_count; ++i) {
      const auto& page_table_entry = snapshot.page_table[first_entry + i];
      if (!page_table_entry.is_mapped) {
        continue;
      }

      const auto& physical_page
        = snapshot.physical_pages.at(page_table_entry.physical_page.value);
      max_age = (std::max)(max_age, physical_page.age);
      found_mapping = true;
    }

    return found_mapping ? max_age + 1U : 0U;
  }

  template <typename T>
  auto DeserializeBufferReadback(const std::vector<std::byte>& bytes,
    const std::size_t expected_count, std::string_view debug_name)
    -> std::vector<T>
  {
    const auto expected_bytes = expected_count * sizeof(T);
    CHECK_F(bytes.size() == expected_bytes,
      "buffer readback size mismatch debug_name=`{}` expected_bytes={} "
      "actual_bytes={}",
      debug_name, expected_bytes, bytes.size());

    auto values = std::vector<T>(expected_count);
    if (!bytes.empty()) {
      std::memcpy(values.data(), bytes.data(), bytes.size());
    }
    return values;
  }

  auto AwaitBufferReadback(graphics::ReadbackManager& readback_manager,
    const std::shared_ptr<graphics::GpuBufferReadback>& readback,
    const graphics::ReadbackTicket ticket, const std::size_t expected_bytes,
    std::string_view debug_name) -> std::vector<std::byte>
  {
    CHECK_NOTNULL_F(readback.get(),
      "buffer readback object is null debug_name=`{}`", debug_name);

    const auto wait_result = readback_manager.Await(ticket);
    CHECK_F(wait_result.has_value(),
      "buffer readback await failed debug_name=`{}` ticket={} fence={} "
      "error={}",
      debug_name, ticket.id, ticket.fence.get(), wait_result.error());

    const auto mapped = readback->TryMap();
    CHECK_F(mapped.has_value(),
      "buffer readback map failed debug_name=`{}` ticket={} fence={} error={}",
      debug_name, ticket.id, ticket.fence.get(), mapped.error());

    const auto bytes = mapped->Bytes();
    CHECK_F(bytes.size() == expected_bytes,
      "buffer readback mapped byte count mismatch debug_name=`{}` "
      "expected_bytes={} actual_bytes={}",
      debug_name, expected_bytes, bytes.size());
    return { bytes.begin(), bytes.end() };
  }

  auto RemoveEvictionsForRetainedPages(VsmPageAllocationPlan& plan,
    const std::unordered_set<std::uint32_t>& retained_pages) -> void
  {
    if (retained_pages.empty()) {
      return;
    }

    const auto old_size = plan.decisions.size();
    std::erase_if(plan.decisions, [&retained_pages](const auto& decision) {
      return decision.action == VsmAllocationAction::kEvict
        && retained_pages.contains(decision.previous_physical_page.value);
    });
    const auto removed_count = old_size - plan.decisions.size();
    CHECK_F(plan.evicted_page_count >= removed_count,
      "retained-entry continuity must not underflow eviction counts");
    plan.evicted_page_count -= static_cast<std::uint32_t>(removed_count);
  }

  struct AppliedInvalidationStats {
    std::uint32_t matched_entries { 0 };
    std::uint32_t marked_pages { 0 };
    std::uint32_t static_marks { 0 };
    std::uint32_t dynamic_marks { 0 };
  };

  auto MergeInvalidationScope(const VsmCacheInvalidationScope lhs,
    const VsmCacheInvalidationScope rhs) noexcept -> VsmCacheInvalidationScope
  {
    if (lhs == rhs) {
      return lhs;
    }
    return VsmCacheInvalidationScope::kStaticAndDynamic;
  }

  auto ResolveProjectionIndex(const VsmExtractedCacheFrame& snapshot,
    const VsmVirtualShadowMapId map_id) -> std::optional<std::uint32_t>
  {
    for (std::uint32_t i = 0; i < snapshot.projection_records.size(); ++i) {
      if (snapshot.projection_records[i].map_id == map_id) {
        return i;
      }
    }
    return std::nullopt;
  }

  auto IsUsableInvalidationSphere(const glm::vec4& sphere) noexcept -> bool
  {
    return std::isfinite(sphere.x) && std::isfinite(sphere.y)
      && std::isfinite(sphere.z) && std::isfinite(sphere.w) && sphere.w > 0.0F;
  }

  auto ApplyInvalidationScope(VsmPhysicalPageMeta& meta,
    const VsmCacheInvalidationScope scope, AppliedInvalidationStats& stats)
    -> void
  {
    switch (scope) {
    case VsmCacheInvalidationScope::kDynamicOnly:
      if (!meta.dynamic_invalidated) {
        ++stats.dynamic_marks;
      }
      meta.dynamic_invalidated = true;
      break;
    case VsmCacheInvalidationScope::kStaticOnly:
      if (!meta.static_invalidated) {
        ++stats.static_marks;
      }
      meta.static_invalidated = true;
      break;
    case VsmCacheInvalidationScope::kStaticAndDynamic:
      if (!meta.static_invalidated) {
        ++stats.static_marks;
      }
      if (!meta.dynamic_invalidated) {
        ++stats.dynamic_marks;
      }
      meta.static_invalidated = true;
      meta.dynamic_invalidated = true;
      break;
    }
  }

  auto ComparePrimitiveIdentity(const VsmPrimitiveIdentity& lhs,
    const VsmPrimitiveIdentity& rhs) noexcept -> bool
  {
    return std::tie(lhs.transform_index, lhs.transform_generation,
             lhs.submesh_index, lhs.primitive_flags)
      < std::tie(rhs.transform_index, rhs.transform_generation,
        rhs.submesh_index, rhs.primitive_flags);
  }

  auto CompareRenderedPrimitiveHistory(
    const VsmRenderedPrimitiveHistoryRecord& lhs,
    const VsmRenderedPrimitiveHistoryRecord& rhs) noexcept -> bool
  {
    return std::tie(lhs.map_id, lhs.primitive.transform_index,
             lhs.primitive.transform_generation, lhs.primitive.submesh_index,
             lhs.primitive.primitive_flags)
      < std::tie(rhs.map_id, rhs.primitive.transform_index,
        rhs.primitive.transform_generation, rhs.primitive.submesh_index,
        rhs.primitive.primitive_flags);
  }

  auto CompareStaticPrimitiveFeedback(
    const VsmStaticPrimitivePageFeedbackRecord& lhs,
    const VsmStaticPrimitivePageFeedbackRecord& rhs) noexcept -> bool
  {
    return std::tie(lhs.page_table_index, lhs.primitive.transform_index,
             lhs.primitive.transform_generation, lhs.primitive.submesh_index,
             lhs.primitive.primitive_flags)
      < std::tie(rhs.page_table_index, rhs.primitive.transform_index,
        rhs.primitive.transform_generation, rhs.primitive.submesh_index,
        rhs.primitive.primitive_flags);
  }

  [[nodiscard]] auto TryFindRemapEntry(const VsmVirtualRemapTable& remap_table,
    const VsmVirtualShadowMapId previous_map_id) noexcept
    -> const VsmVirtualRemapEntry*
  {
    const auto remap_it = std::ranges::find_if(
      remap_table.entries, [previous_map_id](const auto& entry) {
        return entry.previous_id == previous_map_id;
      });
    return remap_it != remap_table.entries.end() ? &*remap_it : nullptr;
  }

  [[nodiscard]] auto TryResolveSnapshotPageTableEntryIndex(
    const VsmPageAllocationSnapshot& snapshot,
    const VsmVirtualShadowMapId map_id,
    const VsmVirtualPageCoord& virtual_page) noexcept
    -> std::optional<std::uint32_t>;

  [[nodiscard]] auto TryTranslateCurrentMapId(const VsmCacheManagerSeam& seam,
    const VsmPageAllocationSnapshot& snapshot,
    const VsmVirtualShadowMapId previous_map_id) noexcept
    -> std::optional<VsmVirtualShadowMapId>
  {
    if (const auto* remap_entry
      = TryFindRemapEntry(seam.previous_to_current_remap, previous_map_id);
      remap_entry != nullptr && remap_entry->current_id != 0U
      && remap_entry->rejection_reason == VsmReuseRejectionReason::kNone) {
      return remap_entry->current_id;
    }

    if (TryResolveSnapshotPageTableEntryIndex(
          snapshot, previous_map_id, VsmVirtualPageCoord {})
          .has_value()) {
      return previous_map_id;
    }
    return std::nullopt;
  }

  [[nodiscard]] auto TryTranslateCurrentPage(const VsmCacheManagerSeam& seam,
    const VsmPageAllocationSnapshot& snapshot,
    const VsmVirtualShadowMapId previous_map_id,
    const VsmVirtualPageCoord& previous_page) noexcept
    -> std::optional<VsmPageRequest>
  {
    if (const auto* remap_entry
      = TryFindRemapEntry(seam.previous_to_current_remap, previous_map_id);
      remap_entry != nullptr) {
      if (remap_entry->current_id == 0U
        || remap_entry->rejection_reason != VsmReuseRejectionReason::kNone) {
        return std::nullopt;
      }

      const auto translated_x = static_cast<std::int64_t>(previous_page.page_x)
        - remap_entry->page_offset.x;
      const auto translated_y = static_cast<std::int64_t>(previous_page.page_y)
        - remap_entry->page_offset.y;
      if (translated_x < 0 || translated_y < 0) {
        return std::nullopt;
      }

      auto request = VsmPageRequest {
        .map_id = remap_entry->current_id,
        .page
        = {
            .level = previous_page.level,
            .page_x = static_cast<std::uint32_t>(translated_x),
            .page_y = static_cast<std::uint32_t>(translated_y),
          },
      };
      return TryResolveSnapshotPageTableEntryIndex(
               snapshot, request.map_id, request.page)
               .has_value()
        ? std::optional<VsmPageRequest> { request }
        : std::nullopt;
    }

    return TryResolveSnapshotPageTableEntryIndex(
             snapshot, previous_map_id, previous_page)
        .has_value()
      ? std::optional<VsmPageRequest> {
          VsmPageRequest { .map_id = previous_map_id, .page = previous_page },
        }
      : std::nullopt;
  }

  [[nodiscard]] auto TryResolveSnapshotPageTableEntryIndex(
    const VsmPageAllocationSnapshot& snapshot,
    const VsmVirtualShadowMapId map_id,
    const VsmVirtualPageCoord& virtual_page) noexcept
    -> std::optional<std::uint32_t>
  {
    for (const auto& layout : snapshot.virtual_frame.local_light_layouts) {
      if (layout.id == map_id) {
        return TryGetPageTableEntryIndex(layout, virtual_page);
      }
    }
    for (const auto& layout : snapshot.retained_local_light_layouts) {
      if (layout.id == map_id) {
        return TryGetPageTableEntryIndex(layout, virtual_page);
      }
    }
    for (const auto& layout : snapshot.virtual_frame.directional_layouts) {
      if (virtual_page.level < layout.clip_level_count
        && map_id == layout.first_id + virtual_page.level) {
        return TryGetPageTableEntryIndex(layout, virtual_page);
      }
    }
    for (const auto& layout : snapshot.retained_directional_layouts) {
      if (virtual_page.level < layout.clip_level_count
        && map_id == layout.first_id + virtual_page.level) {
        return TryGetPageTableEntryIndex(layout, virtual_page);
      }
    }
    return std::nullopt;
  }

  auto CarryForwardContinuityPublications(const VsmCacheManagerSeam& seam,
    const VsmExtractedCacheFrame& previous_snapshot,
    VsmPageAllocationSnapshot& current_snapshot) -> void
  {
    auto current_visible = current_snapshot.visible_shadow_primitives;
    std::sort(
      current_visible.begin(), current_visible.end(), ComparePrimitiveIdentity);
    current_visible.erase(
      std::unique(current_visible.begin(), current_visible.end()),
      current_visible.end());

    auto merged_history = current_snapshot.rendered_primitive_history;
    for (const auto& history : previous_snapshot.rendered_primitive_history) {
      if (!current_visible.empty()
        && !std::binary_search(current_visible.begin(), current_visible.end(),
          history.primitive, ComparePrimitiveIdentity)) {
        continue;
      }

      const auto current_map_id
        = TryTranslateCurrentMapId(seam, current_snapshot, history.map_id);
      if (!current_map_id.has_value()) {
        continue;
      }

      merged_history.push_back(VsmRenderedPrimitiveHistoryRecord {
        .primitive = history.primitive,
        .map_id = *current_map_id,
      });
    }
    std::sort(merged_history.begin(), merged_history.end(),
      CompareRenderedPrimitiveHistory);
    merged_history.erase(
      std::unique(merged_history.begin(), merged_history.end()),
      merged_history.end());

    auto merged_feedback = current_snapshot.static_primitive_page_feedback;
    for (const auto& feedback :
      previous_snapshot.static_primitive_page_feedback) {
      if (feedback.valid == 0U) {
        continue;
      }

      const auto translated_request = TryTranslateCurrentPage(
        seam, current_snapshot, feedback.map_id, feedback.virtual_page);
      if (!translated_request.has_value()) {
        continue;
      }

      const auto page_table_index = TryResolveSnapshotPageTableEntryIndex(
        current_snapshot, translated_request->map_id, translated_request->page);
      if (!page_table_index.has_value()
        || *page_table_index >= current_snapshot.page_table.size()) {
        continue;
      }

      const auto& page_table_entry
        = current_snapshot.page_table[*page_table_index];
      if (!page_table_entry.is_mapped
        || page_table_entry.physical_page.value
          >= current_snapshot.physical_pages.size()) {
        continue;
      }

      const auto& physical_page
        = current_snapshot.physical_pages[page_table_entry.physical_page.value];
      if (!physical_page.is_allocated || physical_page.static_invalidated
        || physical_page.owner_id != translated_request->map_id
        || physical_page.owner_page != translated_request->page) {
        continue;
      }

      merged_feedback.push_back(VsmStaticPrimitivePageFeedbackRecord {
        .primitive = feedback.primitive,
        .page_table_index = *page_table_index,
        .physical_page = page_table_entry.physical_page,
        .map_id = translated_request->map_id,
        .virtual_page = translated_request->page,
        .valid = 1U,
      });
    }
    std::sort(merged_feedback.begin(), merged_feedback.end(),
      CompareStaticPrimitiveFeedback);
    merged_feedback.erase(
      std::unique(merged_feedback.begin(), merged_feedback.end()),
      merged_feedback.end());

    if (merged_history.size()
        != current_snapshot.rendered_primitive_history.size()
      || merged_feedback.size()
        != current_snapshot.static_primitive_page_feedback.size()) {
      DLOG_F(2,
        "carried continuity publications generation={} rendered_history={}=>{} "
        "static_feedback={}=>{}",
        current_snapshot.frame_generation,
        current_snapshot.rendered_primitive_history.size(),
        merged_history.size(),
        current_snapshot.static_primitive_page_feedback.size(),
        merged_feedback.size());
    }

    current_snapshot.rendered_primitive_history = std::move(merged_history);
    current_snapshot.static_primitive_page_feedback
      = std::move(merged_feedback);
  }

  auto ClearTransientInvalidationFlags(VsmPageAllocationSnapshot& snapshot)
    -> void
  {
    for (auto& physical_page : snapshot.physical_pages) {
      physical_page.static_invalidated = false;
      physical_page.dynamic_invalidated = false;
    }
  }

} // namespace

VsmCacheManager::VsmCacheManager(
  Graphics* gfx, const VsmCacheManagerConfig& config) noexcept
  : gfx_(gfx)
  , config_(config)
{
}

VsmCacheManager::~VsmCacheManager()
{
  ReleaseWorkingSetResources(gfx_, runtime_state_.working_set_resources);
}

auto VsmCacheManager::Reset() -> void
{
  ReleaseWorkingSetResources(gfx_, runtime_state_.working_set_resources);
  runtime_state_ = {};

  DLOG_F(2, "reset cache-manager debug_name=`{}`", config_.debug_name);
}

auto VsmCacheManager::BeginFrame(const VsmCacheManagerSeam& seam,
  const VsmCacheManagerFrameConfig& config) -> void
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kIdle,
    "BeginFrame requires idle build state");

  FinalizePendingFrameExtraction();

  runtime_state_.captured_seam = seam;
  runtime_state_.frame_config = config;
  runtime_state_.page_requests.clear();
  runtime_state_.current_plan = {};
  runtime_state_.planned_snapshot.reset();
  runtime_state_.current_frame.reset();
  runtime_state_.prepared_invalidation_workload = {};
  runtime_state_.recently_removed_primitives.clear();
  runtime_state_.build_state = VsmCacheBuildState::kFrameOpen;

  if (!runtime_state_.previous_frame.has_value()) {
    runtime_state_.invalidation_reason = VsmCacheInvalidationReason::kNone;
    MarkCacheDataUnavailable();
  } else if (!config_.allow_reuse || !config.allow_reuse) {
    runtime_state_.invalidation_reason = VsmCacheInvalidationReason::kNone;
    MarkCacheDataUnavailable();
  } else if (runtime_state_.cache_data_state
    == VsmCacheDataState::kInvalidated) {
    runtime_state_.is_hzb_data_available = false;
  } else if (config.force_invalidate_all) {
    runtime_state_.cache_data_state = VsmCacheDataState::kInvalidated;
    runtime_state_.is_hzb_data_available = false;
    runtime_state_.invalidation_reason
      = VsmCacheInvalidationReason::kExplicitInvalidateAll;
  } else if (runtime_state_.previous_frame->pool_identity
      != seam.physical_pool.pool_identity
    || runtime_state_.previous_frame->pool_page_size_texels
      != seam.physical_pool.page_size_texels
    || runtime_state_.previous_frame->pool_tile_capacity
      != seam.physical_pool.tile_capacity
    || runtime_state_.previous_frame->pool_slice_count
      != seam.physical_pool.slice_count
    || runtime_state_.previous_frame->pool_depth_format
      != seam.physical_pool.depth_format
    || runtime_state_.previous_frame->pool_slice_roles
      != seam.physical_pool.slice_roles) {
    runtime_state_.cache_data_state = VsmCacheDataState::kInvalidated;
    runtime_state_.is_hzb_data_available = false;
    runtime_state_.invalidation_reason
      = VsmCacheInvalidationReason::kIncompatiblePool;
    LOG_F(WARNING,
      "invalidating cache data because the previous extracted pool is "
      "incompatible with the current pool generation={} debug_name=`{}`",
      seam.current_frame.frame_generation, config.debug_name);
  } else if (!IsFrameShapeCompatible(seam)) {
    runtime_state_.cache_data_state = VsmCacheDataState::kInvalidated;
    runtime_state_.is_hzb_data_available = false;
    runtime_state_.invalidation_reason
      = VsmCacheInvalidationReason::kIncompatibleFrameShape;
    LOG_F(WARNING,
      "invalidating cache data because the extracted snapshot shape is "
      "incompatible with the current publication contract generation={} "
      "debug_name=`{}`",
      seam.current_frame.frame_generation, config.debug_name);
  } else {
    runtime_state_.cache_data_state = VsmCacheDataState::kAvailable;
    runtime_state_.is_hzb_data_available = ComputeHzbAvailability(seam);
  }

  DLOG_F(2,
    "begin cache frame generation={} pool_identity={} cache_state={} "
    "build_state={} invalidation_reason={} hzb_available={} allow_reuse={} "
    "force_invalidate_all={} debug_name=`{}`",
    seam.current_frame.frame_generation, seam.physical_pool.pool_identity,
    nostd::to_string(runtime_state_.cache_data_state),
    nostd::to_string(runtime_state_.build_state),
    nostd::to_string(runtime_state_.invalidation_reason),
    runtime_state_.is_hzb_data_available, config.allow_reuse,
    config.force_invalidate_all, config.debug_name);
}

auto VsmCacheManager::SetPageRequests(
  const std::span<const VsmPageRequest> requests) -> void
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kFrameOpen,
    "SetPageRequests requires frame-open state");
  CHECK_F(runtime_state_.captured_seam.has_value(),
    "SetPageRequests requires a captured seam");

  runtime_state_.page_requests.assign(requests.begin(), requests.end());

  DLOG_F(2, "captured page requests generation={} request_count={}",
    runtime_state_.captured_seam->current_frame.frame_generation,
    runtime_state_.page_requests.size());
}

auto VsmCacheManager::BuildPageAllocationPlan() -> const VsmPageAllocationPlan&
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kFrameOpen,
    "BuildPageAllocationPlan requires frame-open state");
  CHECK_F(runtime_state_.captured_seam.has_value(),
    "BuildPageAllocationPlan requires a captured seam");

  auto planning_previous_frame = std::optional<VsmExtractedCacheFrame> {};
  const auto* previous_frame_for_planner
    = runtime_state_.previous_frame ? &*runtime_state_.previous_frame : nullptr;
  if (previous_frame_for_planner != nullptr
    && !runtime_state_.pending_invalidations.empty()) {
    planning_previous_frame = *previous_frame_for_planner;
    ApplyPendingInvalidations(*planning_previous_frame);
    previous_frame_for_planner = &*planning_previous_frame;
    runtime_state_.pending_invalidations.clear();
  }

  const auto result
    = planner_.Build(*runtime_state_.captured_seam, previous_frame_for_planner,
      runtime_state_.cache_data_state, runtime_state_.page_requests);
  runtime_state_.current_plan = result.plan;
  runtime_state_.planned_snapshot = result.snapshot;
  ApplyRetainedEntryContinuity(*runtime_state_.captured_seam,
    runtime_state_.current_plan, *runtime_state_.planned_snapshot);
  runtime_state_.build_state = VsmCacheBuildState::kPlanned;

  DLOG_F(2,
    "built allocation plan generation={} request_count={} decisions={} "
    "reused={} allocated={} initialized={} evicted={} rejected={}",
    runtime_state_.captured_seam->current_frame.frame_generation,
    runtime_state_.page_requests.size(),
    runtime_state_.current_plan.decisions.size(),
    runtime_state_.current_plan.reused_page_count,
    runtime_state_.current_plan.allocated_page_count,
    runtime_state_.current_plan.initialized_page_count,
    runtime_state_.current_plan.evicted_page_count,
    runtime_state_.current_plan.rejected_page_count);

  return runtime_state_.current_plan;
}

auto VsmCacheManager::CommitPageAllocationFrame()
  -> const VsmPageAllocationFrame&
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kPlanned,
    "CommitPageAllocationFrame requires planned state");
  CHECK_F(runtime_state_.captured_seam.has_value(),
    "CommitPageAllocationFrame requires a captured seam");
  CHECK_F(runtime_state_.planned_snapshot.has_value(),
    "CommitPageAllocationFrame requires a planned snapshot");

  auto snapshot = *runtime_state_.planned_snapshot;
  const auto& resources = EnsureWorkingSetResources(snapshot);
  runtime_state_.current_frame = VsmPageAllocationFrame {
    .snapshot = std::move(snapshot),
    .plan = runtime_state_.current_plan,
    .physical_page_meta_buffer
    = runtime_state_.captured_seam->physical_pool.metadata_buffer,
    .page_table_buffer = resources.page_table_buffer,
    .page_flags_buffer = resources.page_flags_buffer,
    .dirty_flags_buffer = resources.dirty_flags_buffer,
    .physical_page_list_buffer = resources.physical_page_list_buffer,
    .page_rect_bounds_buffer = resources.page_rect_bounds_buffer,
    .is_ready = true,
  };
  runtime_state_.build_state = VsmCacheBuildState::kReady;

  DLOG_F(2,
    "committed allocation frame generation={} page_table_entries={} "
    "physical_pages={} light_entries={}",
    runtime_state_.current_frame->snapshot.frame_generation,
    runtime_state_.current_frame->snapshot.page_table.size(),
    runtime_state_.current_frame->snapshot.physical_pages.size(),
    runtime_state_.current_frame->snapshot.light_cache_entries.size());

  return *runtime_state_.current_frame;
}

auto VsmCacheManager::PublishVisibleShadowPrimitives(
  const std::span<const VsmPrimitiveIdentity> primitives) -> void
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kReady,
    "PublishVisibleShadowPrimitives requires ready state");
  CHECK_F(runtime_state_.current_frame.has_value(),
    "PublishVisibleShadowPrimitives requires a committed current frame");

  auto& published
    = runtime_state_.current_frame->snapshot.visible_shadow_primitives;
  published.assign(primitives.begin(), primitives.end());

  DLOG_F(2, "published visible shadow primitives generation={} count={}",
    runtime_state_.current_frame->snapshot.frame_generation, published.size());
}

auto VsmCacheManager::PublishRenderedPrimitiveHistory(
  const std::span<const VsmRenderedPrimitiveHistoryRecord> history) -> void
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kReady,
    "PublishRenderedPrimitiveHistory requires ready state");
  CHECK_F(runtime_state_.current_frame.has_value(),
    "PublishRenderedPrimitiveHistory requires a committed current frame");

  auto& published
    = runtime_state_.current_frame->snapshot.rendered_primitive_history;
  published.assign(history.begin(), history.end());

  DLOG_F(2, "published rendered primitive history generation={} count={}",
    runtime_state_.current_frame->snapshot.frame_generation, published.size());
}

auto VsmCacheManager::PublishStaticPrimitivePageFeedback(
  const std::span<const VsmStaticPrimitivePageFeedbackRecord> feedback) -> void
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kReady,
    "PublishStaticPrimitivePageFeedback requires ready state");
  CHECK_F(runtime_state_.current_frame.has_value(),
    "PublishStaticPrimitivePageFeedback requires a committed current frame");

  auto& published
    = runtime_state_.current_frame->snapshot.static_primitive_page_feedback;
  published.assign(feedback.begin(), feedback.end());

  DLOG_F(2, "published static primitive/page feedback generation={} count={}",
    runtime_state_.current_frame->snapshot.frame_generation, published.size());
}

auto VsmCacheManager::PublishProjectionRecords(
  const std::span<const VsmPageRequestProjection> projection_records) -> void
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kReady,
    "PublishProjectionRecords requires ready state");
  CHECK_F(runtime_state_.current_frame.has_value(),
    "PublishProjectionRecords requires a committed current frame");

  auto& published = runtime_state_.current_frame->snapshot.projection_records;
  published.assign(projection_records.begin(), projection_records.end());

  DLOG_F(2, "published projection records generation={} count={}",
    runtime_state_.current_frame->snapshot.frame_generation, published.size());
}

auto VsmCacheManager::PublishPhysicalPageMetaSeedBuffer(
  std::shared_ptr<const graphics::Buffer> physical_page_meta_seed_buffer)
  -> void
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kReady,
    "PublishPhysicalPageMetaSeedBuffer requires ready state");
  CHECK_F(runtime_state_.current_frame.has_value(),
    "PublishPhysicalPageMetaSeedBuffer requires a committed current frame");

  runtime_state_.current_frame->physical_page_meta_seed_buffer
    = std::move(physical_page_meta_seed_buffer);

  DLOG_F(2,
    "published physical page metadata seed buffer generation={} has_seed={}",
    runtime_state_.current_frame->snapshot.frame_generation,
    runtime_state_.current_frame->physical_page_meta_seed_buffer != nullptr);
}

auto VsmCacheManager::PublishCurrentFrameHzbAvailability(
  const bool is_current_frame_hzb_data_available) -> void
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kReady,
    "PublishCurrentFrameHzbAvailability requires ready state");
  CHECK_F(runtime_state_.current_frame.has_value(),
    "PublishCurrentFrameHzbAvailability requires a committed current frame");

  runtime_state_.current_frame->snapshot.is_hzb_data_available
    = is_current_frame_hzb_data_available;

  DLOG_F(2,
    "published current-frame HZB availability generation={} available={}",
    runtime_state_.current_frame->snapshot.frame_generation,
    is_current_frame_hzb_data_available);
}

auto VsmCacheManager::BuildInvalidationWork(
  const std::span<const VsmPrimitiveInvalidationRecord> invalidations)
  -> const VsmInvalidationWorkload&
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kFrameOpen,
    "BuildInvalidationWork requires frame-open state");

  runtime_state_.prepared_invalidation_workload = {};

  if (!runtime_state_.previous_frame.has_value() || invalidations.empty()) {
    return runtime_state_.prepared_invalidation_workload;
  }

  const auto& previous = *runtime_state_.previous_frame;
  auto merged_invalidations = std::vector<VsmPrimitiveInvalidationRecord> {};
  merged_invalidations.reserve(invalidations.size());

  for (const auto& record : invalidations) {
    if (!IsUsableInvalidationSphere(record.world_bounding_sphere)) {
      LOG_F(WARNING,
        "skipping primitive invalidation with unusable world-bounding sphere "
        "transform_index={} generation={} submesh_index={}",
        record.primitive.transform_index, record.primitive.transform_generation,
        record.primitive.submesh_index);
      continue;
    }

    auto existing
      = std::ranges::find_if(merged_invalidations, [&record](const auto& item) {
          return item.primitive == record.primitive;
        });
    if (existing == merged_invalidations.end()) {
      merged_invalidations.push_back(record);
    } else {
      existing->scope = MergeInvalidationScope(existing->scope, record.scope);
      existing->is_removed = existing->is_removed || record.is_removed;
      existing->world_bounding_sphere = record.world_bounding_sphere;
    }

    if (record.is_removed) {
      const auto removed_it = std::ranges::find(
        runtime_state_.recently_removed_primitives, record.primitive);
      if (removed_it == runtime_state_.recently_removed_primitives.end()) {
        runtime_state_.recently_removed_primitives.push_back(record.primitive);
      }
    }
  }

  for (const auto& record : merged_invalidations) {
    auto candidate_maps
      = std::vector<std::pair<VsmVirtualShadowMapId, bool>> {};

    auto append_candidate = [&](const VsmVirtualShadowMapId map_id,
                              const bool matched_static_feedback) -> void {
      if (map_id == 0U) {
        return;
      }
      auto existing = std::ranges::find_if(candidate_maps,
        [map_id](const auto& item) { return item.first == map_id; });
      if (existing == candidate_maps.end()) {
        candidate_maps.emplace_back(map_id, matched_static_feedback);
      } else {
        existing->second = existing->second || matched_static_feedback;
      }
    };

    for (const auto& history : previous.rendered_primitive_history) {
      if (history.primitive == record.primitive) {
        append_candidate(history.map_id, false);
      }
    }
    if (record.scope != VsmCacheInvalidationScope::kDynamicOnly) {
      for (const auto& feedback : previous.static_primitive_page_feedback) {
        if (feedback.valid != 0U && feedback.primitive == record.primitive) {
          append_candidate(feedback.map_id, true);
        }
      }
    }

    for (const auto& [map_id, matched_static_feedback] : candidate_maps) {
      const auto projection_index = ResolveProjectionIndex(previous, map_id);
      if (!projection_index.has_value()) {
        continue;
      }

      auto& work_items
        = runtime_state_.prepared_invalidation_workload.work_items;
      auto existing = std::ranges::find_if(work_items, [&](const auto& item) {
        return item.primitive == record.primitive
          && item.projection_index == *projection_index;
      });
      if (existing == work_items.end()) {
        work_items.push_back(VsmInvalidationWorkItem {
          .primitive = record.primitive,
          .world_bounding_sphere = record.world_bounding_sphere,
          .projection_index = *projection_index,
          .scope = record.scope,
          .matched_static_feedback = matched_static_feedback,
        });
      } else {
        existing->scope = MergeInvalidationScope(existing->scope, record.scope);
        existing->matched_static_feedback
          = existing->matched_static_feedback || matched_static_feedback;
        existing->world_bounding_sphere = record.world_bounding_sphere;
      }
    }
  }

  std::ranges::sort(runtime_state_.prepared_invalidation_workload.work_items,
    [](const auto& lhs, const auto& rhs) {
      return std::tie(lhs.projection_index, lhs.primitive.transform_index,
               lhs.primitive.transform_generation, lhs.primitive.submesh_index,
               lhs.primitive.primitive_flags)
        < std::tie(rhs.projection_index, rhs.primitive.transform_index,
          rhs.primitive.transform_generation, rhs.primitive.submesh_index,
          rhs.primitive.primitive_flags);
    });

  DLOG_F(2,
    "prepared invalidation workload requests={} work_items={} "
    "recently_removed={}",
    merged_invalidations.size(),
    runtime_state_.prepared_invalidation_workload.work_items.size(),
    runtime_state_.recently_removed_primitives.size());

  return runtime_state_.prepared_invalidation_workload;
}

auto VsmCacheManager::AbortFrame() -> void
{
  if (runtime_state_.build_state == VsmCacheBuildState::kIdle) {
    return;
  }

  const auto aborted_generation = runtime_state_.captured_seam.has_value()
    ? runtime_state_.captured_seam->current_frame.frame_generation
    : 0ULL;
  ClearInFlightFrameState();
  runtime_state_.build_state = VsmCacheBuildState::kIdle;
  runtime_state_.invalidation_reason
    = VsmCacheInvalidationReason::kExplicitReset;

  if (runtime_state_.previous_frame.has_value()) {
    runtime_state_.cache_data_state = VsmCacheDataState::kInvalidated;
    runtime_state_.is_hzb_data_available = false;
  } else {
    MarkCacheDataUnavailable();
  }

  DLOG_F(2,
    "aborted cache frame generation={} cache_state={} build_state={} "
    "hzb_available={} debug_name=`{}`",
    aborted_generation, runtime_state_.cache_data_state,
    runtime_state_.build_state, runtime_state_.is_hzb_data_available,
    config_.debug_name);
}

auto VsmCacheManager::QueueFrameExtraction(graphics::CommandRecorder& recorder)
  -> void
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kReady,
    "QueueFrameExtraction requires ready state");
  CHECK_F(runtime_state_.current_frame.has_value(),
    "QueueFrameExtraction requires a committed current frame");
  CHECK_F(!runtime_state_.pending_frame_extraction.has_value(),
    "QueueFrameExtraction requires no pending extraction");

  auto extracted_snapshot = runtime_state_.current_frame->snapshot;
  if (runtime_state_.captured_seam.has_value()
    && runtime_state_.previous_frame.has_value()) {
    CarryForwardContinuityPublications(*runtime_state_.captured_seam,
      *runtime_state_.previous_frame, extracted_snapshot);
  }
  ClearTransientInvalidationFlags(extracted_snapshot);
  auto queued_readback = std::optional<PendingFrameExtraction> {};

  const auto readback_manager = gfx_ != nullptr
    ? gfx_->GetReadbackManager()
    : observer_ptr<graphics::ReadbackManager> {};
  if (readback_manager != nullptr
    && runtime_state_.current_frame->physical_page_meta_buffer != nullptr
    && runtime_state_.current_frame->page_table_buffer != nullptr) {
    queued_readback = PendingFrameExtraction {
      .physical_page_meta_readback
      = readback_manager->CreateBufferReadback(BuildWorkingSetDebugName(
        runtime_state_.frame_config, "PhysicalMeta.Extract")),
      .physical_page_count = extracted_snapshot.physical_pages.size(),
      .page_table_readback
      = readback_manager->CreateBufferReadback(BuildWorkingSetDebugName(
        runtime_state_.frame_config, "PageTable.Extract")),
      .page_table_entry_count = extracted_snapshot.page_table.size(),
    };

    CHECK_NOTNULL_F(queued_readback->physical_page_meta_readback.get(),
      "QueueFrameExtraction requires physical metadata readback object");
    CHECK_NOTNULL_F(queued_readback->page_table_readback.get(),
      "QueueFrameExtraction requires page-table readback object");

    recorder.BeginTrackingResourceState(
      *runtime_state_.current_frame->physical_page_meta_buffer,
      graphics::ResourceStates::kCommon, true);
    const auto physical_range = graphics::BufferRange { 0U,
      static_cast<std::uint64_t>(queued_readback->physical_page_count)
        * sizeof(VsmPhysicalPageMeta) };
    const auto physical_ticket
      = queued_readback->physical_page_meta_readback->EnqueueCopy(recorder,
        *runtime_state_.current_frame->physical_page_meta_buffer,
        physical_range);
    CHECK_F(physical_ticket.has_value(),
      "QueueFrameExtraction failed to enqueue physical metadata readback "
      "error={}",
      physical_ticket.error());
    queued_readback->physical_page_meta_ticket = *physical_ticket;

    recorder.BeginTrackingResourceState(
      *runtime_state_.current_frame->page_table_buffer,
      graphics::ResourceStates::kCommon, true);
    const auto page_table_range = graphics::BufferRange { 0U,
      static_cast<std::uint64_t>(queued_readback->page_table_entry_count)
        * sizeof(VsmShaderPageTableEntry) };
    const auto page_table_ticket
      = queued_readback->page_table_readback->EnqueueCopy(recorder,
        *runtime_state_.current_frame->page_table_buffer, page_table_range);
    CHECK_F(page_table_ticket.has_value(),
      "QueueFrameExtraction failed to enqueue page-table readback error={}",
      page_table_ticket.error());
    queued_readback->page_table_ticket = *page_table_ticket;
  }

  runtime_state_.previous_frame = std::move(extracted_snapshot);
  runtime_state_.pending_frame_extraction = std::move(queued_readback);
  runtime_state_.cache_data_state = VsmCacheDataState::kAvailable;
  runtime_state_.is_hzb_data_available
    = runtime_state_.previous_frame->is_hzb_data_available;
  runtime_state_.invalidation_reason = VsmCacheInvalidationReason::kNone;
  ClearInFlightFrameState();
  runtime_state_.build_state = VsmCacheBuildState::kIdle;

  DLOG_F(2,
    "queued cache frame extraction generation={} pool_identity={} "
    "pending_gpu_readback={} hzb_available={}",
    runtime_state_.previous_frame->frame_generation,
    runtime_state_.previous_frame->pool_identity,
    runtime_state_.pending_frame_extraction.has_value(),
    runtime_state_.is_hzb_data_available);
}

auto VsmCacheManager::ExtractFrameData() -> void
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kReady,
    "ExtractFrameData requires ready state");
  CHECK_F(runtime_state_.current_frame.has_value(),
    "ExtractFrameData requires a committed current frame");

  auto extracted_snapshot = runtime_state_.current_frame->snapshot;
  if (runtime_state_.captured_seam.has_value()
    && runtime_state_.previous_frame.has_value()) {
    CarryForwardContinuityPublications(*runtime_state_.captured_seam,
      *runtime_state_.previous_frame, extracted_snapshot);
  }
  static_cast<void>(TrySynchronizeSnapshotFromGpu(
    *runtime_state_.current_frame, extracted_snapshot));
  ClearTransientInvalidationFlags(extracted_snapshot);

  runtime_state_.previous_frame = std::move(extracted_snapshot);
  runtime_state_.cache_data_state = VsmCacheDataState::kAvailable;
  runtime_state_.is_hzb_data_available
    = runtime_state_.previous_frame->is_hzb_data_available;
  runtime_state_.invalidation_reason = VsmCacheInvalidationReason::kNone;
  ClearInFlightFrameState();
  runtime_state_.build_state = VsmCacheBuildState::kIdle;

  DLOG_F(2,
    "extracted cache frame generation={} pool_identity={} hzb_available={}",
    runtime_state_.previous_frame->frame_generation,
    runtime_state_.previous_frame->pool_identity,
    runtime_state_.is_hzb_data_available);
}

auto VsmCacheManager::InvalidateAll(const VsmCacheInvalidationReason reason)
  -> void
{
  runtime_state_.invalidation_reason = reason;
  runtime_state_.is_hzb_data_available = false;
  runtime_state_.pending_invalidations.clear();

  if (runtime_state_.previous_frame.has_value()) {
    runtime_state_.cache_data_state = VsmCacheDataState::kInvalidated;
  } else {
    runtime_state_.cache_data_state = VsmCacheDataState::kUnavailable;
  }

  DLOG_F(2, "invalidated cache data reason={} cache_state={}",
    nostd::to_string(reason),
    nostd::to_string(runtime_state_.cache_data_state));
}

auto VsmCacheManager::InvalidateLocalLights(const VsmRemapKeyList& remap_keys,
  const VsmCacheInvalidationScope scope,
  const VsmCacheInvalidationReason reason) -> void
{
  QueueTargetedInvalidation(
    VsmLightCacheKind::kLocal, remap_keys, scope, reason);
}

auto VsmCacheManager::InvalidateDirectionalClipmaps(
  const VsmRemapKeyList& remap_keys, const VsmCacheInvalidationScope scope,
  const VsmCacheInvalidationReason reason) -> void
{
  QueueTargetedInvalidation(
    VsmLightCacheKind::kDirectional, remap_keys, scope, reason);
}

auto VsmCacheManager::DescribeCacheDataState() const noexcept
  -> VsmCacheDataState
{
  return runtime_state_.cache_data_state;
}

auto VsmCacheManager::DescribeBuildState() const noexcept -> VsmCacheBuildState
{
  return runtime_state_.build_state;
}

auto VsmCacheManager::IsCacheDataAvailable() const noexcept -> bool
{
  return runtime_state_.cache_data_state == VsmCacheDataState::kAvailable;
}

auto VsmCacheManager::IsHzbDataAvailable() const noexcept -> bool
{
  return runtime_state_.is_hzb_data_available;
}

auto VsmCacheManager::GetCurrentFrame() const noexcept
  -> const VsmPageAllocationFrame*
{
  return runtime_state_.current_frame ? &*runtime_state_.current_frame
                                      : nullptr;
}

auto VsmCacheManager::GetPreviousFrame() const noexcept
  -> const VsmExtractedCacheFrame*
{
  return runtime_state_.previous_frame ? &*runtime_state_.previous_frame
                                       : nullptr;
}

auto VsmCacheManager::IsFrameShapeCompatible(
  const VsmCacheManagerSeam& seam) const noexcept -> bool
{
  static_cast<void>(seam);

  if (!runtime_state_.previous_frame.has_value()) {
    return false;
  }

  const auto& previous = *runtime_state_.previous_frame;
  // Current publication may append retained-entry continuity products, so raw
  // captured-seam page-table-count changes are not a compatibility boundary.
  if (previous.page_table.size()
    < previous.virtual_frame.total_page_table_entry_count) {
    return false;
  }
  if (previous.physical_pages.size() != previous.pool_tile_capacity) {
    return false;
  }

  return true;
}

auto VsmCacheManager::ComputeHzbAvailability(
  const VsmCacheManagerSeam& seam) const noexcept -> bool
{
  if (!runtime_state_.previous_frame.has_value()) {
    return false;
  }

  const auto& prev = *runtime_state_.previous_frame;
  return prev.is_hzb_data_available && seam.hzb_pool.is_available
    && prev.hzb_pool_width == seam.hzb_pool.width
    && prev.hzb_pool_height == seam.hzb_pool.height
    && prev.hzb_pool_mip_count == seam.hzb_pool.mip_count
    && prev.hzb_pool_format == seam.hzb_pool.format;
}

auto VsmCacheManager::QueueTargetedInvalidation(const VsmLightCacheKind kind,
  const VsmRemapKeyList& remap_keys, const VsmCacheInvalidationScope scope,
  const VsmCacheInvalidationReason reason) -> void
{
  CHECK_F(runtime_state_.build_state == VsmCacheBuildState::kIdle,
    "targeted invalidation requires idle build state");

  if (reason == VsmCacheInvalidationReason::kNone) {
    LOG_F(
      WARNING, "rejecting targeted invalidation because the reason is None");
    return;
  }
  if (!runtime_state_.previous_frame.has_value()) {
    LOG_F(WARNING,
      "ignoring targeted invalidation because no extracted cache frame is "
      "available");
    return;
  }
  if (runtime_state_.cache_data_state != VsmCacheDataState::kAvailable) {
    LOG_F(WARNING,
      "ignoring targeted invalidation because cache data is not available "
      "cache_state={}",
      nostd::to_string(runtime_state_.cache_data_state));
    return;
  }
  const auto validation = Validate(remap_keys);
  if (validation != VsmRemapKeyListValidationResult::kValid) {
    LOG_F(WARNING,
      "rejecting targeted invalidation because remap_keys are invalid "
      "validation={} kind={} reason={}",
      nostd::to_string(validation), nostd::to_string(kind),
      nostd::to_string(reason));
    return;
  }

  runtime_state_.pending_invalidations.push_back(PendingInvalidation {
    .kind = kind,
    .remap_keys = remap_keys,
    .scope = scope,
    .reason = reason,
  });
  runtime_state_.invalidation_reason = reason;
  runtime_state_.is_hzb_data_available = false;

  DLOG_F(2,
    "queued targeted invalidation kind={} scope={} reason={} remap_keys={}",
    nostd::to_string(kind), nostd::to_string(scope), nostd::to_string(reason),
    remap_keys.size());
}

auto VsmCacheManager::ApplyPendingInvalidations(
  VsmExtractedCacheFrame& snapshot) const -> void
{
  auto total_stats = AppliedInvalidationStats {};

  for (const auto& invalidation : runtime_state_.pending_invalidations) {
    auto requested_keys = std::unordered_set<std::string_view> {};
    requested_keys.reserve(invalidation.remap_keys.size());
    for (const auto& remap_key : invalidation.remap_keys) {
      requested_keys.insert(remap_key);
    }

    for (const auto& entry : snapshot.light_cache_entries) {
      if (entry.kind != invalidation.kind
        || !requested_keys.contains(entry.remap_key)) {
        continue;
      }

      ++total_stats.matched_entries;

      const auto first_entry = entry.current_frame_state.first_page_table_entry;
      const auto entry_count = entry.current_frame_state.page_table_entry_count;
      CHECK_F(first_entry + entry_count <= snapshot.page_table.size(),
        "targeted invalidation requires cache-entry page-table ranges to stay "
        "within the extracted snapshot");

      for (std::uint32_t i = 0; i < entry_count; ++i) {
        const auto& page_table_entry = snapshot.page_table[first_entry + i];
        if (!page_table_entry.is_mapped) {
          continue;
        }

        ++total_stats.marked_pages;
        ApplyInvalidationScope(
          snapshot.physical_pages[page_table_entry.physical_page.value],
          invalidation.scope, total_stats);
      }
    }
  }

  if (total_stats.matched_entries > 0
    || !runtime_state_.pending_invalidations.empty()) {
    DLOG_F(2,
      "applied targeted invalidations requests={} matched_entries={} "
      "marked_pages={} static_marks={} dynamic_marks={}",
      runtime_state_.pending_invalidations.size(), total_stats.matched_entries,
      total_stats.marked_pages, total_stats.static_marks,
      total_stats.dynamic_marks);
  }
}

auto VsmCacheManager::ApplyRetainedEntryContinuity(
  const VsmCacheManagerSeam& seam, VsmPageAllocationPlan& plan,
  VsmPageAllocationSnapshot& snapshot) -> void
{
  if (!runtime_state_.previous_frame.has_value()
    || runtime_state_.cache_data_state != VsmCacheDataState::kAvailable
    || config_.retained_unreferenced_frame_count == 0) {
    return;
  }

  const auto& previous_snapshot = *runtime_state_.previous_frame;
  auto next_virtual_id = FindMaxVirtualId(seam.current_frame) + 1U;
  auto next_page_table_entry
    = static_cast<std::uint32_t>(snapshot.page_table.size());
  auto retained_physical_pages = std::unordered_set<std::uint32_t> {};
  auto retained_entry_count = std::uint32_t { 0 };

  for (const auto& previous_entry : previous_snapshot.light_cache_entries) {
    if (IsRemapKeyReferenced(seam.current_frame, previous_entry.remap_key)) {
      continue;
    }

    const auto retained_age
      = ComputeRetainedEntryAge(previous_snapshot, previous_entry);
    if (retained_age == 0
      || retained_age > config_.retained_unreferenced_frame_count) {
      continue;
    }

    const auto layout_ref
      = FindRetainableLayout(previous_snapshot, previous_entry);
    if (layout_ref.local == nullptr && layout_ref.directional == nullptr) {
      LOG_F(WARNING,
        "skipping retained entry remap_key=`{}` because the extracted "
        "snapshot does not publish a matching retainable layout",
        previous_entry.remap_key);
      continue;
    }

    auto retained_entry = previous_entry;
    retained_entry.previous_frame_state = previous_entry.current_frame_state;
    retained_entry.current_frame_state.scheduled_frame
      = snapshot.frame_generation;
    retained_entry.current_frame_state.is_retained_unreferenced = true;

    const auto previous_first_entry
      = previous_entry.current_frame_state.first_page_table_entry;
    const auto previous_entry_count
      = previous_entry.current_frame_state.page_table_entry_count;
    CHECK_F(previous_first_entry + previous_entry_count
        <= previous_snapshot.page_table.size(),
      "retained entry page-table range must stay within the extracted "
      "snapshot");

    std::uint32_t retained_page_count = 0;
    if (layout_ref.local != nullptr) {
      auto retained_layout = *layout_ref.local;
      retained_layout.id = next_virtual_id++;
      retained_layout.first_page_table_entry = next_page_table_entry;
      snapshot.retained_local_light_layouts.push_back(retained_layout);

      retained_entry.current_frame_state.virtual_map_id = retained_layout.id;
      retained_entry.current_frame_state.first_page_table_entry
        = retained_layout.first_page_table_entry;
      retained_entry.current_frame_state.page_table_entry_count
        = retained_layout.total_page_count;
      next_page_table_entry += retained_layout.total_page_count;
      snapshot.page_table.resize(next_page_table_entry);

      for (std::uint32_t i = 0; i < retained_layout.total_page_count; ++i) {
        const auto& previous_mapping
          = previous_snapshot.page_table[previous_first_entry + i];
        if (!previous_mapping.is_mapped) {
          continue;
        }

        snapshot.page_table[retained_layout.first_page_table_entry + i]
          = previous_mapping;
        auto physical_page = previous_snapshot.physical_pages.at(
          previous_mapping.physical_page.value);
        physical_page.owner_id = retained_layout.id;
        physical_page.age = retained_age;
        physical_page.used_this_frame = false;
        physical_page.last_touched_frame = snapshot.frame_generation;
        snapshot.physical_pages[previous_mapping.physical_page.value]
          = physical_page;
        retained_physical_pages.insert(previous_mapping.physical_page.value);
        ++retained_page_count;
      }
    } else {
      auto retained_layout = *layout_ref.directional;
      retained_layout.first_id = next_virtual_id;
      retained_layout.first_page_table_entry = next_page_table_entry;
      snapshot.retained_directional_layouts.push_back(retained_layout);

      retained_entry.current_frame_state.virtual_map_id
        = retained_layout.first_id;
      retained_entry.current_frame_state.first_page_table_entry
        = retained_layout.first_page_table_entry;
      retained_entry.current_frame_state.page_table_entry_count
        = TotalPageCount(retained_layout);
      next_virtual_id += retained_layout.clip_level_count;
      next_page_table_entry += TotalPageCount(retained_layout);
      snapshot.page_table.resize(next_page_table_entry);

      for (std::uint32_t i = 0; i < TotalPageCount(retained_layout); ++i) {
        const auto& previous_mapping
          = previous_snapshot.page_table[previous_first_entry + i];
        if (!previous_mapping.is_mapped) {
          continue;
        }

        snapshot.page_table[retained_layout.first_page_table_entry + i]
          = previous_mapping;
        auto physical_page = previous_snapshot.physical_pages.at(
          previous_mapping.physical_page.value);
        physical_page.owner_id
          = retained_layout.first_id + physical_page.owner_page.level;
        physical_page.age = retained_age;
        physical_page.used_this_frame = false;
        physical_page.last_touched_frame = snapshot.frame_generation;
        snapshot.physical_pages[previous_mapping.physical_page.value]
          = physical_page;
        retained_physical_pages.insert(previous_mapping.physical_page.value);
        ++retained_page_count;
      }
    }

    if (retained_page_count == 0) {
      if (layout_ref.local != nullptr) {
        snapshot.retained_local_light_layouts.pop_back();
      } else {
        snapshot.retained_directional_layouts.pop_back();
      }
      snapshot.page_table.resize(next_page_table_entry - previous_entry_count);
      next_page_table_entry -= previous_entry_count;
      continue;
    }

    snapshot.light_cache_entries.push_back(std::move(retained_entry));
    ++retained_entry_count;
  }

  RemoveEvictionsForRetainedPages(plan, retained_physical_pages);

  if (retained_entry_count > 0) {
    DLOG_F(2,
      "published retained-entry continuity generation={} retained_entries={} "
      "retained_physical_pages={}",
      snapshot.frame_generation, retained_entry_count,
      retained_physical_pages.size());
  }
}

auto VsmCacheManager::FinalizePendingFrameExtraction() -> void
{
  if (!runtime_state_.pending_frame_extraction.has_value()) {
    return;
  }

  CHECK_F(runtime_state_.previous_frame.has_value(),
    "pending frame extraction requires an extracted snapshot");
  CHECK_NOTNULL_F(gfx_, "pending frame extraction requires a graphics device");

  const auto readback_manager = gfx_->GetReadbackManager();
  CHECK_NOTNULL_F(readback_manager.get(),
    "pending frame extraction requires ReadbackManager");

  auto& pending = *runtime_state_.pending_frame_extraction;
  auto& snapshot = *runtime_state_.previous_frame;

  const auto physical_bytes = AwaitBufferReadback(*readback_manager,
    pending.physical_page_meta_readback, pending.physical_page_meta_ticket,
    pending.physical_page_count * sizeof(VsmPhysicalPageMeta),
    "VsmCacheManager.ExtractFrameData.PhysicalMeta");
  snapshot.physical_pages = DeserializeBufferReadback<VsmPhysicalPageMeta>(
    physical_bytes, pending.physical_page_count,
    "VsmCacheManager.ExtractFrameData.PhysicalMeta");
  ClearTransientInvalidationFlags(snapshot);

  const auto page_table_bytes = AwaitBufferReadback(*readback_manager,
    pending.page_table_readback, pending.page_table_ticket,
    pending.page_table_entry_count * sizeof(VsmShaderPageTableEntry),
    "VsmCacheManager.ExtractFrameData.PageTable");
  const auto shader_page_table
    = DeserializeBufferReadback<VsmShaderPageTableEntry>(page_table_bytes,
      pending.page_table_entry_count,
      "VsmCacheManager.ExtractFrameData.PageTable");
  snapshot.page_table.resize(shader_page_table.size());
  for (std::size_t i = 0; i < shader_page_table.size(); ++i) {
    snapshot.page_table[i] = {
      .is_mapped = IsMapped(shader_page_table[i]),
      .physical_page = DecodePhysicalPageIndex(shader_page_table[i]),
    };
  }

  runtime_state_.pending_frame_extraction.reset();

  DLOG_F(2,
    "finalized pending cache extraction generation={} physical_pages={} "
    "page_table_entries={}",
    snapshot.frame_generation, snapshot.physical_pages.size(),
    snapshot.page_table.size());
}

auto VsmCacheManager::TrySynchronizeSnapshotFromGpu(
  const VsmPageAllocationFrame& frame,
  VsmPageAllocationSnapshot& snapshot) const -> bool
{
  if (gfx_ == nullptr || frame.physical_page_meta_buffer == nullptr
    || frame.page_table_buffer == nullptr) {
    return false;
  }

  const auto readback_manager = gfx_->GetReadbackManager();
  if (readback_manager == nullptr) {
    return false;
  }

  const auto physical_bytes
    = readback_manager->ReadBufferNow(*frame.physical_page_meta_buffer,
      graphics::BufferRange { 0U,
        static_cast<std::uint64_t>(snapshot.physical_pages.size())
          * sizeof(VsmPhysicalPageMeta) });
  if (!physical_bytes.has_value()) {
    LOG_F(WARNING,
      "failed to read back current-frame physical metadata during extraction "
      "generation={} error={}",
      snapshot.frame_generation, physical_bytes.error());
    return false;
  }
  snapshot.physical_pages = DeserializeBufferReadback<VsmPhysicalPageMeta>(
    *physical_bytes, snapshot.physical_pages.size(),
    "VsmCacheManager.ExtractFrameData.PhysicalMeta.Now");

  const auto page_table_bytes
    = readback_manager->ReadBufferNow(*frame.page_table_buffer,
      graphics::BufferRange { 0U,
        static_cast<std::uint64_t>(snapshot.page_table.size())
          * sizeof(VsmShaderPageTableEntry) });
  if (!page_table_bytes.has_value()) {
    LOG_F(WARNING,
      "failed to read back current-frame page table during extraction "
      "generation={} error={}",
      snapshot.frame_generation, page_table_bytes.error());
    return false;
  }

  const auto shader_page_table
    = DeserializeBufferReadback<VsmShaderPageTableEntry>(*page_table_bytes,
      snapshot.page_table.size(),
      "VsmCacheManager.ExtractFrameData.PageTable.Now");
  snapshot.page_table.resize(shader_page_table.size());
  for (std::size_t i = 0; i < shader_page_table.size(); ++i) {
    snapshot.page_table[i] = {
      .is_mapped = IsMapped(shader_page_table[i]),
      .physical_page = DecodePhysicalPageIndex(shader_page_table[i]),
    };
  }

  return true;
}

auto VsmCacheManager::EnsureWorkingSetResources(
  const VsmPageAllocationSnapshot& snapshot) -> const FrameWorkingSetResources&
{
  if (gfx_ == nullptr) {
    DLOG_F(3,
      "skipping page-allocation working-set creation because graphics is null");
    runtime_state_.working_set_resources = {};
    return runtime_state_.working_set_resources;
  }

  const auto page_table_entry_capacity = snapshot.page_table.size();
  const auto physical_page_capacity = snapshot.physical_pages.size();
  const auto is_compatible
    = runtime_state_.working_set_resources.page_table_buffer != nullptr
    && runtime_state_.working_set_resources.page_table_entry_capacity
      == page_table_entry_capacity
    && runtime_state_.working_set_resources.physical_page_capacity
      == physical_page_capacity;
  if (is_compatible) {
    DLOG_F(3,
      "reusing page-allocation working set page_table_entries={} "
      "physical_pages={}",
      page_table_entry_capacity, physical_page_capacity);
    return runtime_state_.working_set_resources;
  }

  if (runtime_state_.working_set_resources.page_table_buffer != nullptr) {
    DLOG_F(2,
      "recreating page-allocation working set page_table_entries={} "
      "physical_pages={}",
      page_table_entry_capacity, physical_page_capacity);
  }
  ReleaseWorkingSetResources(gfx_, runtime_state_.working_set_resources);

  const auto page_table_entries_bytes
    = static_cast<std::uint64_t>(
        page_table_entry_capacity == 0 ? 1 : page_table_entry_capacity)
    * kPageTableEntryStrideBytes;
  const auto page_flags_bytes
    = static_cast<std::uint64_t>(
        page_table_entry_capacity == 0 ? 1 : page_table_entry_capacity)
    * kPageFlagsStrideBytes;
  const auto dirty_flags_bytes
    = static_cast<std::uint64_t>(
        physical_page_capacity == 0 ? 1 : physical_page_capacity)
    * kDirtyFlagsStrideBytes;
  const auto physical_page_list_bytes
    = static_cast<std::uint64_t>(
        physical_page_capacity == 0 ? 1 : physical_page_capacity)
    * kPhysicalPageListStrideBytes;
  const auto page_rect_bounds_bytes
    = static_cast<std::uint64_t>(
        page_table_entry_capacity == 0 ? 1 : page_table_entry_capacity)
    * kPageRectBoundsStrideBytes;

  auto resources = FrameWorkingSetResources {};
  resources.page_table_entry_capacity = page_table_entry_capacity;
  resources.physical_page_capacity = physical_page_capacity;
  resources.page_table_buffer
    = CreateWorkingSetBuffer(*gfx_, page_table_entries_bytes,
      BuildWorkingSetDebugName(runtime_state_.frame_config, "PageTable"));
  RegisterResourceIfNeeded(*gfx_, resources.page_table_buffer);
  resources.page_flags_buffer = CreateWorkingSetBuffer(*gfx_, page_flags_bytes,
    BuildWorkingSetDebugName(runtime_state_.frame_config, "PageFlags"));
  RegisterResourceIfNeeded(*gfx_, resources.page_flags_buffer);
  resources.dirty_flags_buffer
    = CreateWorkingSetBuffer(*gfx_, dirty_flags_bytes,
      BuildWorkingSetDebugName(runtime_state_.frame_config, "DirtyFlags"));
  RegisterResourceIfNeeded(*gfx_, resources.dirty_flags_buffer);
  resources.physical_page_list_buffer = CreateWorkingSetBuffer(*gfx_,
    physical_page_list_bytes,
    BuildWorkingSetDebugName(runtime_state_.frame_config, "PhysicalPageList"));
  RegisterResourceIfNeeded(*gfx_, resources.physical_page_list_buffer);
  resources.page_rect_bounds_buffer
    = CreateWorkingSetBuffer(*gfx_, page_rect_bounds_bytes,
      BuildWorkingSetDebugName(runtime_state_.frame_config, "PageRectBounds"));
  RegisterResourceIfNeeded(*gfx_, resources.page_rect_bounds_buffer);

  DLOG_F(2,
    "created page-allocation working set page_table_entries={} "
    "physical_pages={}",
    page_table_entry_capacity, physical_page_capacity);

  runtime_state_.working_set_resources = std::move(resources);
  return runtime_state_.working_set_resources;
}

auto VsmCacheManager::ClearInFlightFrameState() -> void
{
  runtime_state_.captured_seam.reset();
  runtime_state_.frame_config = {};
  runtime_state_.page_requests.clear();
  runtime_state_.current_plan = {};
  runtime_state_.planned_snapshot.reset();
  runtime_state_.prepared_invalidation_workload = {};
  runtime_state_.recently_removed_primitives.clear();
  runtime_state_.current_frame.reset();
}

auto VsmCacheManager::MarkCacheDataUnavailable() noexcept -> void
{
  runtime_state_.cache_data_state = VsmCacheDataState::kUnavailable;
  runtime_state_.is_hzb_data_available = false;
}

} // namespace oxygen::renderer::vsm
