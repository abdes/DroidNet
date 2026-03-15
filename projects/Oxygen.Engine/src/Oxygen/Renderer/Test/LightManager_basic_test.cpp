//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <deque>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/Internal/ShadowBackendCommon.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Types/VirtualShadowPageFlags.h>
#include <Oxygen/Renderer/Types/VirtualShadowPageTableEntry.h>
#include <Oxygen/Renderer/Types/VirtualShadowPhysicalPageMetadata.h>
#include <Oxygen/Renderer/Types/VirtualShadowRequestFeedback.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>


namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal

namespace {

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::observer_ptr;
using oxygen::engine::DirectionalLightFlags;
using oxygen::engine::ViewConstants;
using oxygen::engine::upload::DefaultUploadPolicy;
using oxygen::engine::upload::InlineTransfersCoordinator;
using oxygen::engine::upload::StagingProvider;
using oxygen::engine::upload::UploadCoordinator;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::SingleQueueStrategy;
using oxygen::renderer::LightManager;
using oxygen::renderer::ShadowManager;
using oxygen::renderer::internal::RendererTagFactory;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
auto ResolveVirtualViewIntrospection(
  ShadowManager& shadow_manager, const oxygen::ViewId view_id)
  -> const oxygen::renderer::VirtualShadowViewIntrospection*
{
  shadow_manager.ResolveVirtualCurrentFrame(view_id);
  return shadow_manager.TryGetVirtualViewIntrospection(view_id);
}
auto ResolveAndPublishVirtualViewIntrospection(
  ShadowManager& shadow_manager, const oxygen::ViewId view_id)
  -> const oxygen::renderer::VirtualShadowViewIntrospection*
{
  shadow_manager.ResolveVirtualCurrentFrame(view_id);
  const auto* pre_publish_introspection
    = shadow_manager.TryGetVirtualViewIntrospection(view_id);
  const bool rendered_page_work = pre_publish_introspection != nullptr
    && pre_publish_introspection->pending_raster_page_count > 0U;
  shadow_manager.MarkVirtualRasterExecuted(view_id, rendered_page_work);
  return shadow_manager.TryGetVirtualViewIntrospection(view_id);
}

struct VirtualFeedbackLayout {
  std::uint32_t clip_level_count { 0U };
  std::uint32_t pages_per_axis { 0U };
  std::uint32_t pages_per_level { 0U };
  std::uint64_t directional_address_space_hash { 0U };
  std::array<std::int32_t, oxygen::engine::kMaxVirtualDirectionalClipLevels>
    clip_grid_origin_x {};
  std::array<std::int32_t, oxygen::engine::kMaxVirtualDirectionalClipLevels>
    clip_grid_origin_y {};
};

auto BuildVirtualFeedbackLayout(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata)
  -> VirtualFeedbackLayout
{
  VirtualFeedbackLayout layout {
    .clip_level_count = metadata.clip_level_count,
    .pages_per_axis = metadata.pages_per_axis,
    .pages_per_level = metadata.pages_per_axis * metadata.pages_per_axis,
    .directional_address_space_hash = oxygen::renderer::internal::
      shadow_detail::HashDirectionalVirtualFeedbackAddressSpace(metadata),
  };
  for (std::uint32_t clip_index = 0U;
    clip_index < std::min(metadata.clip_level_count,
      oxygen::engine::kMaxVirtualDirectionalClipLevels);
    ++clip_index) {
    layout.clip_grid_origin_x[clip_index]
      = oxygen::renderer::internal::shadow_detail::
        ResolveDirectionalVirtualClipGridOriginX(metadata, clip_index);
    layout.clip_grid_origin_y[clip_index]
      = oxygen::renderer::internal::shadow_detail::
        ResolveDirectionalVirtualClipGridOriginY(metadata, clip_index);
  }
  return layout;
}

struct DerivedPendingRasterPage {
  std::uint32_t shadow_instance_index { 0xFFFFFFFFU };
  std::uint32_t payload_index { 0xFFFFFFFFU };
  std::uint32_t clip_level { 0U };
  std::uint32_t page_index { 0U };
  std::uint64_t resident_key { 0U };
  std::uint16_t atlas_tile_x { 0U };
  std::uint16_t atlas_tile_y { 0U };
};

struct DerivedPendingRasterState {
  const oxygen::graphics::Texture* depth_texture { nullptr };
  std::vector<DerivedPendingRasterPage> resolved_pages {};
  std::uint32_t page_size_texels { 0U };
  std::uint32_t atlas_tiles_per_axis { 0U };
};

// Test-side derivation of pending raster pages from live page-table/page-flag
// state. Runtime no longer exposes a CPU render plan for this.
auto DerivePendingRasterPages(
  const oxygen::renderer::VirtualShadowViewIntrospection& introspection)
  -> std::vector<DerivedPendingRasterPage>
{
  if (introspection.directional_virtual_metadata.empty()
    || introspection.page_table_entries.empty()
    || introspection.page_table_entries.size()
      != introspection.page_flags_entries.size()) {
    return {};
  }

  const auto& metadata = introspection.directional_virtual_metadata.front();
  const auto layout = BuildVirtualFeedbackLayout(metadata);
  if (layout.clip_level_count == 0U || layout.pages_per_axis == 0U
    || layout.pages_per_level == 0U) {
    return {};
  }

  std::vector<DerivedPendingRasterPage> resolved_pages;
  resolved_pages.reserve(introspection.pending_raster_page_count);

  for (std::size_t global_page_index = 0U;
    global_page_index < introspection.page_table_entries.size();
    ++global_page_index) {
    const auto decoded_entry = oxygen::renderer::DecodeVirtualShadowPageTableEntry(
      introspection.page_table_entries[global_page_index]);
    if (!decoded_entry.current_lod_valid) {
      continue;
    }

    const auto page_flags = introspection.page_flags_entries[global_page_index];
    const bool uncached = oxygen::renderer::HasVirtualShadowPageFlag(
                            page_flags,
                            oxygen::renderer::VirtualShadowPageFlag::kDynamicUncached)
      || oxygen::renderer::HasVirtualShadowPageFlag(
        page_flags, oxygen::renderer::VirtualShadowPageFlag::kStaticUncached);
    if (!uncached) {
      continue;
    }

    const auto clip_index
      = static_cast<std::uint32_t>(global_page_index / layout.pages_per_level);
    if (clip_index >= layout.clip_level_count) {
      continue;
    }

    const auto local_page_index
      = static_cast<std::uint32_t>(global_page_index % layout.pages_per_level);
    const auto page_y = local_page_index / layout.pages_per_axis;
    const auto page_x = local_page_index % layout.pages_per_axis;

    resolved_pages.push_back(DerivedPendingRasterPage {
      .shadow_instance_index = metadata.shadow_instance_index,
      .payload_index = 0U,
      .clip_level = clip_index,
      .page_index = local_page_index,
      .resident_key = oxygen::renderer::internal::shadow_detail::
        PackVirtualResidentPageKey(
          clip_index,
          layout.clip_grid_origin_x[clip_index] + static_cast<std::int32_t>(page_x),
          layout.clip_grid_origin_y[clip_index]
            + static_cast<std::int32_t>(page_y)),
      .atlas_tile_x = static_cast<std::uint16_t>(decoded_entry.tile_x),
      .atlas_tile_y = static_cast<std::uint16_t>(decoded_entry.tile_y),
    });
  }

  return resolved_pages;
}

auto ContainsResolvedPageResidentKey(
  const std::span<const DerivedPendingRasterPage> pages,
  const std::uint64_t resident_key) -> bool
{
  return std::ranges::any_of(
    pages, [resident_key](const auto& page) { return page.resident_key == resident_key; });
}

auto ResolveDerivedPendingRasterState(
  ShadowManager& shadow_manager, const oxygen::ViewId view_id)
  -> const DerivedPendingRasterState*
{
  shadow_manager.ResolveVirtualCurrentFrame(view_id);
  const auto* introspection = shadow_manager.TryGetVirtualViewIntrospection(view_id);
  const auto& depth_texture = shadow_manager.GetVirtualShadowDepthTexture();
  if (!depth_texture && introspection == nullptr) {
    return nullptr;
  }

  static thread_local std::deque<DerivedPendingRasterState> derived_states {};
  derived_states.emplace_back();
  auto& derived = derived_states.back();
  derived.depth_texture = depth_texture.get();
  if (introspection != nullptr) {
    derived.resolved_pages = DerivePendingRasterPages(*introspection);
    if (!introspection->directional_virtual_metadata.empty()) {
      const auto& metadata = introspection->directional_virtual_metadata.front();
      derived.page_size_texels = metadata.page_size_texels;
      if (depth_texture && metadata.page_size_texels > 0U) {
        derived.atlas_tiles_per_axis = std::max(1U,
          depth_texture->GetDescriptor().width / metadata.page_size_texels);
      }
    }
  }
  return &derived;
}

auto ResolveVirtualFeedbackLayout(ShadowManager& shadow_manager,
  LightManager& manager, const ViewConstants& view_constants,
  const oxygen::ViewId view_id, const std::span<const glm::vec4> shadow_casters,
  const ShadowManager::SyntheticSunShadowInput& synthetic_sun)
  -> VirtualFeedbackLayout
{
  const auto published = shadow_manager.PublishForView(view_id, view_constants,
    manager, shadow_casters, {}, &synthetic_sun, std::chrono::milliseconds(16));
  EXPECT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  const auto* introspection
    = ResolveVirtualViewIntrospection(shadow_manager, view_id);
  EXPECT_NE(introspection, nullptr);
  if (introspection == nullptr
    || introspection->directional_virtual_metadata.empty()) {
    return {};
  }

  return BuildVirtualFeedbackLayout(
    introspection->directional_virtual_metadata.front());
}

auto RequestedResidentKeys(const VirtualFeedbackLayout& layout,
  const std::span<const std::uint32_t> requested_page_indices)
  -> std::vector<std::uint64_t>
{
  std::vector<std::uint64_t> resident_keys {};
  resident_keys.reserve(requested_page_indices.size());
  for (const auto global_page_index : requested_page_indices) {
    if (layout.pages_per_level == 0U || layout.pages_per_axis == 0U) {
      continue;
    }

    const auto clip_index = global_page_index / layout.pages_per_level;
    if (clip_index >= layout.clip_level_count) {
      continue;
    }

    const auto local_page_index = global_page_index % layout.pages_per_level;
    const auto page_y = local_page_index / layout.pages_per_axis;
    const auto page_x = local_page_index % layout.pages_per_axis;
    resident_keys.push_back(
      oxygen::renderer::internal::shadow_detail::PackVirtualResidentPageKey(
        clip_index,
        layout.clip_grid_origin_x[clip_index]
          + static_cast<std::int32_t>(page_x),
        layout.clip_grid_origin_y[clip_index]
          + static_cast<std::int32_t>(page_y)));
  }
  return resident_keys;
}

auto MakeVirtualRequestFeedback(const VirtualFeedbackLayout& layout,
  const SequenceNumber source_frame_sequence,
  const std::span<const std::uint32_t> requested_page_indices,
  const oxygen::renderer::VirtualShadowFeedbackKind kind
  = oxygen::renderer::VirtualShadowFeedbackKind::kDetail)
  -> oxygen::renderer::VirtualShadowRequestFeedback
{
  return oxygen::renderer::VirtualShadowRequestFeedback {
    .source_frame_sequence = source_frame_sequence,
    .pages_per_axis = layout.pages_per_axis,
    .clip_level_count = layout.clip_level_count,
    .directional_address_space_hash = layout.directional_address_space_hash,
    .kind = kind,
    .requested_resident_keys
    = RequestedResidentKeys(layout, requested_page_indices),
  };
}

auto MakeVirtualRequestFeedback(const VirtualFeedbackLayout& layout,
  const SequenceNumber source_frame_sequence,
  const std::initializer_list<std::uint32_t> requested_page_indices,
  const oxygen::renderer::VirtualShadowFeedbackKind kind
  = oxygen::renderer::VirtualShadowFeedbackKind::kDetail)
  -> oxygen::renderer::VirtualShadowRequestFeedback
{
  return MakeVirtualRequestFeedback(layout, source_frame_sequence,
    std::span<const std::uint32_t> {
      requested_page_indices.begin(), requested_page_indices.size() },
    kind);
}

auto LocalPageIndexForResidentKey(const VirtualFeedbackLayout& layout,
  const std::uint64_t resident_key) -> std::optional<std::uint32_t>
{
  if (layout.pages_per_axis == 0U || layout.pages_per_level == 0U) {
    return std::nullopt;
  }

  const auto clip_index = oxygen::renderer::internal::shadow_detail::
    VirtualResidentPageKeyClipLevel(resident_key);
  if (clip_index >= layout.clip_level_count) {
    return std::nullopt;
  }

  const auto local_page_x
    = oxygen::renderer::internal::shadow_detail::VirtualResidentPageKeyGridX(
        resident_key)
    - layout.clip_grid_origin_x[clip_index];
  const auto local_page_y
    = oxygen::renderer::internal::shadow_detail::VirtualResidentPageKeyGridY(
        resident_key)
    - layout.clip_grid_origin_y[clip_index];
  if (local_page_x < 0 || local_page_y < 0
    || local_page_x >= static_cast<std::int32_t>(layout.pages_per_axis)
    || local_page_y >= static_cast<std::int32_t>(layout.pages_per_axis)) {
    return std::nullopt;
  }

  return clip_index * layout.pages_per_level
    + static_cast<std::uint32_t>(local_page_y) * layout.pages_per_axis
    + static_cast<std::uint32_t>(local_page_x);
}

auto SelectInteriorFinePage(const VirtualFeedbackLayout& layout)
  -> std::uint32_t
{
  if (layout.pages_per_level == 0U || layout.pages_per_axis == 0U) {
    return 0U;
  }
  return std::min(layout.pages_per_level - 1U, layout.pages_per_axis + 1U);
}

struct VirtualPageCoordinates {
  std::uint32_t clip_index { 0U };
  std::uint32_t page_x { 0U };
  std::uint32_t page_y { 0U };
};

auto DecodeVirtualPageIndex(
  const VirtualFeedbackLayout& layout, const std::uint32_t global_page_index)
  -> std::optional<VirtualPageCoordinates>
{
  if (layout.pages_per_level == 0U || layout.pages_per_axis == 0U) {
    return std::nullopt;
  }

  const auto clip_index = global_page_index / layout.pages_per_level;
  if (clip_index >= layout.clip_level_count) {
    return std::nullopt;
  }

  const auto local_page_index = global_page_index % layout.pages_per_level;
  return VirtualPageCoordinates {
    .clip_index = clip_index,
    .page_x = local_page_index % layout.pages_per_axis,
    .page_y = local_page_index / layout.pages_per_axis,
  };
}

auto FindMappedPageInClip(const VirtualFeedbackLayout& layout,
  const std::span<const std::uint32_t> page_table_entries,
  const std::uint32_t clip_index, const std::uint32_t page_margin = 0U)
  -> std::optional<std::uint32_t>
{
  if (layout.pages_per_level == 0U || layout.pages_per_axis == 0U
    || clip_index >= layout.clip_level_count) {
    return std::nullopt;
  }

  const auto clip_start
    = static_cast<std::size_t>(clip_index) * layout.pages_per_level;
  const auto clip_end
    = std::min(clip_start + layout.pages_per_level, page_table_entries.size());
  if (clip_start >= clip_end) {
    return std::nullopt;
  }

  for (std::size_t global_page_index = clip_start; global_page_index < clip_end;
    ++global_page_index) {
    if (page_table_entries[global_page_index] == 0U) {
      continue;
    }

    const auto decoded = DecodeVirtualPageIndex(
      layout, static_cast<std::uint32_t>(global_page_index));
    if (!decoded.has_value()) {
      continue;
    }
    if (decoded->page_x < page_margin || decoded->page_y < page_margin
      || decoded->page_x + page_margin >= layout.pages_per_axis
      || decoded->page_y + page_margin >= layout.pages_per_axis) {
      continue;
    }
    return static_cast<std::uint32_t>(global_page_index);
  }

  return std::nullopt;
}

auto CollectMappedPagesInClip(const VirtualFeedbackLayout& layout,
  const std::span<const std::uint32_t> page_table_entries,
  const std::uint32_t clip_index) -> std::vector<std::uint32_t>
{
  std::vector<std::uint32_t> mapped_pages {};
  if (layout.pages_per_level == 0U || clip_index >= layout.clip_level_count) {
    return mapped_pages;
  }

  const auto clip_start
    = static_cast<std::size_t>(clip_index) * layout.pages_per_level;
  const auto clip_end
    = std::min(clip_start + layout.pages_per_level, page_table_entries.size());
  for (std::size_t global_page_index = clip_start; global_page_index < clip_end;
    ++global_page_index) {
    if (page_table_entries[global_page_index] != 0U) {
      mapped_pages.push_back(static_cast<std::uint32_t>(global_page_index));
    }
  }
  return mapped_pages;
}

auto CollectCurrentLodMappedPagesInClip(const VirtualFeedbackLayout& layout,
  const std::span<const std::uint32_t> page_table_entries,
  const std::uint32_t clip_index) -> std::vector<std::uint32_t>
{
  std::vector<std::uint32_t> mapped_pages {};
  if (layout.pages_per_level == 0U || clip_index >= layout.clip_level_count) {
    return mapped_pages;
  }

  const auto clip_start
    = static_cast<std::size_t>(clip_index) * layout.pages_per_level;
  const auto clip_end
    = std::min(clip_start + layout.pages_per_level, page_table_entries.size());
  for (std::size_t global_page_index = clip_start; global_page_index < clip_end;
    ++global_page_index) {
    const auto packed_entry = page_table_entries[global_page_index];
    if (packed_entry == 0U
      || !oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
        packed_entry)) {
      continue;
    }
    mapped_pages.push_back(static_cast<std::uint32_t>(global_page_index));
  }
  return mapped_pages;
}

auto FindAdjacentMappedPagesInClip(const VirtualFeedbackLayout& layout,
  const std::span<const std::uint32_t> page_table_entries,
  const std::uint32_t clip_index)
  -> std::optional<std::pair<std::uint32_t, std::uint32_t>>
{
  if (layout.pages_per_level == 0U || layout.pages_per_axis < 2U
    || clip_index >= layout.clip_level_count) {
    return std::nullopt;
  }

  const auto clip_base = clip_index * layout.pages_per_level;
  for (std::uint32_t page_y = 0U; page_y < layout.pages_per_axis; ++page_y) {
    for (std::uint32_t page_x = 0U; page_x + 1U < layout.pages_per_axis;
      ++page_x) {
      const auto first_page
        = clip_base + page_y * layout.pages_per_axis + page_x;
      const auto second_page = first_page + 1U;
      if (static_cast<std::size_t>(second_page) >= page_table_entries.size()) {
        continue;
      }
      if (page_table_entries[first_page] != 0U
        && page_table_entries[second_page] != 0U) {
        return std::pair { first_page, second_page };
      }
    }
  }

  return std::nullopt;
}

auto FindAdjacentCurrentLodMappedPagesInClip(
  const VirtualFeedbackLayout& layout,
  const std::span<const std::uint32_t> page_table_entries,
  const std::uint32_t clip_index)
  -> std::optional<std::pair<std::uint32_t, std::uint32_t>>
{
  if (layout.pages_per_level == 0U || layout.pages_per_axis < 2U
    || clip_index >= layout.clip_level_count) {
    return std::nullopt;
  }

  const auto clip_base = clip_index * layout.pages_per_level;
  for (std::uint32_t page_y = 0U; page_y < layout.pages_per_axis; ++page_y) {
    for (std::uint32_t page_x = 0U; page_x + 1U < layout.pages_per_axis;
      ++page_x) {
      const auto first_page
        = clip_base + page_y * layout.pages_per_axis + page_x;
      const auto second_page = first_page + 1U;
      if (static_cast<std::size_t>(second_page) >= page_table_entries.size()) {
        continue;
      }
      if (oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
            page_table_entries[first_page])
        && oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
          page_table_entries[second_page])) {
        return std::pair { first_page, second_page };
      }
    }
  }

  return std::nullopt;
}

auto CountMappedPagesInClip(const VirtualFeedbackLayout& layout,
  const std::span<const std::uint32_t> page_table_entries,
  const std::uint32_t clip_index) -> std::uint32_t
{
  return static_cast<std::uint32_t>(
    CollectMappedPagesInClip(layout, page_table_entries, clip_index).size());
}

auto CountCurrentLodMappedPagesInClip(const VirtualFeedbackLayout& layout,
  const std::span<const std::uint32_t> page_table_entries,
  const std::uint32_t clip_index) -> std::uint32_t
{
  return static_cast<std::uint32_t>(
    CollectCurrentLodMappedPagesInClip(layout, page_table_entries, clip_index)
      .size());
}

auto FindCurrentLodMappedPageInClip(const VirtualFeedbackLayout& layout,
  const std::span<const std::uint32_t> page_table_entries,
  const std::uint32_t clip_index, const std::uint32_t page_margin = 0U)
  -> std::optional<std::uint32_t>
{
  if (layout.pages_per_level == 0U || layout.pages_per_axis == 0U
    || clip_index >= layout.clip_level_count) {
    return std::nullopt;
  }

  const auto clip_start
    = static_cast<std::size_t>(clip_index) * layout.pages_per_level;
  const auto clip_end
    = std::min(clip_start + layout.pages_per_level, page_table_entries.size());
  for (std::size_t global_page_index = clip_start; global_page_index < clip_end;
    ++global_page_index) {
    const auto packed_entry = page_table_entries[global_page_index];
    if (packed_entry == 0U) {
      continue;
    }

    const auto decoded_entry
      = oxygen::renderer::DecodeVirtualShadowPageTableEntry(packed_entry);
    if (!decoded_entry.current_lod_valid) {
      continue;
    }

    const auto decoded_page = DecodeVirtualPageIndex(
      layout, static_cast<std::uint32_t>(global_page_index));
    if (!decoded_page.has_value()) {
      continue;
    }
    if (decoded_page->page_x < page_margin || decoded_page->page_y < page_margin
      || decoded_page->page_x + page_margin >= layout.pages_per_axis
      || decoded_page->page_y + page_margin >= layout.pages_per_axis) {
      continue;
    }
    return static_cast<std::uint32_t>(global_page_index);
  }

  return std::nullopt;
}

auto DirectionalVirtualPageWorldCenter(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata,
  const std::uint32_t clip_index, const std::uint32_t page_x,
  const std::uint32_t page_y, const float light_space_depth = 0.1F) -> glm::vec3
{
  const auto inverse_light_view = glm::inverse(metadata.light_view);
  const auto& clip_metadata = metadata.clip_metadata[clip_index];
  const float page_world_size = clip_metadata.origin_page_scale.z;
  const glm::vec3 light_space_point(clip_metadata.origin_page_scale.x
      + (static_cast<float>(page_x) + 0.5F) * page_world_size,
    clip_metadata.origin_page_scale.y
      + (static_cast<float>(page_y) + 0.5F) * page_world_size,
    light_space_depth);
  return glm::vec3(inverse_light_view * glm::vec4(light_space_point, 1.0F));
}

auto GlobalPageIndexForWorldPoint(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata,
  const std::uint32_t clip_index, const glm::vec3& world_point)
  -> std::optional<std::uint32_t>
{
  if (metadata.pages_per_axis == 0U
    || clip_index >= metadata.clip_level_count) {
    return std::nullopt;
  }

  const auto& clip_metadata = metadata.clip_metadata[clip_index];
  const float page_world_size = clip_metadata.origin_page_scale.z;
  if (page_world_size <= 0.0F) {
    return std::nullopt;
  }

  const auto light_space_point
    = glm::vec3(metadata.light_view * glm::vec4(world_point, 1.0F));
  const auto page_x = static_cast<std::int32_t>(
    std::floor((light_space_point.x - clip_metadata.origin_page_scale.x)
      / page_world_size));
  const auto page_y = static_cast<std::int32_t>(
    std::floor((light_space_point.y - clip_metadata.origin_page_scale.y)
      / page_world_size));
  if (page_x < 0 || page_y < 0
    || page_x >= static_cast<std::int32_t>(metadata.pages_per_axis)
    || page_y >= static_cast<std::int32_t>(metadata.pages_per_axis)) {
    return std::nullopt;
  }

  const auto pages_per_level
    = metadata.pages_per_axis * metadata.pages_per_axis;
  return clip_index * pages_per_level
    + static_cast<std::uint32_t>(page_y) * metadata.pages_per_axis
    + static_cast<std::uint32_t>(page_x);
}

auto CurrentLodGlobalPageIndexForWorldPoint(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata,
  const std::span<const std::uint32_t> page_table_entries,
  const glm::vec3& world_point) -> std::optional<std::uint32_t>
{
  for (std::uint32_t clip_index = 0U; clip_index < metadata.clip_level_count;
    ++clip_index) {
    const auto page_index
      = GlobalPageIndexForWorldPoint(metadata, clip_index, world_point);
    if (!page_index.has_value() || *page_index >= page_table_entries.size()) {
      continue;
    }

    const auto packed_entry = page_table_entries[*page_index];
    if (packed_entry != 0U
      && oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
        packed_entry)) {
      return page_index;
    }
  }

  return std::nullopt;
}

auto DescribeCurrentLodCoverageForWorldPoint(
  const oxygen::engine::DirectionalVirtualShadowMetadata& metadata,
  const std::span<const std::uint32_t> page_table_entries,
  const glm::vec3& world_point) -> std::string
{
  std::ostringstream stream;
  bool emitted_clip = false;
  for (std::uint32_t clip_index = 0U; clip_index < metadata.clip_level_count;
    ++clip_index) {
    const auto page_index
      = GlobalPageIndexForWorldPoint(metadata, clip_index, world_point);
    if (!page_index.has_value()) {
      continue;
    }
    if (*page_index >= page_table_entries.size()) {
      continue;
    }

    if (emitted_clip) {
      stream << ' ';
    }
    emitted_clip = true;

    const auto packed_entry = page_table_entries[*page_index];
    const auto decoded_entry
      = oxygen::renderer::DecodeVirtualShadowPageTableEntry(packed_entry);
    stream << "c" << clip_index << "=" << *page_index << ":";
    if (packed_entry == 0U) {
      stream << "empty";
      continue;
    }
    if (decoded_entry.current_lod_valid) {
      stream << "current";
      continue;
    }
    if (!decoded_entry.any_lod_valid) {
      stream << "invalid";
      continue;
    }
    stream << "fallback->"
           << oxygen::renderer::ResolveVirtualShadowFallbackClipIndex(
                clip_index, metadata.clip_level_count, packed_entry);
  }

  if (!emitted_clip) {
    stream << "out-of-bounds";
  }
  return stream.str();
}

auto AdvanceRendererFrame(LightManager& manager, ShadowManager& shadow_manager,
  ViewConstants& view_constants, const SequenceNumber sequence, const Slot slot)
  -> void
{
  manager.OnFrameStart(RendererTagFactory::Get(), sequence, slot);
  shadow_manager.OnFrameStart(RendererTagFactory::Get(), sequence, slot);
  view_constants.SetFrameSequenceNumber(sequence, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(slot, ViewConstants::kRenderer);
}

//=== LightManager Basic Tests
//===---------------------------------------------//

class LightManagerTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    gfx_ = std::make_shared<FakeGraphics>();
    gfx_->CreateCommandQueues(SingleQueueStrategy());

    uploader_ = std::make_unique<UploadCoordinator>(
      observer_ptr { gfx_.get() }, DefaultUploadPolicy());

    staging_provider_ = uploader_->CreateRingBufferStaging(
      oxygen::frame::SlotCount { 1 }, 256u);

    inline_transfers_ = std::make_unique<InlineTransfersCoordinator>(
      observer_ptr { gfx_.get() });

    manager_ = std::make_unique<LightManager>(observer_ptr { gfx_.get() },
      observer_ptr { staging_provider_.get() },
      observer_ptr { inline_transfers_.get() });

    static constexpr size_t kTestSceneCapacity = 64;
    scene_
      = std::make_shared<Scene>("LightManagerTestScene", kTestSceneCapacity);
  }

  [[nodiscard]] auto Manager() const -> LightManager& { return *manager_; }
  [[nodiscard]] auto GfxPtr() const -> FakeGraphics* { return gfx_.get(); }
  [[nodiscard]] auto CreateShadowManager(
    const oxygen::DirectionalShadowImplementationPolicy directional_policy
    = oxygen::DirectionalShadowImplementationPolicy::kConventionalOnly,
    const oxygen::ShadowQualityTier quality_tier
    = oxygen::ShadowQualityTier::kHigh) const -> std::unique_ptr<ShadowManager>
  {
    return std::make_unique<ShadowManager>(observer_ptr { gfx_.get() },
      observer_ptr { staging_provider_.get() },
      observer_ptr { inline_transfers_.get() }, quality_tier,
      directional_policy);
  }

  [[nodiscard]] auto CreateNode(const std::string& name, const bool visible,
    const bool casts_shadows) const -> SceneNode
  {
    const auto flags = SceneNode::Flags {}
                         .SetFlag(SceneNodeFlags::kVisible,
                           SceneFlag {}.SetEffectiveValueBit(visible))
                         .SetFlag(SceneNodeFlags::kCastsShadows,
                           SceneFlag {}.SetEffectiveValueBit(casts_shadows));

    auto node = scene_->CreateNode(name, flags);
    EXPECT_TRUE(node.IsValid());
    return node;
  }

  auto UpdateTransforms(SceneNode& node) const -> void
  {
    auto impl = node.GetImpl();
    ASSERT_TRUE(impl.has_value());
    impl->get().UpdateTransforms(*scene_);
  }

private:
  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<UploadCoordinator> uploader_;
  std::shared_ptr<StagingProvider> staging_provider_;
  std::unique_ptr<InlineTransfersCoordinator> inline_transfers_;
  std::unique_ptr<LightManager> manager_;
  std::shared_ptr<Scene> scene_;
};

//! Invisible nodes are a hard gate and emit no lights.
NOLINT_TEST_F(LightManagerTest, CollectFromNode_InvisibleNodeEmitsNoLights)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("invisible", /*visible=*/false,
    /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  UpdateTransforms(node);

  // Act
  manager.CollectFromNode(impl->get());

  // Assert
  EXPECT_TRUE(manager.GetDirectionalLights().empty());
  EXPECT_TRUE(manager.GetPositionalLights().empty());
}

//! Lights with `affects_world=false` are not collected.
NOLINT_TEST_F(LightManagerTest, CollectFromNode_AffectsWorldFalseEmitsNoLights)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().affects_world = false;
  UpdateTransforms(node);

  // Act
  manager.CollectFromNode(impl->get());

  // Assert
  EXPECT_TRUE(manager.GetDirectionalLights().empty());
}

//! Baked mobility lights are excluded from runtime collection.
NOLINT_TEST_F(LightManagerTest, CollectFromNode_BakedMobilityEmitsNoLights)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().mobility = oxygen::scene::LightMobility::kBaked;
  UpdateTransforms(node);

  // Act
  manager.CollectFromNode(impl->get());

  // Assert
  EXPECT_TRUE(manager.GetDirectionalLights().empty());
}

//! Shadow eligibility requires both the light property and the node flag.
NOLINT_TEST_F(
  LightManagerTest, CollectFromNode_ShadowEligibilityRequiresNodeFlag)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/false);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().casts_shadows = true;
  UpdateTransforms(node);

  // Act
  manager.CollectFromNode(impl->get());

  // Assert
  const auto lights = manager.GetDirectionalLights();
  ASSERT_EQ(lights.size(), 1);

  constexpr auto kInvalidShadowIndex = 0xFFFFFFFFu;
  EXPECT_EQ(lights[0].shadow_index, kInvalidShadowIndex);

  const auto flags = lights[0].flags;
  const auto casts_shadows_bit
    = static_cast<std::uint32_t>(DirectionalLightFlags::kCastsShadows);
  EXPECT_EQ(flags & casts_shadows_bit, 0U);
}

//! Directional light direction is derived from world rotation * Forward.
NOLINT_TEST_F(LightManagerTest, CollectFromNode_DirectionUsesWorldRotation)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);

  auto transform = node.GetTransform();
  const auto rotation
    = glm::angleAxis(glm::radians(90.0F), glm::vec3 { 0, 1, 0 });
  ASSERT_TRUE(transform.SetLocalRotation(rotation));

  UpdateTransforms(node);

  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();

  const glm::vec3 expected_dir
    = glm::normalize(rotation * oxygen::space::move::Forward);

  // Act
  manager.CollectFromNode(impl->get());

  // Assert
  const auto lights = manager.GetDirectionalLights();
  ASSERT_EQ(lights.size(), 1);

  EXPECT_NEAR(lights[0].direction_ws.x, expected_dir.x, 1e-5F);
  EXPECT_NEAR(lights[0].direction_ws.y, expected_dir.y, 1e-5F);
  EXPECT_NEAR(lights[0].direction_ws.z, expected_dir.z, 1e-5F);
}

//! When no lights are collected, SRV indices remain invalid.
NOLINT_TEST_F(LightManagerTest, EnsureFrameResources_NoLightsKeepsSrvInvalid)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  // Act
  manager.EnsureFrameResources();

  // Assert
  EXPECT_EQ(manager.GetDirectionalLightsSrvIndex(), kInvalidShaderVisibleIndex);
  EXPECT_EQ(manager.GetPositionalLightsSrvIndex(), kInvalidShaderVisibleIndex);
  EXPECT_TRUE(manager.GetDirectionalShadowCandidates().empty());
}

//! Collecting lights and ensuring frame resources yields valid SRV indices.
NOLINT_TEST_F(LightManagerTest,
  EnsureFrameResources_WithDirectionalAndPositionalLightsAllocatesSrvs)
{
  // Arrange
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto dir_node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto dir_impl = dir_node.GetImpl();
  ASSERT_TRUE(dir_impl.has_value());
  dir_impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  dir_impl->get()
    .GetComponent<oxygen::scene::DirectionalLight>()
    .Common()
    .casts_shadows
    = true;
  UpdateTransforms(dir_node);

  auto point_node
    = CreateNode("point", /*visible=*/true, /*casts_shadows=*/true);
  auto point_impl = point_node.GetImpl();
  ASSERT_TRUE(point_impl.has_value());
  point_impl->get().AddComponent<oxygen::scene::PointLight>();
  UpdateTransforms(point_node);

  manager.CollectFromNode(dir_impl->get());
  manager.CollectFromNode(point_impl->get());

  // Act
  manager.EnsureFrameResources();

  // Assert
  EXPECT_EQ(manager.GetDirectionalLights().size(), 1);
  EXPECT_EQ(manager.GetDirectionalShadowCandidates().size(), 1);
  EXPECT_EQ(manager.GetPositionalLights().size(), 1);

  EXPECT_NE(manager.GetDirectionalLightsSrvIndex(), kInvalidShaderVisibleIndex);
  EXPECT_NE(manager.GetPositionalLightsSrvIndex(), kInvalidShaderVisibleIndex);
}

//! Canonical directional shadow defaults must be non-zero so both scene lights
//! and synthetic suns start from the same sane split contract.
NOLINT_TEST(LightCommonDefaultsTest,
  CascadedShadowSettings_DefaultsUseCanonicalCascadeDistances)
{
  const oxygen::scene::CascadedShadowSettings defaults {};
  EXPECT_EQ(defaults.cascade_count, oxygen::scene::kMaxShadowCascades);
  EXPECT_FLOAT_EQ(defaults.cascade_distances[0], 8.0F);
  EXPECT_FLOAT_EQ(defaults.cascade_distances[1], 24.0F);
  EXPECT_FLOAT_EQ(defaults.cascade_distances[2], 64.0F);
  EXPECT_FLOAT_EQ(defaults.cascade_distances[3], 160.0F);
  EXPECT_FLOAT_EQ(defaults.distribution_exponent, 1.0F);
}

//! Legacy/cooked zero cascade distances must be canonicalized before the
//! shadow runtime consumes a directional light.
NOLINT_TEST_F(LightManagerTest,
  CollectFromNode_DirectionalShadowCandidateCanonicalizesLegacyZeroCascadeSplits)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().casts_shadows = true;
  light.CascadedShadows().cascade_distances = { 0.0F, 0.0F, 0.0F, 0.0F };
  UpdateTransforms(node);

  manager.CollectFromNode(impl->get());

  ASSERT_EQ(manager.GetDirectionalShadowCandidates().size(), 1U);
  const auto& candidate = manager.GetDirectionalShadowCandidates().front();
  EXPECT_EQ(candidate.cascade_count, oxygen::scene::kMaxShadowCascades);
  EXPECT_FLOAT_EQ(candidate.cascade_distances[0], 8.0F);
  EXPECT_FLOAT_EQ(candidate.cascade_distances[1], 24.0F);
  EXPECT_FLOAT_EQ(candidate.cascade_distances[2], 64.0F);
  EXPECT_FLOAT_EQ(candidate.cascade_distances[3], 160.0F);
  EXPECT_FLOAT_EQ(candidate.distribution_exponent, 1.0F);
}

//! ShadowManager publishes shading-facing shadow data and a backend-neutral
//! raster render plan for shadow-casting directionals.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_DirectionalPublicationAndRasterPlanArePublished)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().casts_shadows = true;
  light.Common().shadow.contact_shadows = true;
  light.SetIsSunLight(true);
  light.CascadedShadows().distribution_exponent = 2.0F;
  light.CascadedShadows().cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F };
  UpdateTransforms(node);

  manager.CollectFromNode(impl->get());
  manager.EnsureFrameResources();

  auto shadow_manager = CreateShadowManager();
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const auto published = shadow_manager->PublishForView(
    oxygen::ViewId { 7 }, view_constants, manager);
  const auto* introspection
    = shadow_manager->TryGetViewIntrospection(oxygen::ViewId { 7 });
  const auto* raster_plan
    = shadow_manager->TryGetRasterRenderPlan(oxygen::ViewId { 7 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.directional_shadow_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(introspection, nullptr);
  ASSERT_NE(raster_plan, nullptr);
  ASSERT_EQ(introspection->shadow_instances.size(), 1U);
  ASSERT_EQ(introspection->directional_metadata.size(), 1U);
  ASSERT_EQ(introspection->raster_jobs.size(), 4U);
  ASSERT_EQ(raster_plan->jobs.size(), 4U);
  ASSERT_NE(raster_plan->depth_texture, nullptr);

  const auto& instance = introspection->shadow_instances[0];
  EXPECT_EQ(instance.light_index, 0U);
  EXPECT_EQ(instance.payload_index, 0U);
  EXPECT_EQ(instance.domain, 0U);
  EXPECT_EQ(instance.implementation_kind, 1U);
  EXPECT_NE(instance.flags & (1U << 0), 0U);
  EXPECT_NE(instance.flags & (1U << 1), 0U);
  EXPECT_NE(instance.flags & (1U << 2), 0U);

  const auto& metadata = introspection->directional_metadata[0];
  EXPECT_EQ(metadata.shadow_instance_index, 0U);
  EXPECT_EQ(metadata.implementation_kind, 1U);
  EXPECT_FLOAT_EQ(metadata.distribution_exponent, 2.0F);
  EXPECT_EQ(metadata.cascade_count, 4U);
  EXPECT_FLOAT_EQ(metadata.cascade_distances[0], 8.0F);
  EXPECT_FLOAT_EQ(metadata.cascade_distances[3], 160.0F);
  EXPECT_EQ(introspection->raster_jobs[0].payload_index, 0U);
  EXPECT_EQ(introspection->raster_jobs[3].target_array_slice, 3U);
  EXPECT_EQ(published.sun_shadow_index, 0U);
}

//! ShadowManager can publish a shadowed synthetic sun without a scene
//! directional light.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_SyntheticSunPublishesSunShadowIndex)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager();
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::vec3(0.0F, 0.0F, -1.0F),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };

  const auto published = shadow_manager->PublishForView(
    oxygen::ViewId { 8 }, view_constants, manager, {}, {}, &synthetic_sun);
  const auto* introspection
    = shadow_manager->TryGetViewIntrospection(oxygen::ViewId { 8 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.directional_shadow_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(introspection, nullptr);
  ASSERT_EQ(introspection->shadow_instances.size(), 1U);
  ASSERT_EQ(introspection->directional_metadata.size(), 1U);
  EXPECT_EQ(published.sun_shadow_index, 0U);

  const auto& instance = introspection->shadow_instances[0];
  EXPECT_EQ(instance.light_index, 0xFFFFFFFFU);
  EXPECT_NE(instance.flags & (1U << 2), 0U);
}

//! The current directional VSM slice must still activate for a synthetic sun
//! even when a scene sun light also exists, because forward shading consumes
//! the resolved sun through `sun_shadow_index` and skips the scene sun light.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStatePrefersSyntheticSunOverSceneSunLight)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("scene_sun", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().casts_shadows = true;
  light.SetIsSunLight(true);
  UpdateTransforms(node);

  manager.CollectFromNode(impl->get());
  manager.EnsureFrameResources();

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 81 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 81 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(published.virtual_directional_shadow_metadata_srv,
    kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.virtual_shadow_physical_pool_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_introspection, nullptr);
  EXPECT_EQ(published.sun_shadow_index, 0U);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());
  EXPECT_NE(virtual_introspection->directional_virtual_metadata.front().flags
      & static_cast<std::uint32_t>(
        oxygen::engine::ShadowProductFlags::kSunLight),
    0U);
}

//! Virtual shadow planning is driven by visible receiver demand instead of a
//! centered resident window.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateUsesVisibleReceiverBounds)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kPreferVirtual);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 10 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 10 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(published.virtual_directional_shadow_metadata_srv,
    kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.virtual_shadow_physical_pool_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());
  const auto receiver_page_index = GlobalPageIndexForWorldPoint(
    virtual_introspection->directional_virtual_metadata.front(),
    0U, glm::vec3(0.0F));
  EXPECT_TRUE(receiver_page_index.has_value())
    << "receiver world point falls outside clip-0 metadata coverage";
}


//! Ultra-tier directional VSM should publish a dense virtual address space
//! while keeping physical page texel size above the minimum useful floor.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateUsesDenseGridForUltraTier)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly,
    oxygen::ShadowQualityTier::kUltra);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 30 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 30 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(pending_raster_state, nullptr);
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 30 });
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());
  EXPECT_GE(
    virtual_introspection->directional_virtual_metadata.front().pages_per_axis,
    64U);
  EXPECT_GE(pending_raster_state->page_size_texels, 128U);
}

//! Ultra-tier directional VSM should use a denser virtual address space than
//! the physical pool so quality can improve without sizing the atlas for full
//! residency.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateUsesDenserAddressSpaceThanPhysicalPool)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly,
    oxygen::ShadowQualityTier::kUltra);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 31 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 31 });
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 31 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(pending_raster_state, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());

  const auto& metadata
    = virtual_introspection->directional_virtual_metadata.front();
  const auto virtual_page_count = metadata.clip_level_count
    * metadata.pages_per_axis * metadata.pages_per_axis;
  const auto physical_tile_capacity
    = pending_raster_state->atlas_tiles_per_axis * pending_raster_state->atlas_tiles_per_axis;

  EXPECT_EQ(metadata.clip_level_count, 12U);
  EXPECT_GE(metadata.pages_per_axis, 64U);
  EXPECT_LT(physical_tile_capacity, virtual_page_count);
}








//! Virtual shadow publication keeps resident pages and skips rerasterization
//! when the snapped virtual plan is unchanged.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateReusesResidentPagesForIdenticalInputs)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kPreferVirtual);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 2000.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 11 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 11 });
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 11 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_pending_raster_state, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  EXPECT_TRUE(first_pending_raster_state->resolved_pages.empty());
  EXPECT_EQ(first_virtual_introspection->pending_raster_page_count, 0U);
  EXPECT_EQ(first_virtual_introspection->resident_page_count, 0U);
  EXPECT_EQ(first_virtual_introspection->mapped_page_count, 0U);
  EXPECT_EQ(first_virtual_introspection->pending_page_count, 0U);
  EXPECT_EQ(first_virtual_introspection->clean_page_count, 0U);

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 11 }, true);

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 11 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 11 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 11 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_pending_raster_state, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_pending_raster_state->resolved_pages.size(), 0U);
  EXPECT_EQ(second_virtual_introspection->pending_raster_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->resident_page_count,
    first_virtual_introspection->resident_page_count);
  EXPECT_EQ(second_virtual_introspection->mapped_page_count,
    first_virtual_introspection->mapped_page_count);
  EXPECT_EQ(second_virtual_introspection->pending_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->clean_page_count,
    first_virtual_introspection->resident_page_count);
  EXPECT_EQ(second.virtual_shadow_physical_pool_srv,
    first.virtual_shadow_physical_pool_srv);
}

//! Reordering the shadow-caster bounds set must not invalidate the directional
//! VSM cache when the actual casters are unchanged.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateReusesResidentPagesForReorderedCasterBounds)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kPreferVirtual);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 2> shadow_casters_a {
    glm::vec4(-4.0F, 0.0F, 0.5F, 0.5F),
    glm::vec4(4.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 2> shadow_casters_b {
    shadow_casters_a[1],
    shadow_casters_a[0],
  };
  const std::array<glm::vec4, 2> visible_receivers {
    glm::vec4(-4.0F, 0.0F, 0.0F, 0.05F),
    glm::vec4(4.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 110 },
    view_constants, manager, shadow_casters_a, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 110 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  EXPECT_EQ(first_virtual_introspection->resident_page_count, 0U);

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 110 }, true);

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 110 },
    view_constants, manager, shadow_casters_b, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* second_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 110 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 110 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_pending_raster_state, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_pending_raster_state->resolved_pages.size(), 0U);
  EXPECT_EQ(second_virtual_introspection->pending_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->clean_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->resident_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->mapped_page_count, 0U);
  EXPECT_TRUE(
    std::ranges::equal(second_virtual_introspection->page_table_entries,
      first_virtual_introspection->page_table_entries));
}

//! Clean resident virtual pages that are no longer requested should remain
//! cached instead of being dropped immediately, so later movement can reuse a
//! larger resident working set.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateRetainsCleanPagesAcrossReceiverShift)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kPreferVirtual);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> initial_receivers {
    glm::vec4(-6.0F, 0.0F, 0.0F, 0.05F),
  };
  const std::array<glm::vec4, 1> shifted_receivers {
    glm::vec4(6.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 18 },
    view_constants, manager, shadow_casters, initial_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 18 });
  ASSERT_NE(first_virtual_introspection, nullptr);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 18 }, true);

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 18 },
    view_constants, manager, shadow_casters, shifted_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 18 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 18 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_pending_raster_state, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_virtual_introspection->mapped_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->resident_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->clean_page_count, 0U);
}

//! Page-aligned directional clipmap motion should reuse overlapping resident
//! pages instead of rerasterizing the whole requested working set, even if the
//! backend chooses a slightly different light-space placement while preserving
//! the overlapping page contents.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateReusesResidentPagesAcrossClipmapShift)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kPreferVirtual);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 30 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 30 });
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 30 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_pending_raster_state, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  EXPECT_TRUE(first_pending_raster_state->resolved_pages.empty());
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());

  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto inverse_light_view = glm::inverse(first_metadata.light_view);
  const float fine_page_world
    = first_metadata.clip_metadata[0].origin_page_scale.z;
  const glm::vec3 world_origin
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::vec3 world_shift_x
    = glm::vec3(
        inverse_light_view * glm::vec4(fine_page_world, 0.0F, 0.0F, 1.0F))
    - world_origin;

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 30 }, true);

  view_constants.SetCameraPosition(
    view_constants.GetCameraPosition() + world_shift_x);
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 30 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 30 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 30 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_pending_raster_state, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());

  const auto& second_metadata
    = second_virtual_introspection->directional_virtual_metadata.front();
  EXPECT_NE(first_metadata.clip_metadata[0].origin_page_scale.x,
    second_metadata.clip_metadata[0].origin_page_scale.x);
  EXPECT_EQ(second_virtual_introspection->pending_raster_page_count, 0U);
  EXPECT_EQ(second_pending_raster_state->resolved_pages.size(), 0U);
  EXPECT_EQ(second_virtual_introspection->clean_page_count, 0U);
}

//! Compatible page-aligned clipmap motion must preserve absolute page identity.
//! In the fake unit-test path that only means the same world-space page maps to
//! a new local page index after the snapped clip lattice shifts.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualClipmapShiftMovesWorldPageToNewLocalIndex)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 31 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 31 });
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 31 }, true);
  const auto* first_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 31 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_pending_raster_state, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());

  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto first_layout = BuildVirtualFeedbackLayout(first_metadata);
  const auto first_tracked_page = GlobalPageIndexForWorldPoint(first_metadata,
    0U, DirectionalVirtualPageWorldCenter(first_metadata, 0U,
          first_metadata.pages_per_axis / 2U,
          first_metadata.pages_per_axis / 2U));
  ASSERT_TRUE(first_tracked_page.has_value());
  const auto first_page_coords
    = DecodeVirtualPageIndex(first_layout, *first_tracked_page);
  ASSERT_TRUE(first_page_coords.has_value());
  const auto tracked_world_center = DirectionalVirtualPageWorldCenter(
    first_metadata, first_page_coords->clip_index, first_page_coords->page_x,
    first_page_coords->page_y);

  const auto tracked_resident_keys
    = RequestedResidentKeys(first_layout, std::array { *first_tracked_page });
  ASSERT_EQ(tracked_resident_keys.size(), 1U);
  const auto tracked_resident_key = tracked_resident_keys.front();

  const auto inverse_light_view = glm::inverse(first_metadata.light_view);
  const float fine_page_world
    = first_metadata.clip_metadata[0].origin_page_scale.z;
  const glm::vec3 world_origin
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::vec3 world_shift_x
    = glm::vec3(
        inverse_light_view * glm::vec4(fine_page_world, 0.0F, 0.0F, 1.0F))
    - world_origin;

  view_constants.SetCameraPosition(
    view_constants.GetCameraPosition() + world_shift_x);
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 31 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 31 });
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 31 }, true);
  const auto* second_virtual_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 31 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_pending_raster_state, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());

  const auto& second_metadata
    = second_virtual_introspection->directional_virtual_metadata.front();
  const auto second_page_index
    = GlobalPageIndexForWorldPoint(second_metadata, 0U, tracked_world_center);
  ASSERT_TRUE(second_page_index.has_value());
  ASSERT_NE(*second_page_index, *first_tracked_page);

  const auto rerastered_tracked_page
    = std::ranges::any_of(second_pending_raster_state->resolved_pages,
      [tracked_resident_key](const auto& page) {
        return page.resident_key == tracked_resident_key;
      });
  EXPECT_FALSE(rerastered_tracked_page);
}

//! The explicit clipmap setup stage must publish per-clip page-space offsets
//! and guardband validity derived from the previous snapped clipmap state.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualClipmapSetupPublishesOffsetsAndGuardbandReuse)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 230 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 230 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_FALSE(first_introspection->directional_virtual_metadata.empty());
  const auto first_metadata
    = first_introspection->directional_virtual_metadata.front();
  ASSERT_EQ(first_introspection->clipmap_page_offset_x.size(),
    first_metadata.clip_level_count);
  ASSERT_EQ(first_introspection->clipmap_page_offset_y.size(),
    first_metadata.clip_level_count);
  ASSERT_EQ(first_introspection->clipmap_reuse_guardband_valid.size(),
    first_metadata.clip_level_count);
  ASSERT_EQ(first_introspection->clipmap_cache_valid.size(),
    first_metadata.clip_level_count);
  ASSERT_EQ(first_introspection->clipmap_cache_status.size(),
    first_metadata.clip_level_count);
  EXPECT_FALSE(first_introspection->cache_layout_compatible);
  EXPECT_FALSE(first_introspection->depth_guardband_valid);
  EXPECT_TRUE(std::ranges::all_of(first_introspection->clipmap_cache_valid,
    [](const bool valid) { return !valid; }));
  EXPECT_TRUE(std::ranges::all_of(
    first_introspection->clipmap_cache_status, [](const auto status) {
      return status
        == oxygen::renderer::DirectionalVirtualClipCacheStatus::
          kNoPreviousFrame;
    }));

  const auto inverse_light_view = glm::inverse(first_metadata.light_view);
  const float fine_page_world
    = first_metadata.clip_metadata[0].origin_page_scale.z;
  const glm::vec3 world_origin
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::vec3 world_shift_x
    = glm::vec3(
        inverse_light_view * glm::vec4(fine_page_world, 0.0F, 0.0F, 1.0F))
    - world_origin;

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 230 }, true);

  view_constants.SetCameraPosition(
    view_constants.GetCameraPosition() + world_shift_x);
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 230 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 230 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_introspection, nullptr);
  ASSERT_FALSE(second_introspection->directional_virtual_metadata.empty());
  const auto second_metadata
    = second_introspection->directional_virtual_metadata.front();
  EXPECT_TRUE(second_introspection->cache_layout_compatible);
  EXPECT_TRUE(second_introspection->depth_guardband_valid);

  for (std::uint32_t clip_index = 0U;
    clip_index < second_metadata.clip_level_count; ++clip_index) {
    const auto expected_offset = oxygen::renderer::internal::shadow_detail::
      ResolveDirectionalVirtualClipmapPageOffset(
        first_metadata, second_metadata, clip_index);
    EXPECT_EQ(second_introspection->clipmap_page_offset_x[clip_index],
      expected_offset.delta_x);
    EXPECT_EQ(second_introspection->clipmap_page_offset_y[clip_index],
      expected_offset.delta_y);
    EXPECT_EQ(second_introspection->clipmap_reuse_guardband_valid[clip_index],
      oxygen::renderer::internal::shadow_detail::
        IsDirectionalVirtualClipReuseGuardbandValid(expected_offset,
          oxygen::renderer::internal::shadow_detail::
            kDirectionalVirtualClipReuseGuardbandPages));
    EXPECT_EQ(second_introspection->clipmap_cache_valid[clip_index],
      second_introspection->clipmap_reuse_guardband_valid[clip_index]);
    EXPECT_EQ(second_introspection->clipmap_cache_status[clip_index],
      second_introspection->clipmap_cache_valid[clip_index]
        ? oxygen::renderer::DirectionalVirtualClipCacheStatus::kValid
        : oxygen::renderer::DirectionalVirtualClipCacheStatus::
            kReuseGuardbandInvalid);
  }
  EXPECT_TRUE(second_introspection->clipmap_reuse_guardband_valid[0]);
  EXPECT_TRUE(second_introspection->clipmap_cache_valid[0]);

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 230 }, true);

  view_constants.SetCameraPosition(
    view_constants.GetCameraPosition() + (world_shift_x * 2.0F));
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 2 });

  const auto third = shadow_manager->PublishForView(oxygen::ViewId { 230 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* third_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 230 });

  ASSERT_NE(third.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(third_introspection, nullptr);
  ASSERT_FALSE(third_introspection->directional_virtual_metadata.empty());
  const auto third_metadata
    = third_introspection->directional_virtual_metadata.front();
  const auto clip0_large_offset = oxygen::renderer::internal::shadow_detail::
    ResolveDirectionalVirtualClipmapPageOffset(
      second_metadata, third_metadata, 0U);
  EXPECT_TRUE(third_introspection->cache_layout_compatible);
  EXPECT_TRUE(third_introspection->depth_guardband_valid);
  EXPECT_EQ(
    third_introspection->clipmap_page_offset_x[0], clip0_large_offset.delta_x);
  EXPECT_EQ(
    third_introspection->clipmap_page_offset_y[0], clip0_large_offset.delta_y);
  EXPECT_FALSE(third_introspection->clipmap_reuse_guardband_valid[0]);
  EXPECT_FALSE(third_introspection->clipmap_cache_valid[0]);
  EXPECT_EQ(third_introspection->clipmap_cache_status[0],
    oxygen::renderer::DirectionalVirtualClipCacheStatus::
      kReuseGuardbandInvalid);
}

//! Directional cache validity must reject light-direction changes even if the
//! clip layout and page sizes stay unchanged.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualCacheValidityRejectsLightDirectionChanges)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput first_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  auto second_sun = first_sun;
  second_sun.direction_ws = glm::normalize(glm::vec3(0.5F, -0.15F, -1.0F));

  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 231 },
    view_constants, manager, shadow_casters, visible_receivers, &first_sun,
    std::chrono::milliseconds(16));
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 231 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection, nullptr);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 231 }, true);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 231 },
    view_constants, manager, shadow_casters, visible_receivers, &second_sun,
    std::chrono::milliseconds(16));
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 231 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_introspection, nullptr);
  EXPECT_FALSE(second_introspection->cache_layout_compatible);
  EXPECT_FALSE(second_introspection->depth_guardband_valid);
  EXPECT_TRUE(std::ranges::all_of(second_introspection->clipmap_cache_valid,
    [](const bool valid) { return !valid; }));
  EXPECT_TRUE(std::ranges::all_of(
    second_introspection->clipmap_cache_status, [](const auto status) {
      return status
        == oxygen::renderer::DirectionalVirtualClipCacheStatus::kLayoutInvalid;
    }));
}




//! Directional feedback/address-space identity must track true incompatible
//! changes such as clip page scale, but it must ignore both raw light-view
//! translation drift and page-aligned clipmap panning. Absolute resident keys
//! already carry panning semantics, so reintroducing translation into this hash
//! recreates the fallback-thrash regression.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualFeedbackAddressSpaceTracksPageScaleButIgnoresTranslationDrift)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.08F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 210 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 210 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());

  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const float finest_page_world
    = std::max(first_metadata.clip_metadata[0].origin_page_scale.z, 1.0e-4F);
  auto sub_page_translated_metadata = first_metadata;
  for (std::uint32_t clip_index = 0U;
    clip_index < sub_page_translated_metadata.clip_level_count; ++clip_index) {
    auto& clip = sub_page_translated_metadata.clip_metadata[clip_index];
    clip.origin_page_scale.x += 0.25F * clip.origin_page_scale.z;
    clip.origin_page_scale.y -= 0.25F * clip.origin_page_scale.z;
  }

  auto page_translated_metadata = first_metadata;
  for (std::uint32_t clip_index = 0U;
    clip_index < page_translated_metadata.clip_level_count; ++clip_index) {
    auto& clip = page_translated_metadata.clip_metadata[clip_index];
    clip.origin_page_scale.x += 2.0F * clip.origin_page_scale.z;
    clip.origin_page_scale.y -= 1.0F * clip.origin_page_scale.z;
  }

  auto page_scaled_metadata = first_metadata;
  for (std::uint32_t clip_index = 0U;
    clip_index < page_scaled_metadata.clip_level_count; ++clip_index) {
    auto& clip = page_scaled_metadata.clip_metadata[clip_index];
    clip.origin_page_scale.z *= 2.0F;
  }

  auto raw_view_translated_metadata = first_metadata;
  raw_view_translated_metadata.light_view[3][0] += 2.0F * finest_page_world;
  raw_view_translated_metadata.light_view[3][1] -= 1.0F * finest_page_world;

  auto z_translated_metadata = first_metadata;
  z_translated_metadata.light_view[3][2] += 5.0F;

  const auto first_hash = oxygen::renderer::internal::shadow_detail::
    HashDirectionalVirtualFeedbackAddressSpace(first_metadata);
  const auto sub_page_translated_hash
    = oxygen::renderer::internal::shadow_detail::
      HashDirectionalVirtualFeedbackAddressSpace(sub_page_translated_metadata);
  const auto page_translated_hash = oxygen::renderer::internal::shadow_detail::
    HashDirectionalVirtualFeedbackAddressSpace(page_translated_metadata);
  const auto page_scaled_hash = oxygen::renderer::internal::shadow_detail::
    HashDirectionalVirtualFeedbackAddressSpace(page_scaled_metadata);
  const auto raw_view_translated_hash
    = oxygen::renderer::internal::shadow_detail::
      HashDirectionalVirtualFeedbackAddressSpace(raw_view_translated_metadata);
  const auto z_translated_hash = oxygen::renderer::internal::shadow_detail::
    HashDirectionalVirtualFeedbackAddressSpace(z_translated_metadata);

  EXPECT_EQ(first_hash, sub_page_translated_hash);
  EXPECT_EQ(first_hash, page_translated_hash);
  EXPECT_NE(first_hash, page_scaled_hash);
  EXPECT_EQ(first_hash, raw_view_translated_hash);
  EXPECT_EQ(first_hash, z_translated_hash);
  EXPECT_TRUE(
    oxygen::renderer::internal::shadow_detail::DirectionalCacheMat4Equal(
      oxygen::renderer::internal::shadow_detail::
        BuildDirectionalAddressSpaceComparableLightView(
          first_metadata.light_view),
      oxygen::renderer::internal::shadow_detail::
        BuildDirectionalAddressSpaceComparableLightView(
          raw_view_translated_metadata.light_view)));
  EXPECT_TRUE(
    oxygen::renderer::internal::shadow_detail::DirectionalCacheMat4Equal(
      oxygen::renderer::internal::shadow_detail::
        BuildDirectionalAddressSpaceComparableLightView(
          first_metadata.light_view),
      oxygen::renderer::internal::shadow_detail::
        BuildDirectionalAddressSpaceComparableLightView(
          z_translated_metadata.light_view)));
  EXPECT_EQ(oxygen::renderer::internal::shadow_detail::
              ResolveDirectionalVirtualFeedbackAddressSpacePageShiftX(
                first_metadata, 0U),
    oxygen::renderer::internal::shadow_detail::
      ResolveDirectionalVirtualFeedbackAddressSpacePageShiftX(
        sub_page_translated_metadata, 0U));
  EXPECT_EQ(oxygen::renderer::internal::shadow_detail::
              ResolveDirectionalVirtualFeedbackAddressSpacePageShiftX(
                first_metadata, 0U),
    oxygen::renderer::internal::shadow_detail::
      ResolveDirectionalVirtualFeedbackAddressSpacePageShiftX(
        raw_view_translated_metadata, 0U));
  EXPECT_NE(oxygen::renderer::internal::shadow_detail::
              ResolveDirectionalVirtualFeedbackAddressSpacePageShiftX(
                first_metadata, 0U),
    oxygen::renderer::internal::shadow_detail::
      ResolveDirectionalVirtualFeedbackAddressSpacePageShiftX(
        page_translated_metadata, 0U));
  EXPECT_EQ(oxygen::renderer::internal::shadow_detail::
              ResolveDirectionalVirtualFeedbackAddressSpacePageShiftY(
                first_metadata, 0U),
    oxygen::renderer::internal::shadow_detail::
      ResolveDirectionalVirtualFeedbackAddressSpacePageShiftY(
        sub_page_translated_metadata, 0U));
  EXPECT_EQ(oxygen::renderer::internal::shadow_detail::
              ResolveDirectionalVirtualFeedbackAddressSpacePageShiftY(
                first_metadata, 0U),
    oxygen::renderer::internal::shadow_detail::
      ResolveDirectionalVirtualFeedbackAddressSpacePageShiftY(
        raw_view_translated_metadata, 0U));
  EXPECT_NE(oxygen::renderer::internal::shadow_detail::
              ResolveDirectionalVirtualFeedbackAddressSpacePageShiftY(
                first_metadata, 0U),
    oxygen::renderer::internal::shadow_detail::
      ResolveDirectionalVirtualFeedbackAddressSpacePageShiftY(
        page_translated_metadata, 0U));
}

//! Changing the authored first clip span must invalidate the previous clipmap
//! layout even when the camera and light direction stay fixed.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualCacheValidityRejectsClipmapLayoutChanges)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput first_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  auto second_sun = first_sun;
  second_sun.cascade_distances = { 64.0F, 96.0F, 128.0F, 160.0F };

  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 232 },
    view_constants, manager, shadow_casters, visible_receivers, &first_sun,
    std::chrono::milliseconds(16));
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 232 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_FALSE(first_introspection->directional_virtual_metadata.empty());
  const float first_clip_page_world
    = first_introspection->directional_virtual_metadata.front()
        .clip_metadata[0]
        .origin_page_scale.z;

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 232 }, true);
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 232 },
    view_constants, manager, shadow_casters, visible_receivers, &second_sun,
    std::chrono::milliseconds(16));
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 232 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_introspection, nullptr);
  ASSERT_FALSE(second_introspection->directional_virtual_metadata.empty());
  EXPECT_NE(first_clip_page_world,
    second_introspection->directional_virtual_metadata.front()
      .clip_metadata[0]
      .origin_page_scale.z);
  EXPECT_FALSE(second_introspection->cache_layout_compatible);
  EXPECT_FALSE(second_introspection->depth_guardband_valid);
  EXPECT_TRUE(std::ranges::all_of(
    second_introspection->clipmap_cache_status, [](const auto status) {
      return status
        == oxygen::renderer::DirectionalVirtualClipCacheStatus::kLayoutInvalid;
    }));
}

//! If clipmap panning is disabled, any snapped XY page-space shift must
//! invalidate per-clip cache reuse even when the rest of the layout stays
//! compatible.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPanningDisabledInvalidatesXYClipReuse)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 233 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 233 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_FALSE(first_introspection->directional_virtual_metadata.empty());

  const auto first_metadata
    = first_introspection->directional_virtual_metadata.front();
  const auto inverse_light_view = glm::inverse(first_metadata.light_view);
  const glm::vec3 world_origin
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const float fine_page_world
    = first_metadata.clip_metadata[0].origin_page_scale.z;
  const glm::vec3 world_shift_x
    = glm::vec3(
        inverse_light_view * glm::vec4(fine_page_world, 0.0F, 0.0F, 1.0F))
    - world_origin;

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 233 }, true);
  shadow_manager->SetVirtualDirectionalCacheControls(
    { .clipmap_panning_enabled = false, .force_invalidate = false });

  view_constants.SetCameraPosition(
    view_constants.GetCameraPosition() + world_shift_x);
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 233 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 233 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_introspection, nullptr);
  EXPECT_TRUE(second_introspection->cache_layout_compatible);
  EXPECT_TRUE(second_introspection->depth_guardband_valid);
  ASSERT_FALSE(second_introspection->clipmap_cache_valid.empty());
  ASSERT_FALSE(second_introspection->clipmap_cache_status.empty());
  EXPECT_FALSE(second_introspection->clipmap_cache_valid[0]);
  EXPECT_EQ(second_introspection->clipmap_cache_status[0],
    oxygen::renderer::DirectionalVirtualClipCacheStatus::kPanningDisabled);
}

//! A previous frame that never completed raster must not be treated as a valid
//! reusable directional cache source on the next frame.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualNeverRenderedPreviousFrameInvalidatesCache)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 234 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 234 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 234 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_introspection, nullptr);
  EXPECT_FALSE(second_introspection->cache_layout_compatible);
  EXPECT_FALSE(second_introspection->depth_guardband_valid);
  EXPECT_TRUE(std::ranges::all_of(second_introspection->clipmap_cache_valid,
    [](const bool valid) { return !valid; }));
  EXPECT_TRUE(std::ranges::all_of(
    second_introspection->clipmap_cache_status, [](const auto status) {
      return status
          == oxygen::renderer::DirectionalVirtualClipCacheStatus::kNeverRendered
        || status
          == oxygen::renderer::DirectionalVirtualClipCacheStatus::kNoPreviousFrame;
    }));
}

//! An explicit directional cache invalidate control must reject otherwise
//! reusable previous clipmap state for the next frame.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualForceInvalidateRejectsPreviousCache)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 235 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 235 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection, nullptr);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 235 }, true);
  shadow_manager->SetVirtualDirectionalCacheControls(
    { .clipmap_panning_enabled = true, .force_invalidate = true });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 235 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 235 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_introspection, nullptr);
  EXPECT_FALSE(second_introspection->cache_layout_compatible);
  EXPECT_FALSE(second_introspection->depth_guardband_valid);
  EXPECT_TRUE(std::ranges::all_of(second_introspection->clipmap_cache_valid,
    [](const bool valid) { return !valid; }));
  EXPECT_TRUE(std::ranges::all_of(
    second_introspection->clipmap_cache_status, [](const auto status) {
      return status
        == oxygen::renderer::DirectionalVirtualClipCacheStatus::
          kForceInvalidated;
    }));
}

//! Coarse fallback pages must track the visible receiver depth range, not the
//! camera far plane. A tiny near receiver should require fewer coarse pages
//! than an unbounded frustum with no receiver guidance.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualCoarseBackboneUsesVisibleReceiverDepthRange)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, -1000.0F, 5000.0F),
  };
  const std::array<glm::vec4, 1> near_receiver {
    glm::vec4(0.0F, 0.0F, -5.0F, 0.02F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 211 }, view_constants,
    manager, shadow_casters, near_receiver, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* bounded_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 211 });
  ASSERT_NE(bounded_introspection, nullptr);

  (void)shadow_manager->PublishForView(oxygen::ViewId { 212 }, view_constants,
    manager, shadow_casters, {}, &synthetic_sun, std::chrono::milliseconds(16));
  const auto* unbounded_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 212 });
  ASSERT_NE(unbounded_introspection, nullptr);

  EXPECT_GT(unbounded_introspection->coarse_backbone_page_count, 0U);
  EXPECT_LT(bounded_introspection->coarse_backbone_page_count,
    unbounded_introspection->coarse_backbone_page_count);
}




//! Accepted detail feedback is telemetry-only when the publish-only path has
//! not run current-frame GPU marking. In that state the view must stay
//! coarse-only instead of synthesizing publish-time fine coverage.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualUnseedableAcceptedDetailFeedbackStaysCoarseUntilGpuMarking)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly,
    oxygen::ShadowQualityTier::kMedium);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  const auto make_view_constants = [](const glm::vec3& camera_position,
                                     const SequenceNumber sequence,
                                     const Slot slot) {
    ViewConstants view_constants;
    view_constants
      .SetProjectionMatrix(
        glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
      .SetCameraPosition(camera_position);
    view_constants.SetFrameSequenceNumber(sequence, ViewConstants::kRenderer);
    view_constants.SetFrameSlot(slot, ViewConstants::kRenderer);
    return view_constants;
  };

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> huge_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.0F, 512.0F),
  };
  const std::array<glm::vec4, 1> focused_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 48.0F),
  };

  const glm::vec3 base_camera = glm::vec3(0.0F);
  auto view_constants
    = make_view_constants(base_camera, SequenceNumber { 1 }, Slot { 0 });

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 244 },
    view_constants, manager, huge_shadow_casters, focused_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 244 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());
  const auto first_layout = BuildVirtualFeedbackLayout(
    first_virtual_introspection->directional_virtual_metadata.front());
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 244 }, true);

  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto inverse_light_view = glm::inverse(first_metadata.light_view);
  const float fine_page_world
    = first_metadata.clip_metadata[0].origin_page_scale.z;
  const glm::vec3 world_origin
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::vec3 world_shift_x
    = glm::vec3(
        inverse_light_view * glm::vec4(fine_page_world, 0.0F, 0.0F, 1.0F))
    - world_origin;
  const glm::vec3 shifted_camera = base_camera + world_shift_x;

  view_constants
    = make_view_constants(shifted_camera, SequenceNumber { 2 }, Slot { 1 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 244 },
    view_constants, manager, huge_shadow_casters, focused_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 244 });
  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());
  const auto second_layout = BuildVirtualFeedbackLayout(
    second_virtual_introspection->directional_virtual_metadata.front());
  ASSERT_EQ(second_layout.directional_address_space_hash,
    first_layout.directional_address_space_hash);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 244 }, true);

  const auto clip_origin_delta_x
    = second_layout.clip_grid_origin_x[0] - first_layout.clip_grid_origin_x[0];
  const auto clip_origin_delta_y
    = second_layout.clip_grid_origin_y[0] - first_layout.clip_grid_origin_y[0];
  ASSERT_TRUE(clip_origin_delta_x != 0 || clip_origin_delta_y != 0);

  std::uint32_t edge_page_x = second_layout.pages_per_axis / 2U;
  std::uint32_t edge_page_y = second_layout.pages_per_axis / 2U;
  if (clip_origin_delta_x > 0) {
    edge_page_x = 0U;
  } else if (clip_origin_delta_x < 0) {
    edge_page_x = second_layout.pages_per_axis - 1U;
  } else if (clip_origin_delta_y > 0) {
    edge_page_y = 0U;
  } else {
    edge_page_y = second_layout.pages_per_axis - 1U;
  }
  const std::uint32_t edge_page_index
    = edge_page_y * second_layout.pages_per_axis + edge_page_x;
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 244 },
    MakeVirtualRequestFeedback(
      second_layout, SequenceNumber { 2 }, { edge_page_index }));

  const glm::vec3 third_camera = shifted_camera + world_shift_x * 4.0F;
  view_constants
    = make_view_constants(third_camera, SequenceNumber { 3 }, Slot { 2 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 2 });

  const auto third = shadow_manager->PublishForView(oxygen::ViewId { 244 },
    view_constants, manager, huge_shadow_casters, focused_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* third_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 244 });
  ASSERT_NE(third.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(third_virtual_introspection, nullptr);
  ASSERT_FALSE(third_virtual_introspection->directional_virtual_metadata.empty());

  const auto third_layout = BuildVirtualFeedbackLayout(
    third_virtual_introspection->directional_virtual_metadata.front());
  EXPECT_FALSE(third_virtual_introspection->used_request_feedback);
  EXPECT_EQ(third_virtual_introspection->feedback_requested_page_count, 0U);
  EXPECT_EQ(third_virtual_introspection->selected_page_count,
    third_virtual_introspection->coarse_backbone_page_count);
  EXPECT_EQ(CountMappedPagesInClip(third_layout,
              third_virtual_introspection->page_table_entries, 0U),
    0U);
}

//! Shifted feedback from an older source frame must still keep the legacy
//! current-frame delta-band reinforcement path disabled until remap exists.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualShiftedFeedbackDoesNotUseLegacyDeltaReinforcement)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly,
    oxygen::ShadowQualityTier::kUltra);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  const auto make_view_constants = [](const glm::vec3& camera_position,
                                     const glm::vec3& /*target*/,
                                     const SequenceNumber sequence,
                                     const Slot slot) {
    ViewConstants view_constants;
    view_constants
      .SetProjectionMatrix(
        glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
      .SetCameraPosition(camera_position);
    view_constants.SetFrameSequenceNumber(sequence, ViewConstants::kRenderer);
    view_constants.SetFrameSlot(slot, ViewConstants::kRenderer);
    return view_constants;
  };

  const glm::vec3 base_camera = glm::vec3(0.0F, -8.0F, 5.0F);
  const glm::vec3 base_target = glm::vec3(0.0F, 0.0F, 0.5F);
  auto view_constants = make_view_constants(
    base_camera, base_target, SequenceNumber { 1 }, Slot { 0 });

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 2> shadow_casters {
    glm::vec4(-0.9F, 0.0F, 0.5F, 0.5F),
    glm::vec4(0.9F, 0.0F, 1.5F, 1.5F),
  };
  const std::array<glm::vec4, 3> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 6.5F),
    glm::vec4(-0.9F, 0.0F, 0.5F, 0.5F),
    glm::vec4(0.9F, 0.0F, 1.5F, 1.5F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 139 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 139 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 139 }, true);

  const auto first_layout = BuildVirtualFeedbackLayout(
    first_virtual_introspection->directional_virtual_metadata.front());
  const auto requested_page = GlobalPageIndexForWorldPoint(
    first_virtual_introspection->directional_virtual_metadata.front(), 0U,
    glm::vec3(-0.9F, 0.0F, 0.5F));
  ASSERT_TRUE(requested_page.has_value());
  const std::array<std::uint32_t, 1> requested_pages { *requested_page };

  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto inverse_light_view = glm::inverse(first_metadata.light_view);
  const float fine_page_world
    = first_metadata.clip_metadata[0].origin_page_scale.z;
  const glm::vec3 world_origin
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::vec3 world_shift_x
    = glm::vec3(
        inverse_light_view * glm::vec4(fine_page_world, 0.0F, 0.0F, 1.0F))
    - world_origin;

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 139 },
    MakeVirtualRequestFeedback(first_layout, SequenceNumber { 1 },
      std::span<const std::uint32_t>(requested_pages.data(),
        requested_pages.size())));

  view_constants = make_view_constants(base_camera + world_shift_x,
    base_target + world_shift_x, SequenceNumber { 2 }, Slot { 1 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });
  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 139 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 139 });
  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(
    second_virtual_introspection->current_frame_reinforcement_page_count, 0U);
  EXPECT_EQ(
    second_virtual_introspection->current_frame_reinforcement_reference_frame,
    0U);

  const glm::vec3 far_shift = world_shift_x * 4.0F;
  view_constants = make_view_constants(base_camera + far_shift,
    base_target + far_shift, SequenceNumber { 5 }, Slot { 1 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 5 }, Slot { 1 });
  const auto fifth = shadow_manager->PublishForView(oxygen::ViewId { 139 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* fifth_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 139 });
  ASSERT_NE(fifth.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(fifth_virtual_introspection, nullptr);
  EXPECT_EQ(
    fifth_virtual_introspection->current_frame_reinforcement_reference_frame,
    0U);
  EXPECT_EQ(
    fifth_virtual_introspection->current_frame_reinforcement_page_count, 0U);
}

//! Caster movement should dirty only overlapping resident pages, leaving the
//! rest of the requested working set clean and reusable.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateSpatiallyInvalidatesDirtyPagesOnly)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kPreferVirtual);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 2> initial_shadow_casters {
    glm::vec4(-8.0F, 0.0F, 0.5F, 0.5F),
    glm::vec4(8.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 2> moved_shadow_casters {
    glm::vec4(-8.0F, 0.0F, 0.5F, 0.5F),
    glm::vec4(10.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 2> visible_receivers {
    glm::vec4(-8.0F, 0.0F, 0.0F, 0.1F),
    glm::vec4(8.0F, 0.0F, 0.0F, 0.1F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 32 },
    view_constants, manager, initial_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 32 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  EXPECT_EQ(first_virtual_introspection->resident_page_count, 0U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 32 }, true);

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 32 },
    view_constants, manager, moved_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16), 0x9001U);
  const auto* second_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 32 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 32 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_pending_raster_state, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_pending_raster_state->resolved_pages.size(), 0U);
  EXPECT_EQ(second_virtual_introspection->clean_page_count, 0U);
}

//! When far-view pressure exposes more virtual pages than the active page
//! budget can map, the planner should keep currently mapped requested pages
//! before rotating in newly requested pages.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStatePinsMappedRequestedPagesUnderBudgetPressure)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };

  const auto layout = ResolveVirtualFeedbackLayout(*shadow_manager, manager,
    view_constants, oxygen::ViewId { 124 }, shadow_casters, synthetic_sun);
  ASSERT_GT(layout.pages_per_level, 0U);
  const auto total_pages = layout.pages_per_level * layout.clip_level_count;
  const auto request_count = std::min<std::uint32_t>(total_pages, 2048U);
  ASSERT_GT(request_count, 1U);

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 24 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 24 }, true);

  std::vector<std::uint32_t> initial_requested_pages(request_count);
  std::iota(initial_requested_pages.begin(), initial_requested_pages.end(), 0U);
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 24 },
    MakeVirtualRequestFeedback(layout, SequenceNumber { 1 },
      std::span<const std::uint32_t>(
        initial_requested_pages.data(), initial_requested_pages.size())));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 24 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 24 });
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 24 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_pending_raster_state, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  EXPECT_TRUE(first_pending_raster_state->resolved_pages.empty());
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 24 }, true);

  std::vector<std::uint32_t> shifted_requested_pages(request_count);
  std::iota(shifted_requested_pages.begin(), shifted_requested_pages.end(), 1U);
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 24 },
    MakeVirtualRequestFeedback(layout, SequenceNumber { 2 },
      std::span<const std::uint32_t>(
        shifted_requested_pages.data(), shifted_requested_pages.size())));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 0 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 24 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 24 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 24 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_pending_raster_state, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_virtual_introspection->mapped_page_count, 0U);
  EXPECT_EQ(first_virtual_introspection->mapped_page_count, 0U);
  EXPECT_EQ(second_pending_raster_state->resolved_pages.size(), 0U);
}


//! Repeated near-compatible light-direction changes that still alter the
//! directional page address space must keep publishing the current frame state
//! instead of flashing back to an old stale publication. Background bootstrap
//! is still allowed before Phase 4 remap exists.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualRepeatedNearCompatibleDirectionChangesRejectStaleFallback)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly,
    oxygen::ShadowQualityTier::kLow);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  const auto make_view_constants = [](const glm::vec3& camera_position,
                                     const SequenceNumber sequence,
                                     const Slot slot) {
    ViewConstants view_constants;
    view_constants
      .SetProjectionMatrix(
        glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
      .SetCameraPosition(camera_position);
    view_constants.SetFrameSequenceNumber(sequence, ViewConstants::kRenderer);
    view_constants.SetFrameSlot(slot, ViewConstants::kRenderer);
    return view_constants;
  };

  const glm::vec3 base_camera = glm::vec3(0.0F);
  auto view_constants
    = make_view_constants(base_camera, SequenceNumber { 1 }, Slot { 0 });

  const ShadowManager::SyntheticSunShadowInput coherent_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> huge_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.0F, 512.0F),
  };
  const std::array<glm::vec4, 1> focused_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 48.0F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 243 },
    view_constants, manager, huge_shadow_casters, focused_visible_receivers,
    &coherent_sun, std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 243 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(first_virtual_introspection
      ->published_directional_virtual_metadata.empty());
  const auto first_published_metadata
    = first_virtual_introspection->published_directional_virtual_metadata
        .front();
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 243 }, true);

  const std::array<ShadowManager::SyntheticSunShadowInput, 3>
    near_compatible_suns {
      ShadowManager::SyntheticSunShadowInput {
        .enabled = true,
        .direction_ws = glm::normalize(glm::vec3(0.355F, -0.45F, -1.0F)),
        .bias = coherent_sun.bias,
        .normal_bias = coherent_sun.normal_bias,
        .resolution_hint = coherent_sun.resolution_hint,
        .cascade_count = coherent_sun.cascade_count,
        .distribution_exponent = coherent_sun.distribution_exponent,
        .cascade_distances = coherent_sun.cascade_distances,
      },
      ShadowManager::SyntheticSunShadowInput {
        .enabled = true,
        .direction_ws = glm::normalize(glm::vec3(0.360F, -0.45F, -1.0F)),
        .bias = coherent_sun.bias,
        .normal_bias = coherent_sun.normal_bias,
        .resolution_hint = coherent_sun.resolution_hint,
        .cascade_count = coherent_sun.cascade_count,
        .distribution_exponent = coherent_sun.distribution_exponent,
        .cascade_distances = coherent_sun.cascade_distances,
      },
      ShadowManager::SyntheticSunShadowInput {
        .enabled = true,
        .direction_ws = glm::normalize(glm::vec3(0.365F, -0.45F, -1.0F)),
        .bias = coherent_sun.bias,
        .normal_bias = coherent_sun.normal_bias,
        .resolution_hint = coherent_sun.resolution_hint,
        .cascade_count = coherent_sun.cascade_count,
        .distribution_exponent = coherent_sun.distribution_exponent,
        .cascade_distances = coherent_sun.cascade_distances,
      },
    };

  for (std::size_t i = 0U; i < near_compatible_suns.size(); ++i) {
    const auto sequence = SequenceNumber { 2U + static_cast<std::uint64_t>(i) };
    const auto slot = Slot { static_cast<std::uint32_t>((i + 1U) % 3U) };
    view_constants = make_view_constants(base_camera, sequence, slot);
    AdvanceRendererFrame(
      manager, *shadow_manager, view_constants, sequence, slot);

    const auto publication
      = shadow_manager->PublishForView(oxygen::ViewId { 243 }, view_constants,
        manager, huge_shadow_casters, focused_visible_receivers,
        &near_compatible_suns[i], std::chrono::milliseconds(16));
    const auto* virtual_introspection = ResolveVirtualViewIntrospection(
      *shadow_manager, oxygen::ViewId { 243 });

    ASSERT_NE(
      publication.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
    ASSERT_NE(virtual_introspection, nullptr);
    ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());
    ASSERT_FALSE(
      virtual_introspection->published_directional_virtual_metadata.empty());
    const auto published_metadata
      = virtual_introspection->published_directional_virtual_metadata.front();
    const auto current_metadata
      = virtual_introspection->directional_virtual_metadata.front();
    EXPECT_EQ(virtual_introspection->feedback_refinement_page_count, 0U);
    EXPECT_EQ(
      virtual_introspection->current_frame_reinforcement_page_count, 0U);
    EXPECT_EQ(oxygen::renderer::internal::shadow_detail::
                HashDirectionalVirtualFeedbackAddressSpace(current_metadata),
      oxygen::renderer::internal::shadow_detail::
        HashDirectionalVirtualFeedbackAddressSpace(published_metadata));
  }
}

//! Projection jitter must not perturb the directional VSM address space when
//! the unjittered camera frustum is unchanged.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualProjectionJitterKeepsDirectionalAddressSpaceStable)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly,
    oxygen::ShadowQualityTier::kLow);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  const auto make_view_constants = [](const glm::mat4& projection_matrix,
                                     const glm::mat4& stable_projection_matrix,
                                     const SequenceNumber sequence,
                                     const Slot slot) {
    ViewConstants view_constants;
    view_constants.SetProjectionMatrix(projection_matrix)
      .SetStableProjectionMatrix(stable_projection_matrix)
      .SetCameraPosition(glm::vec3(0.0F));
    view_constants.SetFrameSequenceNumber(sequence, ViewConstants::kRenderer);
    view_constants.SetFrameSlot(slot, ViewConstants::kRenderer);
    return view_constants;
  };

  const oxygen::ViewPort viewport {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = 2560.0F,
    .height = 1400.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  const auto stable_projection
    = glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F);
  const auto jittered_projection_a = oxygen::ApplyJitterToProjection(
    stable_projection, glm::vec2(0.5F, -0.5F), viewport);
  const auto jittered_projection_b = oxygen::ApplyJitterToProjection(
    stable_projection, glm::vec2(-0.5F, 0.5F), viewport);

  auto view_constants = make_view_constants(
    jittered_projection_a, stable_projection, SequenceNumber { 1 }, Slot { 0 });

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.08F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 247 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 247 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_FALSE(first_introspection->directional_virtual_metadata.empty());

  const auto first_address_space_hash = oxygen::renderer::internal::
    shadow_detail::HashDirectionalVirtualFeedbackAddressSpace(
      first_introspection->directional_virtual_metadata.front());
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 247 }, true);

  view_constants = make_view_constants(
    jittered_projection_b, stable_projection, SequenceNumber { 2 }, Slot { 1 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 247 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 247 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_introspection, nullptr);
  ASSERT_FALSE(second_introspection->directional_virtual_metadata.empty());
  EXPECT_TRUE(second_introspection->cache_layout_compatible);

  const auto second_address_space_hash = oxygen::renderer::internal::
    shadow_detail::HashDirectionalVirtualFeedbackAddressSpace(
      second_introspection->directional_virtual_metadata.front());
  EXPECT_EQ(first_address_space_hash, second_address_space_hash);
}

NOLINT_TEST_F(LightManagerTest,
  ViewConstants_ExplicitStableProjectionSurvivesLaterProjectionUpdates)
{
  ViewConstants view_constants;
  const auto projection_a
    = glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F);
  const auto projection_b
    = glm::perspectiveRH_ZO(glm::radians(50.0F), 16.0F / 9.0F, 0.1F, 200.0F);
  const auto stable_projection
    = glm::perspectiveRH_ZO(glm::radians(40.0F), 16.0F / 9.0F, 0.1F, 200.0F);

  view_constants.SetProjectionMatrix(projection_a)
    .SetStableProjectionMatrix(stable_projection)
    .SetProjectionMatrix(projection_b);

  EXPECT_TRUE(
    oxygen::renderer::internal::shadow_detail::DirectionalCacheMat4Equal(
      view_constants.GetProjectionMatrix(), projection_b));
  EXPECT_TRUE(
    oxygen::renderer::internal::shadow_detail::DirectionalCacheMat4Equal(
      view_constants.GetStableProjectionMatrix(), stable_projection));
}

NOLINT_TEST_F(
  LightManagerTest, ViewConstants_StableProjectionUpdatesAdvanceVersion)
{
  ViewConstants view_constants;
  const auto projection
    = glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F);
  const auto stable_projection
    = glm::perspectiveRH_ZO(glm::radians(40.0F), 16.0F / 9.0F, 0.1F, 200.0F);

  const auto initial_version = view_constants.GetVersion();
  view_constants.SetProjectionMatrix(projection);
  const auto after_projection_version = view_constants.GetVersion();
  view_constants.SetStableProjectionMatrix(stable_projection);
  const auto after_stable_projection_version = view_constants.GetVersion();

  EXPECT_GT(after_projection_version.value, initial_version.value);
  EXPECT_GT(
    after_stable_projection_version.value, after_projection_version.value);
}

//! A visually incompatible change must not republish the stale camera-fit
//! snapshot even if a coherent previous publication exists.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualLargeDirectionChangeRejectsLastCoherentPublishFallback)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly,
    oxygen::ShadowQualityTier::kLow);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  const auto make_view_constants = [](const glm::vec3& camera_position,
                                     const SequenceNumber sequence,
                                     const Slot slot) {
    ViewConstants view_constants;
    view_constants
      .SetProjectionMatrix(
        glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
      .SetCameraPosition(camera_position);
    view_constants.SetFrameSequenceNumber(sequence, ViewConstants::kRenderer);
    view_constants.SetFrameSlot(slot, ViewConstants::kRenderer);
    return view_constants;
  };

  const glm::vec3 base_camera = glm::vec3(0.0F);
  auto view_constants
    = make_view_constants(base_camera, SequenceNumber { 1 }, Slot { 0 });

  const ShadowManager::SyntheticSunShadowInput coherent_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const ShadowManager::SyntheticSunShadowInput shifted_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.62F, -0.25F, -1.0F)),
    .bias = coherent_sun.bias,
    .normal_bias = coherent_sun.normal_bias,
    .resolution_hint = coherent_sun.resolution_hint,
    .cascade_count = coherent_sun.cascade_count,
    .distribution_exponent = coherent_sun.distribution_exponent,
    .cascade_distances = coherent_sun.cascade_distances,
  };
  const std::array<glm::vec4, 1> huge_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.0F, 512.0F),
  };
  const std::array<glm::vec4, 1> huge_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 160.0F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 244 },
    view_constants, manager, huge_shadow_casters, huge_visible_receivers,
    &coherent_sun, std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 244 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 244 }, true);

  view_constants
    = make_view_constants(base_camera, SequenceNumber { 2 }, Slot { 1 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 244 },
    view_constants, manager, huge_shadow_casters, huge_visible_receivers,
    &shifted_sun, std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 244 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());
  ASSERT_FALSE(second_virtual_introspection
      ->published_directional_virtual_metadata.empty());

  const auto current_metadata
    = second_virtual_introspection->directional_virtual_metadata.front();
  const auto published_metadata
    = second_virtual_introspection->published_directional_virtual_metadata
        .front();
  EXPECT_EQ(oxygen::renderer::internal::shadow_detail::
              HashDirectionalVirtualFeedbackAddressSpace(current_metadata),
    oxygen::renderer::internal::shadow_detail::
      HashDirectionalVirtualFeedbackAddressSpace(published_metadata));
}


//! Virtual pages remain pending until the raster pass executes, so same-frame
//! republishes must not discard the initial virtual raster work.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateKeepsPendingRasterJobsUntilExecuted)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 13 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 13 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_pending_raster_state, nullptr);
  EXPECT_TRUE(first_pending_raster_state->resolved_pages.empty());
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 13 });
  ASSERT_NE(first_virtual_introspection, nullptr);
  EXPECT_EQ(first_virtual_introspection->pending_page_count, 0U);

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 13 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 13 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 13 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_pending_raster_state, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_pending_raster_state->resolved_pages.size(), 0U);
  EXPECT_EQ(second_virtual_introspection->pending_page_count, 0U);
}

//! Patching the published view-frame slot must leave the explicit resolve
//! contract intact without relying on a CPU pending-job mirror.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateSurvivesViewFrameSlotPatchWithoutCpuMirror)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 15 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);

  constexpr auto kExpectedSlot = oxygen::engine::BindlessViewFrameBindingsSlot {
    oxygen::ShaderVisibleIndex { 42U }
  };
  shadow_manager->SetPublishedViewFrameBindingsSlot(
    oxygen::ViewId { 15 }, kExpectedSlot);

  const auto* pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 15 });
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 15 });
  ASSERT_NE(pending_raster_state, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  EXPECT_TRUE(pending_raster_state->resolved_pages.empty());
  EXPECT_EQ(virtual_introspection->pending_raster_page_count, 0U);
  EXPECT_EQ(pending_raster_state->depth_texture,
    shadow_manager->GetVirtualShadowDepthTexture().get());
}

//! Virtual page reuse must invalidate shadow contents when caster inputs
//! change, even if the snapped clip metadata and requested pages stay the same.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateRerasterizesWhenCasterInputsChange)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kPreferVirtual);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> initial_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> updated_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.8F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 12 },
    view_constants, manager, initial_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* first_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 12 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_pending_raster_state, nullptr);
  EXPECT_TRUE(first_pending_raster_state->resolved_pages.empty());

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 12 },
    view_constants, manager, updated_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* second_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 12 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 12 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_pending_raster_state, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_pending_raster_state->resolved_pages.size(), 0U);
  EXPECT_EQ(second_virtual_introspection->pending_raster_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->pending_page_count, 0U);
  EXPECT_EQ(second.virtual_shadow_physical_pool_srv,
    first.virtual_shadow_physical_pool_srv);
}

//! Physical-pool growth must not reuse iterators into the cleared per-view
//! cache.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateSurvivesPhysicalPoolRecreation)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 14 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 14 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(33));
  const auto* second_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 14 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    second.virtual_shadow_physical_pool_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_pending_raster_state, nullptr);
  EXPECT_TRUE(second_pending_raster_state->resolved_pages.empty());
}

//! Shadow publication is reused for identical inputs and invalidated when
//! shadow-relevant inputs change within the same frame.
NOLINT_TEST_F(
  LightManagerTest, ShadowManagerPublishForView_InvalidatesWhenViewInputsChange)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto node = CreateNode("dir", /*visible=*/true, /*casts_shadows=*/true);
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::DirectionalLight>();
  auto& light = impl->get().GetComponent<oxygen::scene::DirectionalLight>();
  light.Common().casts_shadows = true;
  UpdateTransforms(node);

  manager.CollectFromNode(impl->get());
  manager.EnsureFrameResources();

  auto shadow_manager = CreateShadowManager();
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const auto first = shadow_manager->PublishForView(
    oxygen::ViewId { 9 }, view_constants, manager);
  const auto second = shadow_manager->PublishForView(
    oxygen::ViewId { 9 }, view_constants, manager);
  EXPECT_EQ(
    first.shadow_instance_metadata_srv, second.shadow_instance_metadata_srv);
  EXPECT_EQ(first.directional_shadow_metadata_srv,
    second.directional_shadow_metadata_srv);

  view_constants.SetCameraPosition(glm::vec3(0.0F, -8.0F, 3.0F));
  const auto third = shadow_manager->PublishForView(
    oxygen::ViewId { 9 }, view_constants, manager);
  EXPECT_NE(
    first.shadow_instance_metadata_srv, third.shadow_instance_metadata_srv);
  EXPECT_NE(first.directional_shadow_metadata_srv,
    third.directional_shadow_metadata_srv);
}

//! Virtual shadow page reuse must be invalidated when caster content changes,
//! even if coarse caster/receiver bounds stay unchanged.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageStateInvalidatesWhenCasterContentChanges)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 19 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16), 0x1111U);
  const auto* first_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 19 });
  ASSERT_NE(first.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_pending_raster_state, nullptr);
  EXPECT_TRUE(first_pending_raster_state->resolved_pages.empty());

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 19 }, true);
  const auto* executed_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 19 });
  ASSERT_NE(executed_pending_raster_state, nullptr);
  EXPECT_TRUE(executed_pending_raster_state->resolved_pages.empty());

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 19 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16), 0x2222U);
  const auto* second_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 19 });
  ASSERT_NE(second.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_pending_raster_state, nullptr);
  EXPECT_TRUE(second_pending_raster_state->resolved_pages.empty());
}

//! The explicit current-frame resolve stage now materializes the authoritative
//! resolved-page contract from backend-private pending state.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualResolveStagePublishesResolvedPageContract)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 232 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 232 });
  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 232 });
  const auto* pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 232 });

  ASSERT_NE(
    published.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(introspection, nullptr);
  ASSERT_NE(pending_raster_state, nullptr);
  ASSERT_EQ(pending_raster_state->resolved_pages.size(),
    introspection->pending_raster_page_count);
}

//! Virtual VSM getters are now observation-only. They must not implicitly run
//! current-frame resolve work because page allocation, resolved-page
//! materialization, and page-table upload ordering belong to the explicit
//! resolve stage.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualTryGetDoesNotAutoResolveCurrentFrame)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 235 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(
    published.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);

  const auto* pre_resolve_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 235 });
  ASSERT_NE(pre_resolve_introspection, nullptr);
  EXPECT_EQ(pre_resolve_introspection->mapped_page_count, 0U);
  EXPECT_EQ(pre_resolve_introspection->pending_raster_page_count, 0U);

  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 235 });
  const auto* post_resolve_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 235 });
  ASSERT_NE(post_resolve_introspection, nullptr);
  EXPECT_EQ(post_resolve_introspection->mapped_page_count, 0U);
  EXPECT_EQ(post_resolve_introspection->pending_raster_page_count, 0U);

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 235 }, true);
  const auto* published_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 235 });
  ASSERT_NE(published_introspection, nullptr);
  EXPECT_EQ(published_introspection->resident_page_count, 0U);
}

//! Persistent GPU residency bridge resources are now created by the explicit
//! resolve stage, not by publish-time observation exports.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualResolveMaterializesGpuResidencyResources)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 236 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(
    published.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);

  const auto* pre_resolve_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 236 });
  ASSERT_NE(pre_resolve_introspection, nullptr);
  EXPECT_FALSE(pre_resolve_introspection->has_persistent_gpu_residency_state);
  EXPECT_EQ(pre_resolve_introspection->physical_page_metadata_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_EQ(pre_resolve_introspection->physical_page_lists_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_EQ(
    pre_resolve_introspection->resolve_stats_srv, kInvalidShaderVisibleIndex);

  const auto* resolved_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 236 });
  ASSERT_NE(resolved_introspection, nullptr);
  EXPECT_TRUE(resolved_introspection->has_persistent_gpu_residency_state);
  EXPECT_NE(resolved_introspection->physical_page_metadata_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_NE(resolved_introspection->physical_page_lists_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_NE(
    resolved_introspection->resolve_stats_srv, kInvalidShaderVisibleIndex);
}

//! Marking virtual raster execution must not become a backdoor
//! current-frame resolve path; only the explicit resolve stage may author
//! allocation / page-table state for the frame.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualMarkRenderedDoesNotAutoResolveCurrentFrame)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 2> shadow_casters {
    glm::vec4(-0.75F, 0.0F, 0.5F, 0.5F),
    glm::vec4(0.75F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 2> visible_receivers {
    glm::vec4(-0.75F, 0.0F, 0.0F, 0.10F),
    glm::vec4(0.75F, 0.0F, 0.0F, 0.10F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 233 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(
    published.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);

  const auto* pre_mark_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 233 });
  ASSERT_NE(pre_mark_introspection, nullptr);
  EXPECT_EQ(pre_mark_introspection->mapped_page_count, 0U);
  EXPECT_EQ(pre_mark_introspection->pending_raster_page_count, 0U);

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 233 }, true);

  const auto* post_mark_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 233 });
  ASSERT_NE(post_mark_introspection, nullptr);
  EXPECT_EQ(post_mark_introspection->mapped_page_count, 0U);
  EXPECT_EQ(post_mark_introspection->pending_raster_page_count, 0U);
  EXPECT_EQ(post_mark_introspection->resident_page_count, 0U);

  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 233 });
  const auto* pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 233 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_NE(pending_raster_state, nullptr);
  EXPECT_EQ(introspection->resident_page_count, 0U);
  EXPECT_EQ(introspection->mapped_page_count, 0U);
  EXPECT_EQ(introspection->pending_raster_page_count, 0U);
  EXPECT_EQ(introspection->pending_page_count, 0U);
  EXPECT_EQ(introspection->clean_page_count, 0U);
  EXPECT_TRUE(pending_raster_state->resolved_pages.empty());
}

//! Republishing the same view before resolve must not let publish-time view
//! state construction mutate residency. The authoritative resident snapshot
//! stays deferred behind resolve, and if the camera returns to the previously
//! resolved address space before resolve runs, the old clean pages should still
//! be reused instead of being dropped or reallocated.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualUnresolvedRepublishRetainsAuthoritativeResidentSnapshot)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 2> shadow_casters {
    glm::vec4(-1.0F, 0.0F, 0.5F, 0.5F),
    glm::vec4(1.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 2> visible_receivers {
    glm::vec4(-1.0F, 0.0F, 0.0F, 0.10F),
    glm::vec4(1.0F, 0.0F, 0.0F, 0.10F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 234 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_resolved
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 234 });
  ASSERT_NE(first_resolved, nullptr);
  EXPECT_EQ(first_resolved->resident_page_count, 0U);
  EXPECT_EQ(first_resolved->mapped_page_count, 0U);
  const auto first_mapped_page_count = first_resolved->mapped_page_count;
  const auto first_resident_page_count = first_resolved->resident_page_count;
  const std::vector<oxygen::renderer::VirtualShadowPhysicalPageMetadata>
    first_physical_metadata(first_resolved->physical_page_metadata_entries.begin(),
      first_resolved->physical_page_metadata_entries.end());

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 234 }, true);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });
  view_constants.SetCameraPosition(glm::vec3(18.0F, -6.0F, 3.0F));

  (void)shadow_manager->PublishForView(oxygen::ViewId { 234 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_pre_resolve
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 234 });
  ASSERT_NE(second_pre_resolve, nullptr);
  EXPECT_EQ(second_pre_resolve->resident_page_count, 0U);
  EXPECT_EQ(second_pre_resolve->mapped_page_count, 0U);
  EXPECT_EQ(second_pre_resolve->pending_raster_page_count, 0U);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 2 });
  view_constants.SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));

  (void)shadow_manager->PublishForView(oxygen::ViewId { 234 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* third_resolved
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 234 });
  const auto* third_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 234 });
  ASSERT_NE(third_resolved, nullptr);
  ASSERT_NE(third_pending_raster_state, nullptr);
  EXPECT_EQ(third_resolved->allocated_page_count, 0U);
  EXPECT_EQ(third_resolved->pending_raster_page_count, 0U);
  EXPECT_EQ(third_resolved->mapped_page_count, first_mapped_page_count);
  EXPECT_EQ(third_resolved->resident_page_count, first_resident_page_count);
  EXPECT_EQ(third_resolved->clean_page_count, first_resident_page_count);
  EXPECT_TRUE(third_pending_raster_state->resolved_pages.empty());
  ASSERT_EQ(third_resolved->physical_page_metadata_entries.size(),
    first_physical_metadata.size());
  for (std::size_t i = 0U; i < first_physical_metadata.size(); ++i) {
    EXPECT_EQ(third_resolved->physical_page_metadata_entries[i].resident_key,
      first_physical_metadata[i].resident_key);
    EXPECT_EQ(third_resolved->physical_page_metadata_entries[i].page_flags,
      first_physical_metadata[i].page_flags);
    EXPECT_EQ(
      third_resolved->physical_page_metadata_entries[i].packed_atlas_tile_coords,
      first_physical_metadata[i].packed_atlas_tile_coords);
  }
}

//! The current page-table publication slice owns a persistent per-view GPU
//! buffer, so repeated publications for the same view keep the same bindless
//! slot instead of allocating transient page-table resources every frame.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageTablePublicationUsesStablePerViewGpuBuffer)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 26 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16), 0x1111U);
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 26 });
  ASSERT_NE(first.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_GT(first_introspection->page_table_entries.size(), 0U);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 26 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16), 0x2222U);
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 26 });
  ASSERT_NE(second.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_introspection, nullptr);
  EXPECT_EQ(
    second.virtual_shadow_page_table_srv, first.virtual_shadow_page_table_srv);
  ASSERT_EQ(second_introspection->page_table_entries.size(),
    first_introspection->page_table_entries.size());
}

//! Phase 6 removes CPU-authored page-table uploads from the live path. Page
//! management must expose GPU-writeable bindings for the resolve pass instead
//! of copying the CPU page-table mirror into the shader-visible buffer.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPrepareVirtualPageTableResources_DoesNotUploadCpuPageTableEntries)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 234 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* metadata
    = shadow_manager->TryGetVirtualDirectionalMetadata(oxygen::ViewId { 234 });
  ASSERT_NE(metadata, nullptr);
  const auto expected_copy_size
    = static_cast<std::size_t>(metadata->clip_level_count)
    * static_cast<std::size_t>(metadata->pages_per_axis)
    * static_cast<std::size_t>(metadata->pages_per_axis)
    * sizeof(std::uint32_t);
  const auto* page_management_bindings
    = shadow_manager->TryGetVirtualPageManagementBindings(
      oxygen::ViewId { 234 });
  ASSERT_NE(page_management_bindings, nullptr);
  const auto* published
    = shadow_manager->TryGetFramePublication(oxygen::ViewId { 234 });
  ASSERT_NE(published, nullptr);
  ASSERT_NE(published->virtual_shadow_page_table_srv,
    oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(page_management_bindings->page_table_srv,
    oxygen::kInvalidShaderVisibleIndex);
  ASSERT_NE(page_management_bindings->page_table_uav,
    oxygen::kInvalidShaderVisibleIndex);
  EXPECT_EQ(page_management_bindings->page_table_srv,
    published->virtual_shadow_page_table_srv);

  GfxPtr()->buffer_log_ = {};
  auto recorder = GfxPtr()->AcquireCommandRecorder(
    SingleQueueStrategy().KeyFor(oxygen::graphics::QueueRole::kGraphics),
    "VirtualPageTableUploadBeforeResolve", false);
  ASSERT_NE(recorder, nullptr);

  shadow_manager->PrepareVirtualPageTableResources(
    oxygen::ViewId { 234 }, *recorder);

  const auto& pre_resolve_copy_log = GfxPtr()->buffer_log_;
  const auto pre_resolve_page_table_copy_it = std::find_if(
    pre_resolve_copy_log.copies.begin(), pre_resolve_copy_log.copies.end(),
    [expected_copy_size](
      const oxygen::renderer::testing::BufferCommandLog::CopyEvent& copy) {
      return copy.size == expected_copy_size;
    });
  EXPECT_EQ(pre_resolve_page_table_copy_it, pre_resolve_copy_log.copies.end());

  const auto* introspection = ResolveAndPublishVirtualViewIntrospection(
    *shadow_manager, oxygen::ViewId { 234 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_FALSE(introspection->page_table_entries.empty());
  ASSERT_GT(introspection->page_table_entries.size(), 0U);

  GfxPtr()->buffer_log_ = {};
  recorder = GfxPtr()->AcquireCommandRecorder(
    SingleQueueStrategy().KeyFor(oxygen::graphics::QueueRole::kGraphics),
    "VirtualPageTableUploadAfterResolve", false);
  ASSERT_NE(recorder, nullptr);

  shadow_manager->PrepareVirtualPageTableResources(
    oxygen::ViewId { 234 }, *recorder);

  const auto& copy_log = GfxPtr()->buffer_log_;
  const auto page_table_copy_it
    = std::find_if(copy_log.copies.begin(), copy_log.copies.end(),
      [expected_copy_size](
        const oxygen::renderer::testing::BufferCommandLog::CopyEvent& copy) {
        return copy.size == expected_copy_size;
      });
  EXPECT_EQ(page_table_copy_it, copy_log.copies.end());
}

NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPrepareVirtualPageTableResources_DoesNotUploadCpuPageFlags)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 2> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
    glm::vec4(0.0F, 2.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 235 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(
    published.virtual_shadow_page_flags_srv, kInvalidShaderVisibleIndex);

  const auto* introspection = ResolveAndPublishVirtualViewIntrospection(
    *shadow_manager, oxygen::ViewId { 235 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_FALSE(introspection->page_flags_entries.empty());
  ASSERT_EQ(introspection->page_flags_entries.size(),
    introspection->page_table_entries.size());
  ASSERT_FALSE(introspection->directional_virtual_metadata.empty());

  const auto expected_copy_size
    = introspection->page_flags_entries.size() * sizeof(std::uint32_t);
  const auto* page_management_bindings
    = shadow_manager->TryGetVirtualPageManagementBindings(
      oxygen::ViewId { 235 });
  ASSERT_NE(page_management_bindings, nullptr);
  ASSERT_NE(
    page_management_bindings->page_flags_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    page_management_bindings->page_flags_uav, kInvalidShaderVisibleIndex);
  EXPECT_EQ(page_management_bindings->page_flags_srv,
    published.virtual_shadow_page_flags_srv);

  GfxPtr()->buffer_log_ = {};
  auto recorder = GfxPtr()->AcquireCommandRecorder(
    SingleQueueStrategy().KeyFor(oxygen::graphics::QueueRole::kGraphics),
    "VirtualPageFlagsUploadAfterResolve", false);
  ASSERT_NE(recorder, nullptr);

  shadow_manager->PrepareVirtualPageTableResources(
    oxygen::ViewId { 235 }, *recorder);

  const auto& copy_log = GfxPtr()->buffer_log_;
  const auto page_flags_copy_it
    = std::find_if(copy_log.copies.begin(), copy_log.copies.end(),
      [expected_copy_size](
        const oxygen::renderer::testing::BufferCommandLog::CopyEvent& copy) {
        return copy.size == expected_copy_size;
      });
  EXPECT_EQ(page_flags_copy_it, copy_log.copies.end());
  for (std::size_t i = 0U; i < introspection->page_flags_entries.size(); ++i) {
    if (introspection->page_table_entries[i] == 0U) {
      EXPECT_EQ(introspection->page_flags_entries[i], 0U);
    }
  }
}

//! Phase 7 live resolved raster scheduling must consume current uncached pages
//! without suppressing valid reused current pages from the published page
//! table.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualSameFrameMarkedPagesDriveResolvedRasterSchedule)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 2> shadow_casters {
    glm::vec4(-0.25F, 0.0F, 0.5F, 0.45F),
    glm::vec4(1.5F, 0.0F, 0.5F, 0.45F),
  };
  const std::array<glm::vec4, 2> visible_receivers {
    glm::vec4(-0.25F, 0.0F, 0.0F, 0.2F),
    glm::vec4(1.5F, 0.0F, 0.0F, 0.2F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 245 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 245 });
  const auto* introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 245 });
  ASSERT_NE(pending_raster_state, nullptr);
  ASSERT_NE(introspection, nullptr);
  EXPECT_EQ(
    pending_raster_state->resolved_pages.size(), introspection->pending_raster_page_count);
  EXPECT_TRUE(pending_raster_state->resolved_pages.empty());
}

//! Accepted feedback is telemetry-only on the publish/resolved path. Until the
//! current frame runs GPU marking, resolve must not materialize a new receiver
//! fine page from previous-frame feedback alone.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualAcceptedFineFeedbackDoesNotPublishReceiverFinePageAtResolveTime)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -10.0F, 4.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> bootstrap_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.45F),
  };
  const std::array<glm::vec4, 1> bootstrap_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.2F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 248 }, view_constants,
    manager, bootstrap_shadow_casters, bootstrap_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 248 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());

  const auto& bootstrap_metadata
    = bootstrap_introspection->directional_virtual_metadata.front();
  const auto center_page_x = bootstrap_metadata.pages_per_axis / 2U;
  const auto center_page_y = bootstrap_metadata.pages_per_axis / 2U;
  ASSERT_GT(center_page_x, 6U);
  ASSERT_LT(center_page_x + 7U, bootstrap_metadata.pages_per_axis);

  const auto first_world = DirectionalVirtualPageWorldCenter(
    bootstrap_metadata, 0U, center_page_x - 6U, center_page_y, 0.5F);
  const auto second_world = DirectionalVirtualPageWorldCenter(
    bootstrap_metadata, 0U, center_page_x + 6U, center_page_y, 0.5F);

  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 248 });
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 248 }, true);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const std::array<glm::vec4, 1> first_shadow_casters {
    glm::vec4(first_world, 0.45F),
  };
  const std::array<glm::vec4, 1> first_visible_receivers {
    glm::vec4(first_world.x, first_world.y, first_world.z - 0.5F, 0.2F),
  };
  (void)shadow_manager->PublishForView(oxygen::ViewId { 248 }, view_constants,
    manager, first_shadow_casters, first_visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 248 });
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 248 });
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_FALSE(first_introspection->directional_virtual_metadata.empty());

  const auto first_page_index = GlobalPageIndexForWorldPoint(
    first_introspection->directional_virtual_metadata.front(), 0U, first_world);
  ASSERT_TRUE(first_page_index.has_value());
  const auto first_layout = BuildVirtualFeedbackLayout(
    first_introspection->directional_virtual_metadata.front());
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 248 }, true);
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 248 },
    MakeVirtualRequestFeedback(
      first_layout, SequenceNumber { 2 }, { *first_page_index }));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 2 });

  const std::array<glm::vec4, 1> second_shadow_casters {
    glm::vec4(second_world, 0.45F),
  };
  const std::array<glm::vec4, 1> second_visible_receivers {
    glm::vec4(second_world.x, second_world.y, second_world.z - 0.5F, 0.2F),
  };
  (void)shadow_manager->PublishForView(oxygen::ViewId { 248 }, view_constants,
    manager, second_shadow_casters, second_visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 248 });
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 248 });
  ASSERT_NE(second_introspection, nullptr);
  ASSERT_FALSE(second_introspection->directional_virtual_metadata.empty());

  const auto second_page_index = GlobalPageIndexForWorldPoint(
    second_introspection->directional_virtual_metadata.front(), 0U,
    second_world);
  ASSERT_TRUE(second_page_index.has_value());
  ASSERT_LT(
    *second_page_index, second_introspection->page_table_entries.size());

  EXPECT_FALSE(second_introspection->used_request_feedback);
  EXPECT_EQ(second_introspection->feedback_requested_page_count, 0U);
  EXPECT_FALSE(oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
    second_introspection->page_table_entries[*second_page_index]));
}

//! Accepted fine feedback remains telemetry-only on stable zero-shift frames.
//! Without current-frame GPU marking, publish-time selection stays coarse-only
//! and does not materialize current fine pages.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualStableAcceptedFineFeedbackStaysCoarseUntilGpuMarking)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -10.0F, 4.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> bootstrap_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.45F),
  };
  const std::array<glm::vec4, 1> bootstrap_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.2F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 249 }, view_constants,
    manager, bootstrap_shadow_casters, bootstrap_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 249 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());

  const auto& bootstrap_metadata
    = bootstrap_introspection->directional_virtual_metadata.front();
  const auto center_page_x = bootstrap_metadata.pages_per_axis / 2U;
  const auto center_page_y = bootstrap_metadata.pages_per_axis / 2U;
  ASSERT_GT(center_page_x, 6U);

  const auto world = DirectionalVirtualPageWorldCenter(
    bootstrap_metadata, 0U, center_page_x - 6U, center_page_y, 0.5F);

  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 249 });
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 249 }, true);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(world, 0.45F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(world.x, world.y, world.z - 0.5F, 0.2F),
  };
  (void)shadow_manager->PublishForView(oxygen::ViewId { 249 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 249 });
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 249 });
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_FALSE(first_introspection->directional_virtual_metadata.empty());

  const auto page_index = GlobalPageIndexForWorldPoint(
    first_introspection->directional_virtual_metadata.front(), 0U, world);
  ASSERT_TRUE(page_index.has_value());
  const auto layout = BuildVirtualFeedbackLayout(
    first_introspection->directional_virtual_metadata.front());
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 249 }, true);
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 249 },
    MakeVirtualRequestFeedback(layout, SequenceNumber { 2 }, { *page_index }));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 2 });

  (void)shadow_manager->PublishForView(oxygen::ViewId { 249 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 249 });
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 249 });
  ASSERT_NE(second_introspection, nullptr);
  ASSERT_FALSE(second_introspection->directional_virtual_metadata.empty());

  EXPECT_FALSE(second_introspection->used_request_feedback);
  EXPECT_EQ(second_introspection->feedback_requested_page_count, 0U);
  EXPECT_EQ(second_introspection->same_frame_detail_page_count, 0U);
  EXPECT_EQ(second_introspection->selected_page_count,
    second_introspection->coarse_backbone_page_count);
  EXPECT_FALSE(oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
    second_introspection->page_table_entries[*page_index]));
}

//! Stable zero-shift frames without accepted fine feedback still defer fine
//! coverage to GPU marking. Publish-time resolve must stay coarse-only instead
//! of synthesizing same-frame fine pages on CPU.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualStableNoFeedbackLeavesFineCoverageDeferredToGpuMarking)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -10.0F, 4.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> bootstrap_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.45F),
  };
  const std::array<glm::vec4, 1> bootstrap_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.2F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 250 }, view_constants,
    manager, bootstrap_shadow_casters, bootstrap_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 250 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());

  const auto& bootstrap_metadata
    = bootstrap_introspection->directional_virtual_metadata.front();
  const auto center_page_x = bootstrap_metadata.pages_per_axis / 2U;
  const auto center_page_y = bootstrap_metadata.pages_per_axis / 2U;
  ASSERT_GT(center_page_x, 6U);

  const auto world = DirectionalVirtualPageWorldCenter(
    bootstrap_metadata, 0U, center_page_x - 6U, center_page_y, 0.5F);

  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 250 });
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 250 }, true);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(world, 0.45F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(world.x, world.y, world.z - 0.5F, 0.2F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 250 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 250 });
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 250 });
  ASSERT_NE(second_introspection, nullptr);
  ASSERT_FALSE(second_introspection->directional_virtual_metadata.empty());

  const auto page_index = GlobalPageIndexForWorldPoint(
    second_introspection->directional_virtual_metadata.front(), 0U, world);
  ASSERT_TRUE(page_index.has_value());
  ASSERT_LT(*page_index, second_introspection->page_table_entries.size());

  EXPECT_FALSE(second_introspection->used_request_feedback);
  EXPECT_EQ(second_introspection->same_frame_detail_page_count, 0U);
  EXPECT_EQ(second_introspection->selected_page_count,
    second_introspection->coarse_backbone_page_count);
  EXPECT_FALSE(oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
    second_introspection->page_table_entries[*page_index]));
}

//! Without accepted fine feedback, publish-time bootstrap stays coarse-only
//! under huge caster bounds. Fine coverage remains deferred until GPU marking
//! runs, but receiver visibility still constrains the coarse selection.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualNoFeedbackBootstrapStaysCoarseUntilGpuMarking)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -10.0F, 4.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> huge_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.0F, 512.0F),
  };
  const std::array<glm::vec4, 1> focused_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 48.0F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 262 }, view_constants,
    manager, huge_shadow_casters, focused_visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* focused_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 262 });
  ASSERT_NE(focused_introspection, nullptr);
  ASSERT_FALSE(focused_introspection->directional_virtual_metadata.empty());

  (void)shadow_manager->PublishForView(oxygen::ViewId { 263 }, view_constants,
    manager, huge_shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* unbounded_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 263 });
  ASSERT_NE(unbounded_introspection, nullptr);
  ASSERT_FALSE(unbounded_introspection->directional_virtual_metadata.empty());

  EXPECT_FALSE(focused_introspection->used_request_feedback);
  EXPECT_FALSE(unbounded_introspection->used_request_feedback);
  EXPECT_EQ(focused_introspection->same_frame_detail_page_count, 0U);
  EXPECT_EQ(unbounded_introspection->same_frame_detail_page_count, 0U);
  EXPECT_LT(
    focused_introspection->selected_page_count, unbounded_introspection->selected_page_count);

  const auto focused_page_index = CurrentLodGlobalPageIndexForWorldPoint(
    focused_introspection->directional_virtual_metadata.front(),
    focused_introspection->page_table_entries, glm::vec3(0.0F));
  EXPECT_FALSE(focused_page_index.has_value());
}

//! Cold bootstrap is allowed to remain incomplete. Before accepted fine
//! feedback exists, receiver-only fine pages must stay unpublished instead of
//! becoming blank current pages or coarse fallback aliases. That keeps the very
//! first scene load in no-page rather than showing obviously wrong garbage.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualNoFeedbackBootstrapLeavesReceiverOnlyFinePageUnpublished)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -10.0F, 4.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> bootstrap_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.45F),
  };
  const std::array<glm::vec4, 1> bootstrap_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.2F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 264 }, view_constants,
    manager, bootstrap_shadow_casters, bootstrap_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 264 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());

  const auto& bootstrap_metadata
    = bootstrap_introspection->directional_virtual_metadata.front();
  const auto center_page_x = bootstrap_metadata.pages_per_axis / 2U;
  const auto center_page_y = bootstrap_metadata.pages_per_axis / 2U;
  ASSERT_GT(center_page_x, 8U);
  ASSERT_LT(center_page_x + 9U, bootstrap_metadata.pages_per_axis);

  const auto caster_world = DirectionalVirtualPageWorldCenter(
    bootstrap_metadata, 0U, center_page_x - 8U, center_page_y, 0.5F);
  const auto receiver_world = DirectionalVirtualPageWorldCenter(
    bootstrap_metadata, 0U, center_page_x + 8U, center_page_y, 0.5F);

  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 264 });
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 264 }, true);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(caster_world, 0.45F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(receiver_world.x, receiver_world.y, receiver_world.z - 0.5F,
      0.2F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 264 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 264 });
  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 264 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_FALSE(introspection->directional_virtual_metadata.empty());

  const auto receiver_page_index = GlobalPageIndexForWorldPoint(
    introspection->directional_virtual_metadata.front(), 0U, receiver_world);
  ASSERT_TRUE(receiver_page_index.has_value());
  ASSERT_LT(*receiver_page_index, introspection->page_table_entries.size());
  const auto receiver_page_entry
    = oxygen::renderer::DecodeVirtualShadowPageTableEntry(
      introspection->page_table_entries[*receiver_page_index]);

  EXPECT_FALSE(introspection->used_request_feedback);
  EXPECT_EQ(introspection->same_frame_detail_page_count, 0U);
  EXPECT_FALSE(receiver_page_entry.current_lod_valid);
  EXPECT_FALSE(receiver_page_entry.any_lod_valid);
}


//! Persistent resolve resources must mirror resident pages deterministically so
//! the later GPU resolve pass has one stable residency input surface.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualResolveStateSnapshotTracksResidentPagesDeterministically)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 228 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 228 });
  ASSERT_NE(first_introspection, nullptr);
  EXPECT_TRUE(first_introspection->has_persistent_gpu_residency_state);
  EXPECT_NE(first_introspection->physical_page_metadata_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_NE(first_introspection->physical_page_lists_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_NE(first_introspection->resolve_stats_srv, kInvalidShaderVisibleIndex);
  const auto allocated_metadata_count = static_cast<std::uint32_t>(std::count_if(
    first_introspection->physical_page_metadata_entries.begin(),
    first_introspection->physical_page_metadata_entries.end(),
    [](const oxygen::renderer::VirtualShadowPhysicalPageMetadata& entry) {
      return oxygen::renderer::HasVirtualShadowPageFlag(
        entry.page_flags, oxygen::renderer::VirtualShadowPageFlag::kAllocated);
    }));
  ASSERT_EQ(allocated_metadata_count, first_introspection->resident_page_count);
  EXPECT_EQ(first_introspection->resolve_stats.resident_entry_count,
    first_introspection->resident_page_count);
  EXPECT_EQ(first_introspection->resolve_stats.pending_raster_page_count,
    first_introspection->pending_raster_page_count);

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 228 }, true);
  const auto* executed_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 228 });
  ASSERT_NE(executed_introspection, nullptr);
  EXPECT_TRUE(executed_introspection->has_persistent_gpu_residency_state);
  EXPECT_NE(executed_introspection->physical_page_metadata_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_NE(executed_introspection->physical_page_lists_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_NE(executed_introspection->resolve_stats_srv, kInvalidShaderVisibleIndex);
}

//! Resolve resources must stay stable per view across republishes so the later
//! resolve pass can own one persistent residency surface.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualResolveStateUsesStablePerViewGpuBuffers)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 229 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16), 0x1111U);
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 229 });
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_TRUE(first_introspection->has_persistent_gpu_residency_state);
  ASSERT_NE(first_introspection->physical_page_metadata_srv,
    kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection->physical_page_lists_srv,
    kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection->resolve_stats_srv, kInvalidShaderVisibleIndex);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  (void)shadow_manager->PublishForView(oxygen::ViewId { 229 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16), 0x2222U);
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 229 });
  ASSERT_NE(second_introspection, nullptr);
  EXPECT_TRUE(second_introspection->has_persistent_gpu_residency_state);
  EXPECT_EQ(second_introspection->physical_page_metadata_srv,
    first_introspection->physical_page_metadata_srv);
  EXPECT_EQ(second_introspection->physical_page_lists_srv,
    first_introspection->physical_page_lists_srv);
  EXPECT_EQ(second_introspection->resolve_stats_srv,
    first_introspection->resolve_stats_srv);
}


//! The live resolved page set must stay authoritative across frames without any
//! auxiliary CPU schedule.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualResolvedPagesStayAuthoritativeAcrossFrames)
{
  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly);
  ASSERT_NE(shadow_manager, nullptr);
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  ViewConstants view_constants;
  view_constants
    .SetProjectionMatrix(
      glm::perspectiveRH_ZO(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 200.0F))
    .SetCameraPosition(glm::vec3(0.0F, -6.0F, 3.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.35F, -0.45F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 3> shadow_casters {
    glm::vec4(-0.75F, 0.0F, 0.5F, 0.5F),
    glm::vec4(0.75F, 0.0F, 0.5F, 0.5F),
    glm::vec4(0.0F, 1.25F, 0.75F, 0.5F),
  };
  const std::array<glm::vec4, 2> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.12F),
    glm::vec4(0.6F, 0.4F, 0.0F, 0.10F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 230 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 230 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_pending_raster_state, nullptr);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 230 }, true);

  const auto derive_active_pages =
    [](const oxygen::renderer::VirtualShadowViewIntrospection& introspection) {
      std::vector<DerivedPendingRasterPage> active_pages {};
      const auto active_page_count
        = introspection.requested_page_list_count
        + introspection.dirty_page_list_count
        + introspection.clean_page_list_count;
      active_pages.reserve(active_page_count);
      const auto physical_page_count
        = std::min<std::size_t>(active_page_count,
          introspection.physical_page_list_entries.size());
      for (std::size_t i = 0U; i < physical_page_count; ++i) {
        const auto& entry = introspection.physical_page_list_entries[i];
        active_pages.push_back(DerivedPendingRasterPage {
          .shadow_instance_index
          = introspection.directional_virtual_metadata.front().shadow_instance_index,
          .payload_index = 0U,
          .clip_level = oxygen::renderer::internal::shadow_detail::
            VirtualResidentPageKeyClipLevel(entry.resident_key),
          .page_index = 0U,
          .resident_key = entry.resident_key,
          .atlas_tile_x = 0U,
          .atlas_tile_y = 0U,
        });
      }
      return active_pages;
    };
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 230 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 230 });
  const auto* second_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 230 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_introspection, nullptr);
  ASSERT_NE(second_pending_raster_state, nullptr);
  ASSERT_FALSE(second_introspection->directional_virtual_metadata.empty());
  const auto layout = BuildVirtualFeedbackLayout(
    second_introspection->directional_virtual_metadata.front());
  const auto coarse_backbone_begin
    = layout.clip_level_count > 3U ? layout.clip_level_count - 3U : 0U;
  const auto first_active_pages = derive_active_pages(*second_introspection);
  EXPECT_TRUE(first_active_pages.empty());

  std::optional<std::uint64_t> requested_fine_resident_key {};
  std::optional<std::uint64_t> omitted_fine_resident_key {};
  std::optional<std::uint64_t> coarse_resident_key {};
  for (const auto& job : first_active_pages) {
    if (job.clip_level < static_cast<std::int32_t>(coarse_backbone_begin)) {
      if (!requested_fine_resident_key.has_value()) {
        requested_fine_resident_key = job.resident_key;
      } else if (job.resident_key != *requested_fine_resident_key) {
        omitted_fine_resident_key = job.resident_key;
      }
    } else if (!coarse_resident_key.has_value()) {
      coarse_resident_key = job.resident_key;
    }
  }

  EXPECT_FALSE(requested_fine_resident_key.has_value());
  EXPECT_FALSE(omitted_fine_resident_key.has_value());
  EXPECT_FALSE(coarse_resident_key.has_value());

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 230 }, true);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 2 });

  const auto third = shadow_manager->PublishForView(oxygen::ViewId { 230 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* third_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 230 });
  const auto* third_pending_raster_state
    = ResolveDerivedPendingRasterState(*shadow_manager, oxygen::ViewId { 230 });

  ASSERT_NE(third.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(third_introspection, nullptr);
  ASSERT_NE(third_pending_raster_state, nullptr);
  ASSERT_FALSE(third_introspection->directional_virtual_metadata.empty());
  const auto second_active_pages = derive_active_pages(*third_introspection);
  EXPECT_TRUE(second_active_pages.empty());

  EXPECT_EQ(third_pending_raster_state->resolved_pages.size(), 0U);
  EXPECT_EQ(third_introspection->pending_raster_page_count, 0U);
}
} // namespace
