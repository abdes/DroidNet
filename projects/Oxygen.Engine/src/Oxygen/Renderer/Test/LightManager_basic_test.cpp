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
using oxygen::renderer::VirtualShadowAtlasTileDebugState;
using oxygen::renderer::internal::RendererTagFactory;
using oxygen::renderer::testing::FakeGraphics;
using oxygen::scene::Scene;
using oxygen::scene::SceneFlag;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

auto CountAtlasTileDebugState(const std::span<const std::uint32_t> tile_states,
  const VirtualShadowAtlasTileDebugState expected_state) -> std::uint32_t
{
  return static_cast<std::uint32_t>(std::count(tile_states.begin(),
    tile_states.end(), static_cast<std::uint32_t>(expected_state)));
}

auto ResolveVirtualViewIntrospection(
  ShadowManager& shadow_manager, const oxygen::ViewId view_id)
  -> const oxygen::renderer::VirtualShadowViewIntrospection*
{
  shadow_manager.ResolveVirtualCurrentFrame(view_id);
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

struct DerivedResolvedVirtualPage {
  std::uint32_t shadow_instance_index { 0xFFFFFFFFU };
  std::uint32_t payload_index { 0xFFFFFFFFU };
  std::uint32_t clip_level { 0U };
  std::uint32_t page_index { 0U };
  std::uint64_t resident_key { 0U };
  std::uint16_t atlas_tile_x { 0U };
  std::uint16_t atlas_tile_y { 0U };
};

struct DerivedVirtualRenderPlan {
  const oxygen::graphics::Texture* depth_texture { nullptr };
  std::vector<DerivedResolvedVirtualPage> resolved_pages {};
  std::uint32_t page_size_texels { 0U };
  std::uint32_t atlas_tiles_per_axis { 0U };
};

// Legacy test-only derivation of pending raster pages from published
// page-table/page-flag state. Runtime no longer mirrors this CPU page list.
auto DeriveResolvedRasterPages(
  const oxygen::renderer::VirtualShadowViewIntrospection& introspection)
  -> std::vector<DerivedResolvedVirtualPage>
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

  std::vector<DerivedResolvedVirtualPage> resolved_pages;
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

    resolved_pages.push_back(DerivedResolvedVirtualPage {
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
  const std::span<const DerivedResolvedVirtualPage> pages,
  const std::uint64_t resident_key) -> bool
{
  return std::ranges::any_of(
    pages, [resident_key](const auto& page) { return page.resident_key == resident_key; });
}

auto ResolveVirtualRenderPlan(
  ShadowManager& shadow_manager, const oxygen::ViewId view_id)
  -> const DerivedVirtualRenderPlan*
{
  shadow_manager.ResolveVirtualCurrentFrame(view_id);
  const auto* introspection = shadow_manager.TryGetVirtualViewIntrospection(view_id);
  const auto& depth_texture = shadow_manager.GetVirtualShadowDepthTexture();
  if (!depth_texture && introspection == nullptr) {
    return nullptr;
  }

  static thread_local std::deque<DerivedVirtualRenderPlan> derived_plans {};
  derived_plans.emplace_back();
  auto& derived = derived_plans.back();
  derived.depth_texture = depth_texture.get();
  if (introspection != nullptr) {
    derived.resolved_pages = DeriveResolvedRasterPages(*introspection);
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
  ShadowManagerPublishForView_VirtualPlanPrefersSyntheticSunOverSceneSunLight)
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
  const auto* virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 81 });
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 81 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(published.virtual_directional_shadow_metadata_srv,
    kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.virtual_shadow_physical_pool_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_plan, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  EXPECT_EQ(published.sun_shadow_index, 0U);
  EXPECT_FALSE(virtual_plan->resolved_pages.empty());
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());
  EXPECT_NE(virtual_introspection->directional_virtual_metadata.front().flags
      & static_cast<std::uint32_t>(
        oxygen::engine::ShadowProductFlags::kSunLight),
    0U);
}

//! Virtual shadow planning is driven by visible receiver demand instead of a
//! centered resident window.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanUsesVisibleReceiverBounds)
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
  const auto* virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 10 });
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 10 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(published.virtual_directional_shadow_metadata_srv,
    kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    published.virtual_shadow_physical_pool_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_plan, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  EXPECT_FALSE(virtual_plan->resolved_pages.empty());
  EXPECT_EQ(virtual_introspection->pending_raster_page_count,
    virtual_plan->resolved_pages.size());
}

//! Explicit per-view request feedback becomes eligible on the next compatible
//! publication and can then map the requested pages.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanUsesSubmittedRequestFeedbackAfterSafeFrameDelay)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 same-frame GPU marking contract. "
       "Readback-driven feedback is no longer the authoritative live demand "
       "source for page publication.";

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
    glm::vec4(-1.5F, 0.0F, 0.5F, 0.5F),
    glm::vec4(1.5F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 20 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 20 });
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 20 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());
  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto first_layout = BuildVirtualFeedbackLayout(first_metadata);
  ASSERT_GT(first_layout.clip_level_count, 1U);
  ASSERT_GT(first_layout.pages_per_level, 1U);
  const auto requested_page = GlobalPageIndexForWorldPoint(
    first_metadata, 0U, glm::vec3(shadow_casters.front()));
  ASSERT_TRUE(requested_page.has_value());
  const auto requested_resident_keys = RequestedResidentKeys(
    first_layout, std::span<const std::uint32_t> { &*requested_page, 1U });
  ASSERT_EQ(requested_resident_keys.size(), 1U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 20 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 20 },
    MakeVirtualRequestFeedback(
      first_layout, SequenceNumber { 1 }, { *requested_page }));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto published = shadow_manager->PublishForView(oxygen::ViewId { 20 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 20 });
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 20 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_plan, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());
  const auto second_layout = BuildVirtualFeedbackLayout(
    virtual_introspection->directional_virtual_metadata.front());
  const auto translated_page_index = LocalPageIndexForResidentKey(
    second_layout, requested_resident_keys.front());
  ASSERT_TRUE(translated_page_index.has_value());
  EXPECT_TRUE(virtual_introspection->used_request_feedback);
  EXPECT_EQ(virtual_introspection->feedback_requested_page_count, 1U);
  EXPECT_EQ(virtual_introspection->feedback_refinement_page_count, 0U);
  EXPECT_EQ(virtual_introspection->receiver_bootstrap_page_count, 0U);
  ASSERT_LT(
    *translated_page_index, virtual_introspection->page_table_entries.size());
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasAnyLod(
    virtual_introspection->page_table_entries[*translated_page_index]));
}

//! Ultra-tier directional VSM should publish a dense virtual address space
//! while keeping physical page texel size above the minimum useful floor.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanUsesDenseGridForUltraTier)
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
  const auto* virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 30 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_plan, nullptr);
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 30 });
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());
  EXPECT_GE(
    virtual_introspection->directional_virtual_metadata.front().pages_per_axis,
    64U);
  EXPECT_GE(virtual_plan->page_size_texels, 128U);
}

//! Ultra-tier directional VSM should use a denser virtual address space than
//! the physical pool so quality can improve without sizing the atlas for full
//! residency.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanUsesDenserAddressSpaceThanPhysicalPool)
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
  const auto* virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 31 });
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 31 });

  ASSERT_NE(published.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_plan, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());

  const auto& metadata
    = virtual_introspection->directional_virtual_metadata.front();
  const auto virtual_page_count = metadata.clip_level_count
    * metadata.pages_per_axis * metadata.pages_per_axis;
  const auto physical_tile_capacity
    = virtual_plan->atlas_tiles_per_axis * virtual_plan->atlas_tiles_per_axis;

  EXPECT_EQ(metadata.clip_level_count, 12U);
  EXPECT_GE(metadata.pages_per_axis, 64U);
  EXPECT_LT(physical_tile_capacity, virtual_page_count);
}

//! Explicit virtual request feedback remains the active request source while it
//! stays compatible and fresh enough for the current frame window.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualRequestFeedbackPersistsWithinFreshnessWindow)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 same-frame GPU marking contract. "
       "Request feedback remains telemetry-only and is no longer the live "
       "authority for page publication freshness.";

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
  const std::array<glm::vec4, 1> first_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };
  const std::array<glm::vec4, 1> second_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 21 },
    view_constants, manager, shadow_casters, first_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 21 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());
  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto first_layout = BuildVirtualFeedbackLayout(first_metadata);
  ASSERT_GT(first_layout.pages_per_level, 1U);
  const auto requested_page = GlobalPageIndexForWorldPoint(
    first_metadata, 0U, glm::vec3(shadow_casters.front()));
  ASSERT_TRUE(requested_page.has_value());
  const auto requested_resident_keys = RequestedResidentKeys(
    first_layout, std::span<const std::uint32_t> { &*requested_page, 1U });
  ASSERT_EQ(requested_resident_keys.size(), 1U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 21 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 21 },
    MakeVirtualRequestFeedback(
      first_layout, SequenceNumber { 1 }, { *requested_page }));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 21 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 21 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 21 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());
  const auto second_layout = BuildVirtualFeedbackLayout(
    second_virtual_introspection->directional_virtual_metadata.front());
  const auto second_translated_page_index = LocalPageIndexForResidentKey(
    second_layout, requested_resident_keys.front());
  ASSERT_TRUE(second_translated_page_index.has_value());
  ASSERT_LT(*second_translated_page_index,
    second_virtual_introspection->page_table_entries.size());
  EXPECT_TRUE(second_virtual_introspection->used_request_feedback);
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasAnyLod(
    second_virtual_introspection
      ->page_table_entries[*second_translated_page_index]));
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 21 });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 0 });

  const auto third = shadow_manager->PublishForView(oxygen::ViewId { 21 },
    view_constants, manager, shadow_casters, second_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* third_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 21 });
  const auto* third_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 21 });

  ASSERT_NE(third.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(third_virtual_plan, nullptr);
  ASSERT_NE(third_virtual_introspection, nullptr);
  ASSERT_FALSE(
    third_virtual_introspection->directional_virtual_metadata.empty());
  const auto third_layout = BuildVirtualFeedbackLayout(
    third_virtual_introspection->directional_virtual_metadata.front());
  const auto third_translated_page_index = LocalPageIndexForResidentKey(
    third_layout, requested_resident_keys.front());
  ASSERT_TRUE(third_translated_page_index.has_value());
  EXPECT_GE(third_virtual_introspection->mapped_page_count,
    third_virtual_introspection->pending_page_count);
  EXPECT_TRUE(third_virtual_introspection->used_request_feedback);
  ASSERT_LT(*third_translated_page_index,
    third_virtual_introspection->page_table_entries.size());
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasAnyLod(
    third_virtual_introspection
      ->page_table_entries[*third_translated_page_index]));
}

//! Feedback-driven detail selection is exact and sparse: the requested fine
//! page should map, but adjacent guard-band dilation should not expand clip-0
//! coverage anymore.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualRequestFeedbackAppliesGuardBand)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 same-frame GPU marking contract. "
       "Feedback guard-band shaping is telemetry only until the later "
       "round-trip removal phase.";

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
      glm::perspectiveRH_ZO(glm::radians(90.0F), 16.0F / 9.0F, 0.1F, 10000.0F))
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

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 27 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 27 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());
  const auto& bootstrap_metadata
    = bootstrap_introspection->directional_virtual_metadata.front();
  const auto layout = BuildVirtualFeedbackLayout(bootstrap_metadata);
  ASSERT_GT(layout.pages_per_level, 1U);
  const auto requested_page = GlobalPageIndexForWorldPoint(
    bootstrap_metadata, 0U, glm::vec3(shadow_casters.front()));
  ASSERT_TRUE(requested_page.has_value());
  const auto requested_resident_keys = RequestedResidentKeys(
    layout, std::span<const std::uint32_t> { &*requested_page, 1U });
  ASSERT_EQ(requested_resident_keys.size(), 1U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 27 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 27 },
    MakeVirtualRequestFeedback(
      layout, SequenceNumber { 1 }, { *requested_page }));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto publication = shadow_manager->PublishForView(oxygen::ViewId { 27 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 27 });
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 27 });

  ASSERT_NE(
    publication.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_plan, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());
  const auto publication_layout = BuildVirtualFeedbackLayout(
    virtual_introspection->directional_virtual_metadata.front());
  const auto translated_page_index = LocalPageIndexForResidentKey(
    publication_layout, requested_resident_keys.front());
  ASSERT_TRUE(translated_page_index.has_value());
  const auto mapped_clip0_pages = CountCurrentLodMappedPagesInClip(
    publication_layout, virtual_introspection->page_table_entries, 0U);

  EXPECT_TRUE(virtual_introspection->used_request_feedback);
  EXPECT_EQ(virtual_introspection->feedback_requested_page_count, 1U);
  EXPECT_EQ(virtual_introspection->feedback_refinement_page_count, 0U);
  EXPECT_GT(virtual_introspection->mapped_page_count, 1U);
  ASSERT_LT(
    *translated_page_index, virtual_introspection->page_table_entries.size());
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
    virtual_introspection->page_table_entries[*translated_page_index]));
  EXPECT_LT(mapped_clip0_pages, 8U);
}

//! Feedback guard dilation should preserve continuity across adjacent fine
//! pages. When the next frame requests a neighboring page, the previous page
//! should remain mapped without requiring a second explicit feedback hit.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualRequestFeedbackGuardBandKeepsAdjacentPagesMapped)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 same-frame GPU marking contract. "
       "Adjacent-page continuity now comes from current publication plus "
       "fallback aliases, not delayed feedback guard-band acceptance.";

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
    .SetCameraPosition(glm::vec3(0.0F, -8.0F, 5.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.4F, -0.3F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 1.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.1F),
  };

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 28 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 28 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());
  const auto layout = BuildVirtualFeedbackLayout(
    bootstrap_introspection->directional_virtual_metadata.front());
  ASSERT_GT(layout.pages_per_level, 2U);
  const auto adjacent_pages = FindAdjacentMappedPagesInClip(
    layout, bootstrap_introspection->page_table_entries, 0U);
  ASSERT_TRUE(adjacent_pages.has_value());
  const auto first_requested_page = adjacent_pages->first;
  const auto second_requested_page = adjacent_pages->second;
  const auto first_requested_resident_keys = RequestedResidentKeys(
    layout, std::span<const std::uint32_t> { &first_requested_page, 1U });
  const auto second_requested_resident_keys = RequestedResidentKeys(
    layout, std::span<const std::uint32_t> { &second_requested_page, 1U });
  ASSERT_EQ(first_requested_resident_keys.size(), 1U);
  ASSERT_EQ(second_requested_resident_keys.size(), 1U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 28 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 28 },
    MakeVirtualRequestFeedback(
      layout, SequenceNumber { 1 }, { first_requested_page }));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 28 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 28 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());
  const auto first_layout = BuildVirtualFeedbackLayout(
    first_virtual_introspection->directional_virtual_metadata.front());
  const auto translated_first_requested_page = LocalPageIndexForResidentKey(
    first_layout, first_requested_resident_keys.front());
  const auto translated_second_requested_page = LocalPageIndexForResidentKey(
    first_layout, second_requested_resident_keys.front());
  ASSERT_TRUE(translated_first_requested_page.has_value());
  ASSERT_TRUE(translated_second_requested_page.has_value());
  ASSERT_LT(*translated_first_requested_page,
    first_virtual_introspection->page_table_entries.size());
  ASSERT_LT(*translated_second_requested_page,
    first_virtual_introspection->page_table_entries.size());
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasAnyLod(
    first_virtual_introspection
      ->page_table_entries[*translated_first_requested_page]));
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasAnyLod(
    first_virtual_introspection
      ->page_table_entries[*translated_second_requested_page]));
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 28 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 28 },
    MakeVirtualRequestFeedback(first_layout, SequenceNumber { 2 },
      { *translated_second_requested_page }));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 0 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 28 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 28 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());
  const auto second_layout = BuildVirtualFeedbackLayout(
    second_virtual_introspection->directional_virtual_metadata.front());
  const auto final_first_requested_page = LocalPageIndexForResidentKey(
    second_layout, first_requested_resident_keys.front());
  const auto final_second_requested_page = LocalPageIndexForResidentKey(
    second_layout, second_requested_resident_keys.front());
  ASSERT_TRUE(final_first_requested_page.has_value());
  ASSERT_TRUE(final_second_requested_page.has_value());
  ASSERT_LT(*final_first_requested_page,
    second_virtual_introspection->page_table_entries.size());
  ASSERT_LT(*final_second_requested_page,
    second_virtual_introspection->page_table_entries.size());
  EXPECT_TRUE(second_virtual_introspection->used_request_feedback);
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasAnyLod(
    second_virtual_introspection
      ->page_table_entries[*final_first_requested_page]));
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasAnyLod(
    second_virtual_introspection
      ->page_table_entries[*final_second_requested_page]));
}

//! Coarse GPU-marked feedback must coexist with detail feedback instead of
//! overwriting it, so the next publication can preserve both coarse coverage
//! and fine refinement from the same source frame.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualCoarseFeedbackDoesNotOverwriteDetailFeedback)
{
  GTEST_SKIP()
    << "Superseded by the explicit coarse-band/page-table fallback contract in "
       "Phase 5; coarse seeding and feedback consumption are covered by the "
       "dedicated coarse-backbone and feedback-consumption tests.";

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
    glm::vec4(-1.5F, 0.0F, 0.5F, 0.5F),
    glm::vec4(1.5F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 242 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 242 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());

  const auto first_layout = BuildVirtualFeedbackLayout(
    first_virtual_introspection->directional_virtual_metadata.front());
  ASSERT_GT(first_layout.clip_level_count, 3U);
  const auto coarse_backbone_begin = oxygen::renderer::internal::shadow_detail::
    ResolveDirectionalCoarseBackboneBegin(first_layout.clip_level_count);
  const auto detail_page = FindMappedPageInClip(
    first_layout, first_virtual_introspection->page_table_entries, 0U, 1U);
  const auto coarse_page = FindMappedPageInClip(first_layout,
    first_virtual_introspection->page_table_entries, coarse_backbone_begin);
  ASSERT_TRUE(detail_page.has_value());
  ASSERT_TRUE(coarse_page.has_value());

  const auto detail_resident_keys = RequestedResidentKeys(
    first_layout, std::span<const std::uint32_t> { &*detail_page, 1U });
  const auto coarse_resident_keys = RequestedResidentKeys(
    first_layout, std::span<const std::uint32_t> { &*coarse_page, 1U });
  ASSERT_EQ(detail_resident_keys.size(), 1U);
  ASSERT_EQ(coarse_resident_keys.size(), 1U);

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 242 });
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 242 },
    MakeVirtualRequestFeedback(first_layout, SequenceNumber { 1 },
      { *detail_page }, oxygen::renderer::VirtualShadowFeedbackKind::kDetail));
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 242 },
    MakeVirtualRequestFeedback(first_layout, SequenceNumber { 1 },
      { *coarse_page }, oxygen::renderer::VirtualShadowFeedbackKind::kCoarse));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 242 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 242 });
  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());

  const auto second_layout = BuildVirtualFeedbackLayout(
    second_virtual_introspection->directional_virtual_metadata.front());
  const auto translated_detail_page
    = LocalPageIndexForResidentKey(second_layout, detail_resident_keys.front());
  const auto translated_coarse_page
    = LocalPageIndexForResidentKey(second_layout, coarse_resident_keys.front());
  ASSERT_TRUE(translated_detail_page.has_value());
  ASSERT_TRUE(translated_coarse_page.has_value());
  ASSERT_LT(*translated_detail_page,
    second_virtual_introspection->page_table_entries.size());
  ASSERT_LT(*translated_coarse_page,
    second_virtual_introspection->page_table_entries.size());

  EXPECT_TRUE(second_virtual_introspection->used_request_feedback);
  EXPECT_EQ(second_virtual_introspection->receiver_bootstrap_page_count, 0U);
  EXPECT_GT(second_virtual_introspection->coarse_backbone_page_count, 0U);
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasAnyLod(
    second_virtual_introspection->page_table_entries[*translated_detail_page]));
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
    second_virtual_introspection->page_table_entries[*translated_coarse_page]));
}

//! Coarse GPU feedback alone must be able to seed the coarse backbone without
//! falling back to receiver bootstrap, so the coarse path remains sampleable
//! even before fine-detail feedback converges.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualCoarseFeedbackSeedsCoarseBackbone)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 same-frame GPU coarse-mark path. "
       "The live coarse backbone no longer depends on delayed coarse "
       "feedback seeding.";

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

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 243 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 243 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());

  const auto first_layout = BuildVirtualFeedbackLayout(
    first_virtual_introspection->directional_virtual_metadata.front());
  ASSERT_GT(first_layout.clip_level_count, 3U);
  const auto coarse_backbone_begin = first_layout.clip_level_count - 3U;
  const auto coarse_page = FindCurrentLodMappedPageInClip(first_layout,
    first_virtual_introspection->page_table_entries, coarse_backbone_begin);
  ASSERT_TRUE(coarse_page.has_value());
  const auto coarse_resident_keys = RequestedResidentKeys(
    first_layout, std::span<const std::uint32_t> { &*coarse_page, 1U });
  ASSERT_EQ(coarse_resident_keys.size(), 1U);

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 243 });
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 243 },
    MakeVirtualRequestFeedback(first_layout, SequenceNumber { 1 },
      { *coarse_page }, oxygen::renderer::VirtualShadowFeedbackKind::kCoarse));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 243 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 243 });
  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());

  const auto second_layout = BuildVirtualFeedbackLayout(
    second_virtual_introspection->directional_virtual_metadata.front());
  const auto translated_coarse_page
    = LocalPageIndexForResidentKey(second_layout, coarse_resident_keys.front());
  ASSERT_TRUE(translated_coarse_page.has_value());
  ASSERT_LT(*translated_coarse_page,
    second_virtual_introspection->page_table_entries.size());

  EXPECT_TRUE(second_virtual_introspection->used_request_feedback);
  EXPECT_EQ(second_virtual_introspection->feedback_requested_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->feedback_refinement_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->receiver_bootstrap_page_count, 0U);
  EXPECT_GT(second_virtual_introspection->coarse_backbone_page_count, 0U);
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
    second_virtual_introspection->page_table_entries[*translated_coarse_page]));
}

//! Grace-window hysteresis must not pollute close-range feedback with a large
//! previous far-view request set. Small current request sets must still map
//! their newly requested fine pages.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualRequestFeedbackSmallSetOverridesOldFarSet)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 same-frame GPU marking contract. "
       "Old feedback-set override behavior is no longer the live authority.";

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
    .SetCameraPosition(glm::vec3(0.0F, -8.0F, 5.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.4F, -0.3F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 6.5F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 6.5F),
  };

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 29 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 29 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());
  const auto layout = BuildVirtualFeedbackLayout(
    bootstrap_introspection->directional_virtual_metadata.front());
  ASSERT_GT(layout.pages_per_level, 2U);
  const auto mapped_requested_pages = CollectMappedPagesInClip(
    layout, bootstrap_introspection->page_table_entries, 0U);
  ASSERT_GT(mapped_requested_pages.size(), 1U);

  std::vector<std::uint32_t> far_requested_pages(mapped_requested_pages.begin(),
    mapped_requested_pages.begin()
      + std::min<std::size_t>(mapped_requested_pages.size(), 64U));
  const auto target_page
    = mapped_requested_pages.size() > far_requested_pages.size()
    ? mapped_requested_pages[far_requested_pages.size()]
    : far_requested_pages.back();
  const auto target_resident_keys = RequestedResidentKeys(
    layout, std::span<const std::uint32_t> { &target_page, 1U });
  ASSERT_EQ(target_resident_keys.size(), 1U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 29 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 29 },
    MakeVirtualRequestFeedback(layout, SequenceNumber { 1 },
      std::span<const std::uint32_t>(
        far_requested_pages.data(), far_requested_pages.size())));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 29 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 29 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 29 },
    MakeVirtualRequestFeedback(layout, SequenceNumber { 2 }, { target_page }));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 0 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 29 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 29 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());
  const auto second_layout = BuildVirtualFeedbackLayout(
    second_virtual_introspection->directional_virtual_metadata.front());
  const auto translated_target_page
    = LocalPageIndexForResidentKey(second_layout, target_resident_keys.front());
  ASSERT_TRUE(translated_target_page.has_value());
  ASSERT_LT(*translated_target_page,
    second_virtual_introspection->page_table_entries.size());
  EXPECT_TRUE(second_virtual_introspection->used_request_feedback);
  EXPECT_EQ(second_virtual_introspection->feedback_requested_page_count, 1U);
  EXPECT_NE(
    second_virtual_introspection->page_table_entries[*translated_target_page],
    0U);
}

//! Sparse feedback in one fine clip must stay sparse. Two far-apart requested
//! pages must not inflate into one large mapped rectangle that fills the gap
//! between them.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualRequestFeedbackKeepsSparsePagesSparse)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 same-frame GPU marking contract. "
       "Sparse current-page publication is no longer driven by delayed "
       "request feedback acceptance.";

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
    .SetCameraPosition(glm::vec3(0.0F, -8.0F, 5.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.4F, -0.3F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> bootstrap_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 30 },
    view_constants, manager, bootstrap_shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 30 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());
  const auto& bootstrap_metadata
    = bootstrap_introspection->directional_virtual_metadata.front();
  const auto layout = BuildVirtualFeedbackLayout(bootstrap_metadata);
  ASSERT_GT(layout.pages_per_axis, 10U);
  const float page_world
    = bootstrap_metadata.clip_metadata[0].origin_page_scale.z;
  const auto world_a
    = DirectionalVirtualPageWorldCenter(bootstrap_metadata, 0U, 2U, 2U);
  const auto world_b
    = DirectionalVirtualPageWorldCenter(bootstrap_metadata, 0U, 9U, 9U);
  const auto gap_world
    = DirectionalVirtualPageWorldCenter(bootstrap_metadata, 0U, 5U, 5U);
  const std::array<glm::vec4, 2> sparse_shadow_casters {
    glm::vec4(world_a, page_world * 0.1F),
    glm::vec4(world_b, page_world * 0.1F),
  };
  const std::array<glm::vec4, 2> visible_receivers {
    glm::vec4(world_a, page_world * 0.1F),
    glm::vec4(world_b, page_world * 0.1F),
  };
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 30 });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 30 },
    view_constants, manager, sparse_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 30 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());
  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto first_layout = BuildVirtualFeedbackLayout(first_metadata);
  const auto sparse_page_a
    = GlobalPageIndexForWorldPoint(first_metadata, 0U, world_a);
  const auto sparse_page_b
    = GlobalPageIndexForWorldPoint(first_metadata, 0U, world_b);
  ASSERT_TRUE(sparse_page_a.has_value());
  ASSERT_TRUE(sparse_page_b.has_value());
  const auto sparse_page_a_keys = RequestedResidentKeys(
    first_layout, std::span<const std::uint32_t> { &*sparse_page_a, 1U });
  const auto sparse_page_b_keys = RequestedResidentKeys(
    first_layout, std::span<const std::uint32_t> { &*sparse_page_b, 1U });
  ASSERT_EQ(sparse_page_a_keys.size(), 1U);
  ASSERT_EQ(sparse_page_b_keys.size(), 1U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 30 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 30 },
    MakeVirtualRequestFeedback(
      first_layout, SequenceNumber { 2 }, { *sparse_page_a, *sparse_page_b }));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 0 });

  const auto publication = shadow_manager->PublishForView(oxygen::ViewId { 30 },
    view_constants, manager, sparse_shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 30 });

  ASSERT_NE(
    publication.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_introspection, nullptr);
  EXPECT_TRUE(virtual_introspection->used_request_feedback);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());
  const auto& second_metadata
    = virtual_introspection->directional_virtual_metadata.front();
  const auto second_layout = BuildVirtualFeedbackLayout(second_metadata);
  const auto translated_page_a
    = LocalPageIndexForResidentKey(second_layout, sparse_page_a_keys.front());
  const auto translated_page_b
    = LocalPageIndexForResidentKey(second_layout, sparse_page_b_keys.front());
  const auto gap_page
    = GlobalPageIndexForWorldPoint(second_metadata, 0U, gap_world);
  ASSERT_TRUE(translated_page_a.has_value());
  ASSERT_TRUE(translated_page_b.has_value());
  ASSERT_TRUE(gap_page.has_value());
  ASSERT_LT(
    *translated_page_a, virtual_introspection->page_table_entries.size());
  ASSERT_LT(
    *translated_page_b, virtual_introspection->page_table_entries.size());
  ASSERT_LT(*gap_page, virtual_introspection->page_table_entries.size());
  const auto mapped_clip0_pages = CountCurrentLodMappedPagesInClip(
    second_layout, virtual_introspection->page_table_entries, 0U);

  EXPECT_NE(virtual_introspection->page_table_entries[*translated_page_a], 0U);
  EXPECT_NE(virtual_introspection->page_table_entries[*translated_page_b], 0U);
  EXPECT_FALSE(oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
    virtual_introspection->page_table_entries[*gap_page]));
  EXPECT_LT(mapped_clip0_pages, 50U);
}

//! Virtual shadow publication keeps resident pages and skips rerasterization
//! when the snapped virtual plan is unchanged.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanReusesResidentPagesForIdenticalInputs)
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
  const auto* first_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 11 });
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 11 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(first_virtual_plan->resolved_pages.empty());
  ASSERT_EQ(first_virtual_introspection->pending_raster_page_count,
    first_virtual_plan->resolved_pages.size());
  ASSERT_FALSE(first_virtual_introspection->atlas_tile_debug_states.empty());
  EXPECT_EQ(first_virtual_introspection->resident_page_count,
    first_virtual_plan->resolved_pages.size());
  EXPECT_EQ(first_virtual_introspection->mapped_page_count,
    first_virtual_plan->resolved_pages.size());
  EXPECT_EQ(first_virtual_introspection->pending_page_count,
    first_virtual_plan->resolved_pages.size());
  EXPECT_EQ(first_virtual_introspection->clean_page_count, 0U);
  EXPECT_EQ(CountAtlasTileDebugState(
              first_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kRewritten),
    first_virtual_plan->resolved_pages.size());
  EXPECT_EQ(CountAtlasTileDebugState(
              first_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kReused),
    0U);
  EXPECT_EQ(CountAtlasTileDebugState(
              first_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kCached),
    0U);

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 11 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 11 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 11 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 11 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_virtual_plan->resolved_pages.size(), 0U);
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
  ASSERT_FALSE(second_virtual_introspection->atlas_tile_debug_states.empty());
  EXPECT_EQ(CountAtlasTileDebugState(
              second_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kRewritten),
    0U);
  EXPECT_EQ(CountAtlasTileDebugState(
              second_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kReused),
    second_virtual_introspection->resident_page_count);
  EXPECT_EQ(CountAtlasTileDebugState(
              second_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kCached),
    0U);
  EXPECT_EQ(CountAtlasTileDebugState(
              second_virtual_introspection->atlas_tile_debug_states,
              VirtualShadowAtlasTileDebugState::kCleared),
    static_cast<std::uint32_t>(
      second_virtual_introspection->atlas_tile_debug_states.size())
      - second_virtual_introspection->resident_page_count);
}

//! Resident pages carried across frames but not requested by the current frame
//! must not be reported as reused in the atlas inspector.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualAtlasDebugSeparatesCachedFromReused)
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
  const std::array<glm::vec4, 2> wide_receivers {
    glm::vec4(-1.5F, 0.0F, 0.0F, 0.08F),
    glm::vec4(1.5F, 0.0F, 0.0F, 0.08F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 17 }, view_constants,
    manager, shadow_casters, wide_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 17 });
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 17 });

  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 2 }, Slot { 1 });
  shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 2 }, Slot { 1 });
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 2 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 1 }, ViewConstants::kRenderer);

  const std::array<glm::vec4, 1> narrow_receivers {
    glm::vec4(-1.5F, 0.0F, 0.0F, 0.08F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 17 }, view_constants,
    manager, shadow_casters, narrow_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 17 });

  ASSERT_NE(second_virtual_introspection, nullptr);
  const auto cached_count = CountAtlasTileDebugState(
    second_virtual_introspection->atlas_tile_debug_states,
    VirtualShadowAtlasTileDebugState::kCached);
  const auto reused_count = CountAtlasTileDebugState(
    second_virtual_introspection->atlas_tile_debug_states,
    VirtualShadowAtlasTileDebugState::kReused);
  EXPECT_GT(second_virtual_introspection->resident_page_count, 0U);
  EXPECT_GT(reused_count, 0U)
    << "resident=" << second_virtual_introspection->resident_page_count
    << " mapped=" << second_virtual_introspection->mapped_page_count
    << " reused=" << reused_count
    << " selected=" << second_virtual_introspection->selected_page_count
    << " receiver_bootstrap="
    << second_virtual_introspection->receiver_bootstrap_page_count
    << " coarse=" << second_virtual_introspection->coarse_backbone_page_count;
  EXPECT_EQ(cached_count + reused_count,
    second_virtual_introspection->resident_page_count)
    << "resident=" << second_virtual_introspection->resident_page_count
    << " cached=" << cached_count << " reused=" << reused_count;
}

//! Reordering the shadow-caster bounds set must not invalidate the directional
//! VSM cache when the actual casters are unchanged.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanReusesResidentPagesForReorderedCasterBounds)
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
  ASSERT_GT(first_virtual_introspection->resident_page_count, 0U);

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 110 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 110 },
    view_constants, manager, shadow_casters_b, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 110 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 110 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_virtual_plan->resolved_pages.size(), 0U);
  EXPECT_EQ(second_virtual_introspection->pending_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->clean_page_count,
    first_virtual_introspection->resident_page_count);
  EXPECT_EQ(second_virtual_introspection->resident_page_count,
    first_virtual_introspection->resident_page_count);
  EXPECT_EQ(second_virtual_introspection->mapped_page_count,
    first_virtual_introspection->mapped_page_count);
  EXPECT_TRUE(
    std::ranges::equal(second_virtual_introspection->page_table_entries,
      first_virtual_introspection->page_table_entries));
}

//! Clean resident virtual pages that are no longer requested should remain
//! cached instead of being dropped immediately, so later movement can reuse a
//! larger resident working set.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanRetainsCleanPagesAcrossReceiverShift)
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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 18 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 18 },
    view_constants, manager, shadow_casters, shifted_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 18 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 18 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_GT(second_virtual_introspection->mapped_page_count, 0U);
  EXPECT_GE(second_virtual_introspection->resident_page_count,
    second_virtual_introspection->mapped_page_count);
  EXPECT_GE(second_virtual_introspection->resident_page_count,
    first_virtual_introspection->resident_page_count);
  EXPECT_GT(second_virtual_introspection->clean_page_count, 0U);
}

//! Page-aligned directional clipmap motion should reuse overlapping resident
//! pages instead of rerasterizing the whole requested working set, even if the
//! backend chooses a slightly different light-space placement while preserving
//! the overlapping page contents.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanReusesResidentPagesAcrossClipmapShift)
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
  const auto* first_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 30 });
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 30 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(first_virtual_plan->resolved_pages.empty());
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

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 30 });

  view_constants.SetCameraPosition(
    view_constants.GetCameraPosition() + world_shift_x);
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 30 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 30 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 30 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());

  const auto& second_metadata
    = second_virtual_introspection->directional_virtual_metadata.front();
  EXPECT_NE(first_metadata.clip_metadata[0].origin_page_scale.x,
    second_metadata.clip_metadata[0].origin_page_scale.x);
  EXPECT_LT(second_virtual_introspection->pending_raster_page_count,
    second_virtual_introspection->mapped_page_count);
  EXPECT_LT(second_virtual_plan->resolved_pages.size(),
    first_virtual_introspection->resident_page_count);
  EXPECT_GT(second_virtual_introspection->clean_page_count, 0U);
}

//! Compatible page-aligned clipmap motion must preserve absolute page identity.
//! The same world-space page should move to a new local page index while
//! keeping the same physical atlas tile and skipping reraster.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualClipmapShiftPreservesAbsolutePageTileReuse)
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
  const auto* first_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 31 });
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 31 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());

  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto first_layout = BuildVirtualFeedbackLayout(first_metadata);
  const auto first_tracked_page = FindMappedPageInClip(
    first_layout, first_virtual_introspection->page_table_entries, 0U, 2U);
  ASSERT_TRUE(first_tracked_page.has_value());

  const auto first_page_coords
    = DecodeVirtualPageIndex(first_layout, *first_tracked_page);
  ASSERT_TRUE(first_page_coords.has_value());
  const auto tracked_world_center = DirectionalVirtualPageWorldCenter(
    first_metadata, first_page_coords->clip_index, first_page_coords->page_x,
    first_page_coords->page_y);
  const auto first_entry = oxygen::renderer::DecodeVirtualShadowPageTableEntry(
    first_virtual_introspection->page_table_entries[*first_tracked_page]);
  ASSERT_TRUE(first_entry.any_lod_valid);

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

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 31 });

  view_constants.SetCameraPosition(
    view_constants.GetCameraPosition() + world_shift_x);
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 31 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 31 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 31 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());

  const auto& second_metadata
    = second_virtual_introspection->directional_virtual_metadata.front();
  const auto second_page_index
    = GlobalPageIndexForWorldPoint(second_metadata, 0U, tracked_world_center);
  ASSERT_TRUE(second_page_index.has_value());
  ASSERT_NE(*second_page_index, *first_tracked_page);

  const auto second_entry = oxygen::renderer::DecodeVirtualShadowPageTableEntry(
    second_virtual_introspection->page_table_entries[*second_page_index]);
  EXPECT_TRUE(second_entry.any_lod_valid);
  EXPECT_EQ(second_entry.tile_x, first_entry.tile_x);
  EXPECT_EQ(second_entry.tile_y, first_entry.tile_y);

  const auto rerastered_tracked_page
    = std::ranges::any_of(second_virtual_plan->resolved_pages,
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

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 230 });

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

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 230 });

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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 231 });

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

//! Feedback pages that move outside the current fine-clip window after a
//! clipmap shift must not be remapped into unrelated local pages.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualFeedbackDropsPagesOutsideCurrentClipAfterClipmapShift)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 page-management publication contract. "
       "Clip-shift visibility now comes from live current/fallback "
       "publication rather than delayed feedback pruning.";

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
    view_constants, oxygen::ViewId { 31 }, shadow_casters, synthetic_sun);
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 31 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());

  const auto& bootstrap_metadata
    = bootstrap_introspection->directional_virtual_metadata.front();
  const auto inverse_light_view = glm::inverse(bootstrap_metadata.light_view);
  const float fine_page_world
    = bootstrap_metadata.clip_metadata[0].origin_page_scale.z;
  const glm::vec3 world_origin
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::vec3 world_shift_x
    = glm::vec3(
        inverse_light_view * glm::vec4(fine_page_world, 0.0F, 0.0F, 1.0F))
    - world_origin;

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 31 });
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 31 },
    MakeVirtualRequestFeedback(layout, SequenceNumber { 1 }, { 0U }));

  view_constants.SetCameraPosition(
    view_constants.GetCameraPosition() + world_shift_x);
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 31 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 31 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());
  const auto second_layout = BuildVirtualFeedbackLayout(
    second_virtual_introspection->directional_virtual_metadata.front());
  EXPECT_EQ(CountCurrentLodMappedPagesInClip(second_layout,
              second_virtual_introspection->page_table_entries, 0U),
    0U);
  ASSERT_GT(second_virtual_introspection->page_table_entries.size(), 0U);
  EXPECT_FALSE(oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
    second_virtual_introspection->page_table_entries[0]));
  EXPECT_EQ(second_virtual_introspection->receiver_bootstrap_page_count, 0U);
  EXPECT_EQ(
    second_virtual_introspection->current_frame_reinforcement_page_count, 0U);
}

//! Incompatible request feedback must be rejected and the backend must fall
//! back to current-frame receiver bootstrap instead of silently skipping both
//! feedback refinement and bootstrap seeding.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualIncompatibleFeedbackRebootsReceiverBootstrap)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 same-frame GPU marking contract. "
       "The old receiver-bootstrap feedback reboot path is not the live "
       "missing/dirty authority anymore.";

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

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 132 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 132 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());
  ASSERT_GT(first_virtual_introspection->mapped_page_count, 0U);

  const auto first_layout = BuildVirtualFeedbackLayout(
    first_virtual_introspection->directional_virtual_metadata.front());
  ASSERT_GT(first_layout.pages_per_axis, 0U);
  std::optional<std::uint32_t> requested_page {};
  for (std::uint32_t page_index = 0U; page_index < first_layout.pages_per_level;
    ++page_index) {
    if (page_index >= first_virtual_introspection->page_table_entries.size()) {
      break;
    }
    if (first_virtual_introspection->page_table_entries[page_index] != 0U) {
      requested_page = page_index;
      break;
    }
  }
  ASSERT_TRUE(requested_page.has_value());

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 132 });
  auto incompatible_feedback = MakeVirtualRequestFeedback(
    first_layout, SequenceNumber { 1 }, { *requested_page });
  ++incompatible_feedback.directional_address_space_hash;
  shadow_manager->SubmitVirtualRequestFeedback(
    oxygen::ViewId { 132 }, incompatible_feedback);

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 132 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 132 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());
  EXPECT_FALSE(second_virtual_introspection->used_request_feedback);
  EXPECT_EQ(second_virtual_introspection->feedback_requested_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->feedback_refinement_page_count, 0U);
  EXPECT_GT(second_virtual_introspection->mapped_page_count, 0U);
  EXPECT_EQ(
    second_virtual_introspection->current_frame_reinforcement_page_count, 0U);
}

//! Aged mismatched feedback must not be consumed. Until Phase 4 remap exists,
//! the current frame may still reseed from receiver bounds, but compatible
//! previously resident pages should remain mapped.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualAgedAddressMismatchKeepsCompatibleResidentPagesMapped)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 cache-validity plus same-frame marking "
       "contract. Aged feedback mismatch handling is no longer a live "
       "publication authority test.";

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

  auto view_constants = make_view_constants(
    glm::vec3(0.0F, -8.0F, 5.0F), SequenceNumber { 1 }, Slot { 0 });

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

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 232 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 232 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());
  ASSERT_GT(first_virtual_introspection->mapped_page_count, 0U);

  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto first_layout = BuildVirtualFeedbackLayout(first_metadata);
  const auto first_mapped_clip0_pages = CountMappedPagesInClip(
    first_layout, first_virtual_introspection->page_table_entries, 0U);
  ASSERT_GT(first_mapped_clip0_pages, 0U);

  std::optional<std::uint32_t> first_requested_page {};
  for (std::uint32_t page_index = 0U; page_index < first_layout.pages_per_level;
    ++page_index) {
    if (page_index >= first_virtual_introspection->page_table_entries.size()) {
      break;
    }
    if (first_virtual_introspection->page_table_entries[page_index] != 0U) {
      first_requested_page = page_index;
      break;
    }
  }
  ASSERT_TRUE(first_requested_page.has_value());
  const auto carried_resident_keys = RequestedResidentKeys(first_layout,
    std::span<const std::uint32_t>(
      &first_requested_page.value(), static_cast<std::size_t>(1U)));
  ASSERT_EQ(carried_resident_keys.size(), 1U);

  auto shifted_metadata = first_metadata;
  for (std::uint32_t clip_index = 0U;
    clip_index < shifted_metadata.clip_level_count; ++clip_index) {
    auto& clip = shifted_metadata.clip_metadata[clip_index];
    clip.origin_page_scale.z *= 2.0F;
  }
  auto shifted_layout = first_layout;
  shifted_layout.directional_address_space_hash = oxygen::renderer::internal::
    shadow_detail::HashDirectionalVirtualFeedbackAddressSpace(shifted_metadata);
  ASSERT_NE(shifted_layout.directional_address_space_hash,
    first_layout.directional_address_space_hash);

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 232 });
  for (std::uint64_t frame = 2U; frame <= 4U; ++frame) {
    const auto sequence = SequenceNumber { frame };
    const auto slot = Slot { static_cast<Slot::UnderlyingType>(
      (frame - 1U) % oxygen::frame::kFramesInFlight.get()) };
    view_constants
      = make_view_constants(glm::vec3(0.0F, -8.0F, 5.0F), sequence, slot);
    AdvanceRendererFrame(
      manager, *shadow_manager, view_constants, sequence, slot);
    const auto stable = shadow_manager->PublishForView(oxygen::ViewId { 232 },
      view_constants, manager, shadow_casters, visible_receivers,
      &synthetic_sun, std::chrono::milliseconds(16));
    ASSERT_NE(stable.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
    shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 232 });
  }

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 232 },
    MakeVirtualRequestFeedback(
      shifted_layout, SequenceNumber { 1 }, { *first_requested_page }));

  view_constants = make_view_constants(
    glm::vec3(0.0F, -8.0F, 5.0F), SequenceNumber { 5 }, Slot { 1 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 5 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 232 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 232 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());
  EXPECT_FALSE(second_virtual_introspection->used_request_feedback);
  EXPECT_EQ(second_virtual_introspection->feedback_requested_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->feedback_refinement_page_count, 0U);

  const auto& second_metadata
    = second_virtual_introspection->directional_virtual_metadata.front();
  const auto second_layout = BuildVirtualFeedbackLayout(second_metadata);
  const auto carried_page_index = LocalPageIndexForResidentKey(
    second_layout, carried_resident_keys.front());
  ASSERT_TRUE(carried_page_index.has_value());
  ASSERT_LT(*carried_page_index,
    second_virtual_introspection->page_table_entries.size());
  EXPECT_NE(
    second_virtual_introspection->page_table_entries[*carried_page_index], 0U);

  const auto second_mapped_clip0_pages = CountMappedPagesInClip(
    second_layout, second_virtual_introspection->page_table_entries, 0U);
  EXPECT_GT(second_mapped_clip0_pages, 0U);
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

//! Large movement along the light direction must still reraster touched pages
//! even when the snapped XY lattice is unchanged. Phase 7 now carries that
//! through page-local dirty invalidation instead of forcing a whole-cache
//! depth-guardband reject.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualInvalidatesCleanPagesWhenDepthMappingChanges)
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

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 211 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 211 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());
  ASSERT_GT(first_virtual_introspection->mapped_page_count, 0U);

  const auto& first_metadata
    = first_virtual_introspection->directional_virtual_metadata.front();
  const auto inverse_light_view = glm::inverse(first_metadata.light_view);
  const glm::vec3 light_space_origin_ws
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F));
  const glm::vec3 world_shift_depth
    = glm::vec3(inverse_light_view * glm::vec4(0.0F, 0.0F, 1.0F, 1.0F))
    - light_space_origin_ws;
  const auto cached_depth_range = oxygen::renderer::internal::shadow_detail::
    RecoverDirectionalVirtualDepthRange(first_metadata);
  ASSERT_TRUE(cached_depth_range.valid);
  const float depth_shift_distance
    = std::max(cached_depth_range.half_extent * 2.0F, 512.0F);
  const std::array<glm::vec4, 1> shifted_shadow_casters {
    glm::vec4(glm::vec3(shadow_casters.front())
        + world_shift_depth * depth_shift_distance,
      shadow_casters.front().w),
  };
  const auto first_clip = first_metadata.clip_metadata[0];
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 211 });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 211 },
    view_constants, manager, shifted_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 211 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());
  ASSERT_GT(second_virtual_introspection->mapped_page_count, 0U);
  EXPECT_TRUE(second_virtual_introspection->cache_layout_compatible);
  EXPECT_TRUE(second_virtual_introspection->depth_guardband_valid);
  ASSERT_FALSE(second_virtual_introspection->clipmap_cache_valid.empty());
  ASSERT_FALSE(second_virtual_introspection->clipmap_cache_status.empty());
  EXPECT_TRUE(second_virtual_introspection->clipmap_cache_valid[0]);
  EXPECT_EQ(second_virtual_introspection->clipmap_cache_status[0],
    oxygen::renderer::DirectionalVirtualClipCacheStatus::kValid);

  const auto second_clip
    = second_virtual_introspection->directional_virtual_metadata.front();
  EXPECT_NEAR(first_clip.origin_page_scale.x,
    second_clip.clip_metadata[0].origin_page_scale.x, 1.0e-4F);
  EXPECT_NEAR(first_clip.origin_page_scale.y,
    second_clip.clip_metadata[0].origin_page_scale.y, 1.0e-4F);
  EXPECT_GT(second_virtual_introspection->rerasterized_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->pending_page_count,
    second_virtual_introspection->rerasterized_page_count);
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

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 232 });
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

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 233 });
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
        == oxygen::renderer::DirectionalVirtualClipCacheStatus::kNeverRendered;
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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 235 });
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

//! Current-frame visible-receiver bootstrap must stay sparse. Two far-apart
//! receivers in the same fine clip must not be merged into one dense page box.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualBootstrapKeepsSparseReceiverPagesSparse)
{
  GTEST_SKIP()
    << "Superseded by the Phase 5 explicit coarse-band contract; sparse "
       "coverage is no longer authored by CPU receiver bootstrap alone.";

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
    .SetCameraPosition(glm::vec3(0.0F, -8.0F, 5.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.4F, -0.3F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> bootstrap_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 34 },
    view_constants, manager, bootstrap_shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 34 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());
  const auto& bootstrap_metadata
    = bootstrap_introspection->directional_virtual_metadata.front();
  const auto layout = BuildVirtualFeedbackLayout(bootstrap_metadata);
  ASSERT_GT(layout.pages_per_axis, 10U);
  const float page_world
    = bootstrap_metadata.clip_metadata[0].origin_page_scale.z;
  const auto world_a
    = DirectionalVirtualPageWorldCenter(bootstrap_metadata, 0U, 2U, 2U);
  const auto world_b
    = DirectionalVirtualPageWorldCenter(bootstrap_metadata, 0U, 9U, 9U);
  const auto gap_world
    = DirectionalVirtualPageWorldCenter(bootstrap_metadata, 0U, 5U, 5U);
  const std::array<glm::vec4, 2> sparse_shadow_casters {
    glm::vec4(world_a, page_world * 0.1F),
    glm::vec4(world_b, page_world * 0.1F),
  };
  const std::array<glm::vec4, 2> visible_receivers {
    glm::vec4(world_a, page_world * 0.1F),
    glm::vec4(world_b, page_world * 0.1F),
  };

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 34 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto publication = shadow_manager->PublishForView(oxygen::ViewId { 34 },
    view_constants, manager, sparse_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 34 });

  ASSERT_NE(
    publication.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_introspection, nullptr);
  EXPECT_FALSE(virtual_introspection->used_request_feedback);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());
  const auto& publication_metadata
    = virtual_introspection->directional_virtual_metadata.front();
  const auto publication_layout
    = BuildVirtualFeedbackLayout(publication_metadata);
  const auto sparse_page_a
    = GlobalPageIndexForWorldPoint(publication_metadata, 0U, world_a);
  const auto sparse_page_b
    = GlobalPageIndexForWorldPoint(publication_metadata, 0U, world_b);
  const auto gap_page
    = GlobalPageIndexForWorldPoint(publication_metadata, 0U, gap_world);
  ASSERT_TRUE(sparse_page_a.has_value());
  ASSERT_TRUE(sparse_page_b.has_value());
  ASSERT_TRUE(gap_page.has_value());
  ASSERT_LT(*sparse_page_a, virtual_introspection->page_table_entries.size());
  ASSERT_LT(*sparse_page_b, virtual_introspection->page_table_entries.size());
  ASSERT_LT(*gap_page, virtual_introspection->page_table_entries.size());
  const auto mapped_clip0_pages = CountMappedPagesInClip(
    publication_layout, virtual_introspection->page_table_entries, 0U);

  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasAnyLod(
    virtual_introspection->page_table_entries[*sparse_page_a]));
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasAnyLod(
    virtual_introspection->page_table_entries[*sparse_page_b]));
  EXPECT_FALSE(oxygen::renderer::VirtualShadowPageTableEntryHasAnyLod(
    virtual_introspection->page_table_entries[*gap_page]));
  EXPECT_LT(mapped_clip0_pages, 50U);
}

//! Cold/mismatch receiver bootstrap should only seed the nearest fine clips.
//! Farther fine clips should stay unmapped until feedback or later motion
//! refinement demands them.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualBootstrapCapsCoverageToNearestFineClips)
{
  GTEST_SKIP()
    << "Superseded by the Phase 5 explicit coarse-band contract; cold-start "
       "coverage now includes the stable coarse tail band instead of only "
       "nearest-fine CPU bootstrap pages.";

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
    .SetCameraPosition(glm::vec3(0.0F, -8.0F, 5.0F));
  view_constants.SetFrameSequenceNumber(
    SequenceNumber { 1 }, ViewConstants::kRenderer);
  view_constants.SetFrameSlot(Slot { 0 }, ViewConstants::kRenderer);

  const ShadowManager::SyntheticSunShadowInput synthetic_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.4F, -0.3F, -1.0F)),
    .bias = 0.0F,
    .normal_bias = 0.0F,
    .resolution_hint
    = static_cast<std::uint32_t>(oxygen::scene::ShadowResolutionHint::kMedium),
    .cascade_count = 4U,
    .distribution_exponent = 1.0F,
    .cascade_distances = { 8.0F, 24.0F, 64.0F, 160.0F },
  };
  const std::array<glm::vec4, 1> bootstrap_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 35 },
    view_constants, manager, bootstrap_shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 35 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());

  const auto& bootstrap_metadata
    = bootstrap_introspection->directional_virtual_metadata.front();
  const auto layout = BuildVirtualFeedbackLayout(bootstrap_metadata);
  ASSERT_GE(layout.clip_level_count, 12U);
  const float page_world
    = bootstrap_metadata.clip_metadata[0].origin_page_scale.z;
  const auto world_a
    = DirectionalVirtualPageWorldCenter(bootstrap_metadata, 0U, 2U, 2U);
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(world_a, page_world * 0.1F),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(world_a, page_world * 0.1F),
  };

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 35 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto publication = shadow_manager->PublishForView(oxygen::ViewId { 35 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 35 });

  ASSERT_NE(
    publication.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(virtual_introspection, nullptr);
  EXPECT_FALSE(virtual_introspection->used_request_feedback);
  ASSERT_FALSE(virtual_introspection->directional_virtual_metadata.empty());

  const auto publication_layout = BuildVirtualFeedbackLayout(
    virtual_introspection->directional_virtual_metadata.front());
  const auto coarse_backbone_begin = oxygen::renderer::internal::shadow_detail::
    ResolveDirectionalCoarseBackboneBegin(publication_layout.clip_level_count);

  EXPECT_GT(CountMappedPagesInClip(publication_layout,
              virtual_introspection->page_table_entries, 0U),
    0U);
  EXPECT_GT(CountMappedPagesInClip(publication_layout,
              virtual_introspection->page_table_entries, 1U),
    0U);
  EXPECT_GT(CountMappedPagesInClip(publication_layout,
              virtual_introspection->page_table_entries, 2U),
    0U);
  EXPECT_EQ(CountMappedPagesInClip(publication_layout,
              virtual_introspection->page_table_entries, 3U),
    0U);
  EXPECT_GT(CountMappedPagesInClip(publication_layout,
              virtual_introspection->page_table_entries, coarse_backbone_begin),
    0U);
}

//! Once clipmap remap exists, recent compatible feedback hashes must stay on
//! the feedback/refine path across a one-page clip shift instead of falling
//! back to bootstrap.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualClipShiftAcceptsRecentCompatibleFeedbackHashes)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 same-frame GPU marking contract. "
       "Compatible feedback hashes are retained only as telemetry lineage; "
       "they no longer control live publication across clip shifts.";

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

  auto view_constants = make_view_constants(glm::vec3(0.0F, -8.0F, 5.0F),
    glm::vec3(0.0F, 0.0F, 0.5F), SequenceNumber { 1 }, Slot { 0 });

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

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 138 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 138 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());
  ASSERT_GT(first_virtual_introspection->mapped_page_count, 0U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 138 });

  const auto first_layout = BuildVirtualFeedbackLayout(
    first_virtual_introspection->directional_virtual_metadata.front());
  const auto page_limit = static_cast<std::uint32_t>(
    first_virtual_introspection->page_table_entries.size());
  std::vector<std::uint32_t> requested_pages {};
  requested_pages.reserve(page_limit);
  for (std::uint32_t page_index = 0U; page_index < page_limit; ++page_index) {
    if (first_virtual_introspection->page_table_entries[page_index] == 0U) {
      continue;
    }
    requested_pages.push_back(page_index);
  }
  ASSERT_FALSE(requested_pages.empty());
  const auto requested_resident_keys = RequestedResidentKeys(first_layout,
    std::span<const std::uint32_t>(
      requested_pages.data(), requested_pages.size()));
  ASSERT_FALSE(requested_resident_keys.empty());

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

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 138 },
    MakeVirtualRequestFeedback(first_layout, SequenceNumber { 1 },
      std::span<const std::uint32_t>(
        requested_pages.data(), requested_pages.size())));

  view_constants
    = make_view_constants(glm::vec3(0.0F, -8.0F, 5.0F) + world_shift_x,
      glm::vec3(0.0F, 0.0F, 0.5F), SequenceNumber { 2 }, Slot { 1 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 138 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 138 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());
  EXPECT_TRUE(second_virtual_introspection->used_request_feedback);
  EXPECT_GT(second_virtual_introspection->feedback_requested_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->receiver_bootstrap_page_count, 0U);
  EXPECT_EQ(
    second_virtual_introspection->current_frame_reinforcement_page_count, 0U);
  EXPECT_EQ(
    second_virtual_introspection->current_frame_reinforcement_reference_frame,
    0U);

  const auto second_layout = BuildVirtualFeedbackLayout(
    second_virtual_introspection->directional_virtual_metadata.front());
  EXPECT_NE(second_layout.directional_address_space_hash,
    first_layout.directional_address_space_hash);
  std::optional<std::uint32_t> translated_page_index {};
  for (const auto resident_key : requested_resident_keys) {
    translated_page_index
      = LocalPageIndexForResidentKey(second_layout, resident_key);
    if (translated_page_index.has_value()) {
      break;
    }
  }
  ASSERT_TRUE(translated_page_index.has_value());
  ASSERT_LT(*translated_page_index,
    second_virtual_introspection->page_table_entries.size());
  EXPECT_NE(
    second_virtual_introspection->page_table_entries[*translated_page_index],
    0U);
}

//! Accepted detail feedback that no longer seeds any current-space fine pages
//! must not suppress full same-frame fine coverage. Otherwise the live view
//! collapses to coarse fallback even though the feedback lineage still looks
//! nominally compatible.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualUnseedableAcceptedDetailFeedbackKeepsSameFrameFineCoverage)
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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 244 });

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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 244 });

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
  EXPECT_GT(third_virtual_introspection->selected_page_count,
    third_virtual_introspection->coarse_backbone_page_count);
  EXPECT_GT(CountMappedPagesInClip(third_layout,
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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 139 });

  const auto first_layout = BuildVirtualFeedbackLayout(
    first_virtual_introspection->directional_virtual_metadata.front());
  std::vector<std::uint32_t> requested_pages {};
  requested_pages.reserve(96U);
  const auto page_limit = static_cast<std::uint32_t>(
    first_virtual_introspection->page_table_entries.size());
  for (std::uint32_t page_index = 0U; page_index < page_limit; ++page_index) {
    if (first_virtual_introspection->page_table_entries[page_index] == 0U) {
      continue;
    }
    requested_pages.push_back(page_index);
    if (requested_pages.size() == 96U) {
      break;
    }
  }
  ASSERT_FALSE(requested_pages.empty());

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
      std::span<const std::uint32_t>(
        requested_pages.data(), requested_pages.size())));

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
  ShadowManagerPublishForView_VirtualPlanSpatiallyInvalidatesDirtyPagesOnly)
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
  ASSERT_GT(first_virtual_introspection->resident_page_count, 0U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 32 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 32 },
    view_constants, manager, moved_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16), 0x9001U);
  const auto* second_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 32 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 32 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_GT(second_virtual_plan->resolved_pages.size(), 0U);
  EXPECT_LT(second_virtual_plan->resolved_pages.size(),
    first_virtual_introspection->resident_page_count);
  EXPECT_GT(second_virtual_introspection->clean_page_count, 0U);
}

//! When far-view pressure exposes more virtual pages than the active page
//! budget can map, the planner should keep currently mapped requested pages
//! before rotating in newly requested pages.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanPinsMappedRequestedPagesUnderBudgetPressure)
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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 24 });

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
  const auto* first_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 24 });
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 24 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(first_virtual_plan->resolved_pages.empty());
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 24 });

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
  const auto* second_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 24 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 24 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_virtual_introspection->mapped_page_count,
    first_virtual_introspection->mapped_page_count);
  EXPECT_GT(second_virtual_introspection->mapped_page_count, 0U);
  EXPECT_LE(second_virtual_plan->resolved_pages.size(), 4U);
}

//! Incompatible address-space motion must not republish a stale whole-frame
//! page table. Background reseeding may still occur before Phase 4 remap
//! lands, but the published metadata/page table must stay current-frame-owned.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualAddressSpaceMismatchRejectsLastCoherentPublishFallback)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 live page-management publication path. "
       "Whole-publication fallback is not the authority under the current "
       "design.";

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

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 242 },
    view_constants, manager, huge_shadow_casters, focused_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 242 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_introspection, nullptr);
  ASSERT_FALSE(
    first_virtual_introspection->directional_virtual_metadata.empty());
  ASSERT_FALSE(first_virtual_introspection->page_table_entries.empty());
  const auto first_layout = BuildVirtualFeedbackLayout(
    first_virtual_introspection->directional_virtual_metadata.front());
  const auto first_coarse_clip_index = first_layout.clip_level_count - 1U;
  const std::vector<std::uint32_t> first_page_table_entries(
    first_virtual_introspection->page_table_entries.begin(),
    first_virtual_introspection->page_table_entries.end());
  const auto first_published_metadata
    = first_virtual_introspection->published_directional_virtual_metadata
        .front();
  const auto first_published_mapped_coarse_pages = CountMappedPagesInClip(
    first_layout, first_page_table_entries, first_coarse_clip_index);
  ASSERT_GT(first_published_mapped_coarse_pages, 0U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 242 });

  const ShadowManager::SyntheticSunShadowInput shifted_sun {
    .enabled = true,
    .direction_ws = glm::normalize(glm::vec3(0.355F, -0.45F, -1.0F)),
    .bias = synthetic_sun.bias,
    .normal_bias = synthetic_sun.normal_bias,
    .resolution_hint = synthetic_sun.resolution_hint,
    .cascade_count = synthetic_sun.cascade_count,
    .distribution_exponent = synthetic_sun.distribution_exponent,
    .cascade_distances = synthetic_sun.cascade_distances,
  };
  view_constants
    = make_view_constants(base_camera, SequenceNumber { 2 }, Slot { 1 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 242 },
    view_constants, manager, huge_shadow_casters, focused_visible_receivers,
    &shifted_sun, std::chrono::milliseconds(16));
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 242 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_introspection, nullptr);
  ASSERT_FALSE(
    second_virtual_introspection->directional_virtual_metadata.empty());
  EXPECT_EQ(second_virtual_introspection->feedback_refinement_page_count, 0U);
  EXPECT_EQ(
    second_virtual_introspection->current_frame_reinforcement_page_count, 0U);
  EXPECT_EQ(second_virtual_introspection->published_page_table_entries.size(),
    first_page_table_entries.size());
  EXPECT_FALSE(std::equal(
    second_virtual_introspection->published_page_table_entries.begin(),
    second_virtual_introspection->published_page_table_entries.end(),
    first_page_table_entries.begin(), first_page_table_entries.end()));
  ASSERT_TRUE(second_virtual_introspection->coarse_safety_capacity_fit);
  ASSERT_FALSE(second_virtual_introspection
      ->published_directional_virtual_metadata.empty());
  const auto current_metadata
    = second_virtual_introspection->directional_virtual_metadata.front();
  const auto second_published_metadata
    = second_virtual_introspection->published_directional_virtual_metadata
        .front();
  EXPECT_EQ(oxygen::renderer::internal::shadow_detail::
              HashDirectionalVirtualFeedbackAddressSpace(current_metadata),
    oxygen::renderer::internal::shadow_detail::
      HashDirectionalVirtualFeedbackAddressSpace(second_published_metadata));
  EXPECT_NE(
    oxygen::renderer::internal::shadow_detail::
      HashDirectionalVirtualFeedbackAddressSpace(second_published_metadata),
    oxygen::renderer::internal::shadow_detail::
      HashDirectionalVirtualFeedbackAddressSpace(first_published_metadata));
  const auto second_published_layout
    = BuildVirtualFeedbackLayout(second_published_metadata);
  const auto second_published_coarse_clip_index
    = second_published_layout.clip_level_count - 1U;
  const auto second_published_mapped_coarse_pages
    = CountMappedPagesInClip(second_published_layout,
      second_virtual_introspection->published_page_table_entries,
      second_published_coarse_clip_index);
  EXPECT_GT(second_published_mapped_coarse_pages, 0U);
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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 243 });

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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 247 });

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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 244 });

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

//! Under pressure, invalid resident pages should be evicted before unrelated
//! clean cached pages. Otherwise a moved caster can flush still-valid cached
//! pages for other visible casters and force unnecessary rerasterization when
//! those casters are requested again.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualBudgetPressureEvictsDirtyPagesBeforeCleanCachedPages)
{
  GTEST_SKIP()
    << "Superseded by the deterministic Phase 6 eviction-priority contract "
       "test plus the requested-page pinning and reuse/allocation coherence "
       "tests. The old end-to-end pressure scenario is no longer stable after "
       "the page-management authority split.";

  auto& manager = Manager();
  manager.OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto probe_shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly,
    oxygen::ShadowQualityTier::kLow);
  ASSERT_NE(probe_shadow_manager, nullptr);
  probe_shadow_manager->OnFrameStart(
    RendererTagFactory::Get(), SequenceNumber { 1 }, Slot { 0 });

  auto shadow_manager = CreateShadowManager(
    oxygen::DirectionalShadowImplementationPolicy::kVirtualOnly,
    oxygen::ShadowQualityTier::kLow);
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
  const std::array<glm::vec4, 1> probe_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 1.0F),
  };
  const auto probe = probe_shadow_manager->PublishForView(
    oxygen::ViewId { 901 }, view_constants, manager, probe_shadow_casters, {},
    &synthetic_sun, std::chrono::milliseconds(16));
  ASSERT_NE(probe.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  const auto* probe_introspection = ResolveVirtualViewIntrospection(
    *probe_shadow_manager, oxygen::ViewId { 901 });
  ASSERT_NE(probe_introspection, nullptr);
  ASSERT_FALSE(probe_introspection->directional_virtual_metadata.empty());

  const auto& probe_metadata
    = probe_introspection->directional_virtual_metadata.front();
  const auto layout = BuildVirtualFeedbackLayout(probe_metadata);
  ASSERT_GT(layout.clip_level_count, 3U);
  ASSERT_GE(layout.pages_per_axis, 8U);
  ASSERT_GT(probe_metadata.clip_metadata[0].origin_page_scale.z, 0.0F);

  constexpr std::uint32_t kTrackedClipIndex = 0U;
  constexpr std::uint32_t kTrackedPageX = 1U;
  constexpr std::uint32_t kTrackedPageY = 1U;
  const auto initial_pressure_page_x = layout.pages_per_axis - 3U;
  const auto initial_pressure_page_y = layout.pages_per_axis - 3U;
  const auto moved_pressure_page_x = layout.pages_per_axis / 2U;
  const auto moved_pressure_page_y = layout.pages_per_axis - 3U;
  const float clip0_page_world
    = probe_metadata.clip_metadata[0].origin_page_scale.z;
  const float tracked_caster_radius = clip0_page_world * 0.35F;
  const float pressure_caster_radius = clip0_page_world * 18.0F;
  const glm::vec3 tracked_world = DirectionalVirtualPageWorldCenter(
    probe_metadata, kTrackedClipIndex, kTrackedPageX, kTrackedPageY);
  const glm::vec3 initial_pressure_world
    = DirectionalVirtualPageWorldCenter(probe_metadata, kTrackedClipIndex,
      initial_pressure_page_x, initial_pressure_page_y);
  const glm::vec3 moved_pressure_world
    = DirectionalVirtualPageWorldCenter(probe_metadata, kTrackedClipIndex,
      moved_pressure_page_x, moved_pressure_page_y);
  const auto tracked_page = GlobalPageIndexForWorldPoint(
    probe_metadata, kTrackedClipIndex, tracked_world);
  ASSERT_TRUE(tracked_page.has_value());

  const std::array<glm::vec4, 2> initial_shadow_casters {
    glm::vec4(tracked_world, tracked_caster_radius),
    glm::vec4(initial_pressure_world, pressure_caster_radius),
  };
  const std::array<glm::vec4, 2> pressured_shadow_casters {
    glm::vec4(tracked_world, tracked_caster_radius),
    glm::vec4(moved_pressure_world, pressure_caster_radius),
  };
  const auto fine_clip_count = layout.clip_level_count - 3U;
  const auto fine_request_count = fine_clip_count * layout.pages_per_level;
  ASSERT_GT(fine_request_count, 0U);

  std::vector<std::uint32_t> initial_requested_pages(fine_request_count);
  std::iota(initial_requested_pages.begin(), initial_requested_pages.end(), 0U);

  std::vector<std::uint32_t> pressured_requested_pages {};
  pressured_requested_pages.reserve(fine_request_count - 1U);
  std::unordered_set<std::uint64_t> dirty_candidate_keys {};
  const auto append_dirty_keys_for_bound = [&](const glm::vec4 bound) {
    const float radius = std::max(0.0F, bound.w);
    if (radius <= 0.0F) {
      return;
    }
    const glm::vec3 center_ls = glm::vec3(
      probe_metadata.light_view * glm::vec4(glm::vec3(bound), 1.0F));
    for (std::uint32_t clip_index = 0U; clip_index < layout.clip_level_count;
      ++clip_index) {
      const float page_world_size = std::max(
        probe_metadata.clip_metadata[clip_index].origin_page_scale.z, 1.0e-4F);
      const auto min_grid_x = static_cast<std::int32_t>(
        std::floor((center_ls.x - radius) / page_world_size));
      const auto max_grid_x = static_cast<std::int32_t>(
        std::ceil((center_ls.x + radius) / page_world_size) - 1.0F);
      const auto min_grid_y = static_cast<std::int32_t>(
        std::floor((center_ls.y - radius) / page_world_size));
      const auto max_grid_y = static_cast<std::int32_t>(
        std::ceil((center_ls.y + radius) / page_world_size) - 1.0F);
      for (std::int32_t grid_y = min_grid_y; grid_y <= max_grid_y; ++grid_y) {
        for (std::int32_t grid_x = min_grid_x; grid_x <= max_grid_x; ++grid_x) {
          dirty_candidate_keys.insert(
            oxygen::renderer::internal::shadow_detail::
              PackVirtualResidentPageKey(clip_index, grid_x, grid_y));
        }
      }
    }
  };
  append_dirty_keys_for_bound(
    glm::vec4(initial_pressure_world, pressure_caster_radius));
  append_dirty_keys_for_bound(
    glm::vec4(moved_pressure_world, pressure_caster_radius));
  for (std::uint32_t page_index = 0U; page_index < fine_request_count;
    ++page_index) {
    const auto resident_keys = RequestedResidentKeys(
      layout, std::span<const std::uint32_t>(&page_index, 1U));
    ASSERT_EQ(resident_keys.size(), 1U);
    if (page_index == *tracked_page
      || dirty_candidate_keys.contains(resident_keys.front())) {
      continue;
    }
    pressured_requested_pages.push_back(page_index);
  }
  ASSERT_LT(pressured_requested_pages.size() + 1U, fine_request_count);
  ASSERT_GT(pressured_requested_pages.size(), fine_request_count / 2U);

  (void)shadow_manager->PublishForView(oxygen::ViewId { 241 }, view_constants,
    manager, initial_shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 241 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 241 },
    MakeVirtualRequestFeedback(layout, SequenceNumber { 1 },
      std::span<const std::uint32_t>(
        initial_requested_pages.data(), initial_requested_pages.size())));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 241 },
    view_constants, manager, initial_shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 241 });
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_LT(*tracked_page, first_introspection->page_table_entries.size());
  ASSERT_NE(first_introspection->page_table_entries[*tracked_page], 0U);
  ASSERT_GE(first_introspection->resident_page_count, 200U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 241 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 241 },
    MakeVirtualRequestFeedback(layout, SequenceNumber { 2 },
      std::span<const std::uint32_t>(
        pressured_requested_pages.data(), pressured_requested_pages.size())));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 2 });

  (void)shadow_manager->PublishForView(oxygen::ViewId { 241 }, view_constants,
    manager, pressured_shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16), 0xA001U);
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 241 });
  ASSERT_NE(second_introspection, nullptr);
  EXPECT_GT(second_introspection->evicted_page_count, 0U);
  EXPECT_GT(second_introspection->allocated_page_count, 0U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 241 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 241 },
    MakeVirtualRequestFeedback(
      layout, SequenceNumber { 3 }, { *tracked_page }));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 4 }, Slot { 0 });

  (void)shadow_manager->PublishForView(oxygen::ViewId { 241 }, view_constants,
    manager, pressured_shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16), 0xA001U);
  const auto* third_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 241 });
  const auto* third_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 241 });
  ASSERT_NE(third_introspection, nullptr);
  ASSERT_NE(third_plan, nullptr);
  ASSERT_LT(*tracked_page, third_introspection->page_table_entries.size());
  EXPECT_NE(third_introspection->page_table_entries[*tracked_page], 0U);
  const auto tracked_local_page = *tracked_page % layout.pages_per_level;
  const auto tracked_page_rerasterized = std::any_of(
    third_plan->resolved_pages.begin(), third_plan->resolved_pages.end(),
    [tracked_local_page](const DerivedResolvedVirtualPage& job) {
      return job.clip_level == kTrackedClipIndex
        && job.page_index == tracked_local_page;
    });
  EXPECT_FALSE(tracked_page_rerasterized);
}

//! Virtual pages remain pending until the raster pass executes, so same-frame
//! republishes must not discard the initial virtual raster work.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanKeepsPendingRasterJobsUntilExecuted)
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
  const auto* first_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 13 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_FALSE(first_virtual_plan->resolved_pages.empty());
  const auto* first_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 13 });
  ASSERT_NE(first_virtual_introspection, nullptr);
  EXPECT_EQ(first_virtual_introspection->pending_page_count,
    first_virtual_plan->resolved_pages.size());

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 13 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 13 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 13 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_virtual_plan->resolved_pages.size(),
    first_virtual_plan->resolved_pages.size());
  EXPECT_EQ(second_virtual_introspection->pending_page_count,
    first_virtual_plan->resolved_pages.size());
}

//! Patching the published view-frame slot must leave the explicit resolve
//! contract intact without relying on a CPU pending-job mirror.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanSurvivesViewFrameSlotPatchWithoutCpuMirror)
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

  const auto* virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 15 });
  const auto* virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 15 });
  ASSERT_NE(virtual_plan, nullptr);
  ASSERT_NE(virtual_introspection, nullptr);
  ASSERT_FALSE(virtual_plan->resolved_pages.empty());
  EXPECT_EQ(virtual_introspection->pending_raster_page_count,
    virtual_plan->resolved_pages.size());
  EXPECT_EQ(virtual_plan->depth_texture,
    shadow_manager->GetVirtualShadowDepthTexture().get());
}

//! Virtual page reuse must invalidate shadow contents when caster inputs
//! change, even if the snapped clip metadata and requested pages stay the same.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanRerasterizesWhenCasterInputsChange)
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
  const auto* first_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 12 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_virtual_plan, nullptr);
  ASSERT_FALSE(first_virtual_plan->resolved_pages.empty());

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 12 },
    view_constants, manager, updated_shadow_casters, visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* second_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 12 });
  const auto* second_virtual_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 12 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  ASSERT_NE(second_virtual_introspection, nullptr);
  EXPECT_EQ(second_virtual_plan->resolved_pages.size(),
    first_virtual_plan->resolved_pages.size());
  EXPECT_EQ(second_virtual_introspection->pending_raster_page_count,
    first_virtual_plan->resolved_pages.size());
  EXPECT_EQ(second_virtual_introspection->pending_page_count,
    first_virtual_plan->resolved_pages.size());
  EXPECT_EQ(second.virtual_shadow_physical_pool_srv,
    first.virtual_shadow_physical_pool_srv);
}

//! Physical-pool growth must not reuse iterators into the cleared per-view
//! cache.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPlanSurvivesPhysicalPoolRecreation)
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
  const auto* second_virtual_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 14 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(
    second.virtual_shadow_physical_pool_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_virtual_plan, nullptr);
  EXPECT_FALSE(second_virtual_plan->resolved_pages.empty());
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
  ShadowManagerPublishForView_VirtualPlanInvalidatesWhenCasterContentChanges)
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
  const auto* first_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 19 });
  ASSERT_NE(first.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_plan, nullptr);
  ASSERT_FALSE(first_plan->resolved_pages.empty());

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 19 });
  const auto* executed_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 19 });
  ASSERT_NE(executed_plan, nullptr);
  EXPECT_TRUE(executed_plan->resolved_pages.empty());

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 19 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16), 0x2222U);
  const auto* second_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 19 });
  ASSERT_NE(second.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_plan, nullptr);
  EXPECT_FALSE(second_plan->resolved_pages.empty());
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
  const auto* render_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 232 });

  ASSERT_NE(
    published.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(introspection, nullptr);
  ASSERT_GT(introspection->pending_raster_page_count, 0U);
  ASSERT_NE(render_plan, nullptr);
  ASSERT_FALSE(render_plan->resolved_pages.empty());
  ASSERT_EQ(render_plan->resolved_pages.size(),
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

  const auto* resolved_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 235 });
  const auto* resolved_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 235 });
  ASSERT_NE(resolved_plan, nullptr);
  ASSERT_NE(resolved_introspection, nullptr);
  EXPECT_FALSE(resolved_plan->resolved_pages.empty());
  EXPECT_GT(resolved_introspection->mapped_page_count, 0U);
  EXPECT_GT(resolved_introspection->pending_raster_page_count, 0U);
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
  EXPECT_EQ(pre_resolve_introspection->resolve_resident_pages_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_EQ(
    pre_resolve_introspection->resolve_stats_srv, kInvalidShaderVisibleIndex);

  const auto* resolved_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 236 });
  ASSERT_NE(resolved_introspection, nullptr);
  EXPECT_TRUE(resolved_introspection->has_persistent_gpu_residency_state);
  EXPECT_NE(resolved_introspection->resolve_resident_pages_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_NE(
    resolved_introspection->resolve_stats_srv, kInvalidShaderVisibleIndex);
}

//! Marking the virtual render plan executed must not become a backdoor
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

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 233 });

  const auto* post_mark_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 233 });
  ASSERT_NE(post_mark_introspection, nullptr);
  EXPECT_EQ(post_mark_introspection->mapped_page_count, 0U);
  EXPECT_EQ(post_mark_introspection->pending_raster_page_count, 0U);
  EXPECT_EQ(post_mark_introspection->resident_page_count, 0U);

  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 233 });
  const auto* render_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 233 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_NE(render_plan, nullptr);
  EXPECT_GT(introspection->resident_page_count, 0U);
  EXPECT_GT(introspection->mapped_page_count, 0U);
  EXPECT_GT(introspection->pending_raster_page_count, 0U);
  EXPECT_GT(introspection->pending_page_count, 0U);
  EXPECT_EQ(introspection->clean_page_count, 0U);
  EXPECT_FALSE(render_plan->resolved_pages.empty());
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
  ASSERT_GT(first_resolved->resident_page_count, 0U);
  ASSERT_GT(first_resolved->mapped_page_count, 0U);
  const auto first_mapped_page_count = first_resolved->mapped_page_count;
  const auto first_resident_page_count = first_resolved->resident_page_count;
  const std::vector<oxygen::renderer::VirtualShadowResolveResidentPageEntry>
    first_resident_entries(
      first_resolved->resolve_resident_page_entries.begin(),
      first_resolved->resolve_resident_page_entries.end());

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 234 });

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
  const auto* third_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 234 });
  ASSERT_NE(third_resolved, nullptr);
  ASSERT_NE(third_plan, nullptr);
  EXPECT_EQ(third_resolved->allocated_page_count, 0U);
  EXPECT_EQ(third_resolved->pending_raster_page_count, 0U);
  EXPECT_EQ(third_resolved->mapped_page_count, first_mapped_page_count);
  EXPECT_EQ(third_resolved->resident_page_count, first_resident_page_count);
  EXPECT_EQ(third_resolved->clean_page_count, first_resident_page_count);
  EXPECT_TRUE(third_plan->resolved_pages.empty());
  ASSERT_EQ(third_resolved->resolve_resident_page_entries.size(),
    first_resident_entries.size());
  for (std::size_t i = 0U; i < first_resident_entries.size(); ++i) {
    EXPECT_EQ(third_resolved->resolve_resident_page_entries[i].resident_key,
      first_resident_entries[i].resident_key);
    EXPECT_EQ(third_resolved->resolve_resident_page_entries[i].atlas_tile_x,
      first_resident_entries[i].atlas_tile_x);
    EXPECT_EQ(third_resolved->resolve_resident_page_entries[i].atlas_tile_y,
      first_resident_entries[i].atlas_tile_y);
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

//! Mapped page-table entries represent the current frame's requested coverage
//! and should carry both the valid bit and the requested-this-frame bit.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageTableMarksMappedPagesRequestedThisFrame)
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

  (void)shadow_manager->PublishForView(oxygen::ViewId { 27 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 27 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_GT(introspection->mapped_page_count, 0U);
  bool saw_mapped_page = false;
  for (const auto packed_entry : introspection->page_table_entries) {
    const auto decoded
      = oxygen::renderer::DecodeVirtualShadowPageTableEntry(packed_entry);
    if (!decoded.current_lod_valid) {
      EXPECT_FALSE(decoded.requested_this_frame);
      continue;
    }

    saw_mapped_page = true;
    EXPECT_TRUE(decoded.any_lod_valid);
    EXPECT_TRUE(decoded.requested_this_frame);
  }
  EXPECT_TRUE(saw_mapped_page);
}

//! The published page table must expose sampleable coarse fallback directly in
//! the entry contract. Fine pages without current-L0 data should still carry a
//! valid coarser mapping via `any_lod_valid` and `fallback_lod_offset`.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageTablePublishesFallbackOnlyEntries)
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

  (void)shadow_manager->PublishForView(oxygen::ViewId { 28 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 28 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_FALSE(introspection->directional_virtual_metadata.empty());

  const auto layout = BuildVirtualFeedbackLayout(
    introspection->directional_virtual_metadata.front());
  std::uint32_t fallback_only_entry_count = 0U;
  for (std::uint32_t global_page_index = 0U;
    global_page_index < introspection->page_table_entries.size();
    ++global_page_index) {
    const auto packed_entry
      = introspection->page_table_entries[global_page_index];
    if (packed_entry == 0U) {
      continue;
    }

    const auto decoded
      = oxygen::renderer::DecodeVirtualShadowPageTableEntry(packed_entry);
    if (decoded.current_lod_valid) {
      continue;
    }

    ASSERT_TRUE(decoded.any_lod_valid);
    EXPECT_FALSE(decoded.requested_this_frame);
    EXPECT_GT(decoded.fallback_lod_offset, 0U);

    const auto page_coords = DecodeVirtualPageIndex(layout, global_page_index);
    ASSERT_TRUE(page_coords.has_value());
    const auto resolved_fallback_clip = std::min(layout.clip_level_count - 1U,
      page_coords->clip_index
        + static_cast<std::uint32_t>(decoded.fallback_lod_offset));
    EXPECT_GT(resolved_fallback_clip, page_coords->clip_index);
    ++fallback_only_entry_count;
  }

  EXPECT_GT(fallback_only_entry_count, 0U);
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

  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 234 });
  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 234 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_FALSE(introspection->page_table_entries.empty());
  ASSERT_GT(introspection->mapped_page_count, 0U);

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

  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 235 });
  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 235 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_FALSE(introspection->page_flags_entries.empty());
  ASSERT_EQ(introspection->page_flags_entries.size(),
    introspection->page_table_entries.size());
  ASSERT_FALSE(introspection->directional_virtual_metadata.empty());

  const auto layout = BuildVirtualFeedbackLayout(
    introspection->directional_virtual_metadata.front());
  const auto coarse_backbone_begin
    = layout.clip_level_count > 3U ? layout.clip_level_count - 3U : 0U;
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

  bool saw_detail_page = false;
  bool saw_coarse_page = false;
  bool saw_marked_current_page = false;
  bool saw_unmarked_current_page = false;
  bool saw_fallback_only_entry = false;
  for (std::size_t i = 0U; i < introspection->page_flags_entries.size(); ++i) {
    if (introspection->page_table_entries[i] == 0U) {
      EXPECT_EQ(introspection->page_flags_entries[i], 0U);
      continue;
    }

    const auto decoded = oxygen::renderer::DecodeVirtualShadowPageTableEntry(
      introspection->page_table_entries[i]);
    const auto page_flags = introspection->page_flags_entries[i];
    if (!decoded.current_lod_valid) {
      EXPECT_TRUE(decoded.any_lod_valid);
      EXPECT_EQ(page_flags, 0U);
      saw_fallback_only_entry = true;
      continue;
    }

    EXPECT_TRUE(oxygen::renderer::HasVirtualShadowPageFlag(
      page_flags, oxygen::renderer::VirtualShadowPageFlag::kAllocated));
    const auto clip_index = layout.pages_per_level == 0U
      ? 0U
      : static_cast<std::uint32_t>(i / layout.pages_per_level);
    const bool detail_geometry = oxygen::renderer::HasVirtualShadowPageFlag(
      page_flags, oxygen::renderer::VirtualShadowPageFlag::kDetailGeometry);
    const bool used_this_frame = oxygen::renderer::HasVirtualShadowPageFlag(
      page_flags, oxygen::renderer::VirtualShadowPageFlag::kUsedThisFrame);
    if (detail_geometry) {
      EXPECT_LT(clip_index, coarse_backbone_begin);
    }
    saw_detail_page = saw_detail_page || detail_geometry;
    saw_coarse_page = saw_coarse_page || !detail_geometry;
    saw_marked_current_page = saw_marked_current_page || used_this_frame;
    saw_unmarked_current_page = saw_unmarked_current_page || !used_this_frame;
  }
  EXPECT_TRUE(saw_coarse_page);
  EXPECT_TRUE(saw_marked_current_page);
  EXPECT_TRUE(saw_unmarked_current_page);
  EXPECT_TRUE(saw_fallback_only_entry);
}

//! Phase 7 fallback-only aliases must remain pure page-table aliases; hierarchy
//! propagation may not stamp descendant flags onto them.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualFallbackOnlyAliasesKeepZeroPageFlags)
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
    .SetCameraPosition(glm::vec3(0.0F, -8.0F, 3.0F));
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

  (void)shadow_manager->PublishForView(oxygen::ViewId { 236 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 236 });

  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 236 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_EQ(introspection->page_table_entries.size(),
    introspection->page_flags_entries.size());

  bool saw_fallback_only_entry = false;
  for (std::size_t i = 0U; i < introspection->page_table_entries.size(); ++i) {
    const auto decoded = oxygen::renderer::DecodeVirtualShadowPageTableEntry(
      introspection->page_table_entries[i]);
    if (decoded.current_lod_valid || !decoded.any_lod_valid) {
      continue;
    }
    EXPECT_EQ(introspection->page_flags_entries[i], 0U);
    saw_fallback_only_entry = true;
  }

  EXPECT_TRUE(saw_fallback_only_entry);
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
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 245 });

  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 245 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_FALSE(introspection->directional_virtual_metadata.empty());

  const auto layout = BuildVirtualFeedbackLayout(
    introspection->directional_virtual_metadata.front());
  std::uint32_t expected_schedule_count = 0U;
  for (std::size_t i = 0U; i < introspection->page_table_entries.size(); ++i) {
    const auto entry = oxygen::renderer::DecodeVirtualShadowPageTableEntry(
      introspection->page_table_entries[i]);
    if (!entry.current_lod_valid) {
      continue;
    }

    const auto page_flags = introspection->page_flags_entries[i];
    const bool uncached
      = oxygen::renderer::HasVirtualShadowPageFlag(
          page_flags, oxygen::renderer::VirtualShadowPageFlag::kDynamicUncached)
      || oxygen::renderer::HasVirtualShadowPageFlag(
        page_flags, oxygen::renderer::VirtualShadowPageFlag::kStaticUncached);
    if (uncached) {
      ++expected_schedule_count;
    }
  }

  ASSERT_GT(expected_schedule_count, 0U);
  const auto resolved_pages = DeriveResolvedRasterPages(*introspection);
  EXPECT_EQ(resolved_pages.size(), expected_schedule_count);
  for (const auto& page : resolved_pages) {
    const auto global_page_index
      = page.clip_level * layout.pages_per_level + page.page_index;
    ASSERT_LT(global_page_index, introspection->page_table_entries.size());
    const auto entry = oxygen::renderer::DecodeVirtualShadowPageTableEntry(
      introspection->page_table_entries[global_page_index]);
    const auto page_flags
      = introspection->page_flags_entries[global_page_index];
    EXPECT_TRUE(entry.current_lod_valid);
    EXPECT_TRUE(oxygen::renderer::HasVirtualShadowPageFlag(
      page_flags, oxygen::renderer::VirtualShadowPageFlag::kAllocated));
    EXPECT_TRUE(oxygen::renderer::HasVirtualShadowPageFlag(page_flags,
                  oxygen::renderer::VirtualShadowPageFlag::kDynamicUncached)
      || oxygen::renderer::HasVirtualShadowPageFlag(
        page_flags, oxygen::renderer::VirtualShadowPageFlag::kStaticUncached));
  }
}

//! Accepted feedback is telemetry on the live fine-page path once the current
//! frame has valid receiver coverage. The current receiver region must still
//! allocate/map its own fine page instead of staying pinned to the old
//! feedback-selected page.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualAcceptedFineFeedbackDoesNotOverrideCurrentReceiverSelection)
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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 248 });

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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 248 });
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
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
    second_introspection->page_table_entries[*second_page_index]));
}

//! Accepted fine feedback must not reselect the whole fine receiver region when
//! the absolute receiver region is unchanged. Stable zero-shift frames should
//! keep same-frame detail contribution at zero so live fine capacity does not
//! collapse back into coarse fallback.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualStableAcceptedFineFeedbackDoesNotReselectWholeFineRegion)
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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 249 });

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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 249 });
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

  EXPECT_TRUE(second_introspection->used_request_feedback);
  EXPECT_GT(second_introspection->feedback_requested_page_count, 0U);
  EXPECT_EQ(second_introspection->same_frame_detail_page_count, 0U);
  EXPECT_EQ(second_introspection->selected_page_count,
    second_introspection->coarse_backbone_page_count
      + second_introspection->feedback_requested_page_count);
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
    second_introspection->page_table_entries[*page_index]));
}

//! Stable zero-shift frames without accepted fine feedback must keep same-frame
//! fine coverage alive. Publishing only the coarse backbone in that state makes
//! the live view fall back to visibly wrong coarse pages during bootstrap and
//! after feedback resets.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualStableNoFeedbackKeepsSameFrameFineCoverage)
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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 250 });

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
  EXPECT_GT(second_introspection->same_frame_detail_page_count, 0U);
  EXPECT_GT(second_introspection->selected_page_count,
    second_introspection->coarse_backbone_page_count);
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
    second_introspection->page_table_entries[*page_index]));
}

//! Without accepted fine feedback, fine bootstrap must stay receiver-driven
//! under huge caster bounds. Falling back to the whole fine frustum here burns
//! the physical pool on pages the current frame never samples, which is what
//! causes the visible startup collapse to wrong coarse fallback.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualNoFeedbackBootstrapPrefersReceiverRegionOverFineFrustum)
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
  EXPECT_LT(focused_introspection->same_frame_detail_page_count,
    unbounded_introspection->same_frame_detail_page_count);
  EXPECT_LT(
    focused_introspection->selected_page_count, unbounded_introspection->selected_page_count);

  const auto focused_page_index = CurrentLodGlobalPageIndexForWorldPoint(
    focused_introspection->directional_virtual_metadata.front(),
    focused_introspection->page_table_entries, glm::vec3(0.0F));
  EXPECT_TRUE(focused_page_index.has_value());
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
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 264 });

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
  EXPECT_GT(introspection->same_frame_detail_page_count, 0U);
  EXPECT_FALSE(receiver_page_entry.current_lod_valid);
  EXPECT_FALSE(receiver_page_entry.any_lod_valid);
}

//! Phase 7 static/dynamic separation must keep static pages clean when only an
//! unrelated dynamic caster moves.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualStaticPagesStayCleanWhenDynamicCasterMovesElsewhere)
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
  const std::array<std::uint8_t, 2> static_flags { 1U, 0U };
  const std::array<glm::vec4, 1> bootstrap_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.45F),
  };
  const std::array<glm::vec4, 1> bootstrap_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.2F),
  };
  (void)shadow_manager->PublishForView(oxygen::ViewId { 246 }, view_constants,
    manager, bootstrap_shadow_casters, bootstrap_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 246 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());

  const auto& bootstrap_metadata
    = bootstrap_introspection->directional_virtual_metadata.front();
  const auto center_page_x = bootstrap_metadata.pages_per_axis / 2U;
  const auto center_page_y = bootstrap_metadata.pages_per_axis / 2U;
  ASSERT_GT(center_page_x, 8U);
  ASSERT_LT(center_page_x + 9U, bootstrap_metadata.pages_per_axis);

  const auto static_world = DirectionalVirtualPageWorldCenter(
    bootstrap_metadata, 0U, center_page_x - 8U, center_page_y, 0.5F);
  const auto first_dynamic_world = DirectionalVirtualPageWorldCenter(
    bootstrap_metadata, 0U, center_page_x + 8U, center_page_y, 0.5F);
  const auto second_dynamic_world = DirectionalVirtualPageWorldCenter(
    bootstrap_metadata, 0U, center_page_x + 9U, center_page_y, 0.5F);

  // Phase 7 republishes all in-layout resident pages through the live
  // page-management path. Establish a clean baseline before the dynamic caster
  // moves, otherwise the bootstrap frame's unrasterized dirty pages remain in
  // the physical page lists and the test stops observing the static/dynamic
  // split it is meant to validate.
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 246 });
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 246 });

  const std::array<glm::vec4, 2> first_shadow_casters {
    glm::vec4(static_world, 0.45F),
    glm::vec4(first_dynamic_world, 0.45F),
  };
  const std::array<glm::vec4, 2> first_visible_receivers {
    glm::vec4(static_world.x, static_world.y, static_world.z - 0.5F, 0.2F),
    glm::vec4(first_dynamic_world.x, first_dynamic_world.y,
      first_dynamic_world.z - 0.5F, 0.2F),
  };
  const std::array<glm::vec4, 2> second_shadow_casters {
    glm::vec4(static_world, 0.45F),
    glm::vec4(second_dynamic_world, 0.45F),
  };
  const std::array<glm::vec4, 2> second_visible_receivers {
    glm::vec4(static_world.x, static_world.y, static_world.z - 0.5F, 0.2F),
    glm::vec4(second_dynamic_world.x, second_dynamic_world.y,
      second_dynamic_world.z - 0.5F, 0.2F),
  };

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });
  (void)shadow_manager->PublishForView(oxygen::ViewId { 246 }, view_constants,
    manager, first_shadow_casters, first_visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16), 0U, static_flags);
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 246 });
  // The live path commits directional virtual pages during the explicit
  // resolve pass, so establish the static/dynamic baseline from the resolved
  // publication rather than the pre-resolve publish snapshot.
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 246 });
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_FALSE(first_introspection->directional_virtual_metadata.empty());
  const auto first_static_page_index = CurrentLodGlobalPageIndexForWorldPoint(
    first_introspection->directional_virtual_metadata.front(),
    first_introspection->page_table_entries, static_world);
  ASSERT_TRUE(first_static_page_index.has_value());
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 246 });
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 2 });

  (void)shadow_manager->PublishForView(oxygen::ViewId { 246 }, view_constants,
    manager, second_shadow_casters, second_visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16), 0U, static_flags);
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 246 });

  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 246 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_FALSE(introspection->directional_virtual_metadata.empty());
  const auto first_address_space_hash = oxygen::renderer::internal::
    shadow_detail::HashDirectionalVirtualFeedbackAddressSpace(
      first_introspection->directional_virtual_metadata.front());
  const auto second_address_space_hash = oxygen::renderer::internal::
    shadow_detail::HashDirectionalVirtualFeedbackAddressSpace(
      introspection->directional_virtual_metadata.front());
  ASSERT_EQ(first_address_space_hash, second_address_space_hash);
  const auto& second_metadata
    = introspection->directional_virtual_metadata.front();
  const auto static_current_lod_page_index
    = CurrentLodGlobalPageIndexForWorldPoint(
      second_metadata, introspection->page_table_entries, static_world);
  const auto moved_dynamic_current_lod_page_index
    = CurrentLodGlobalPageIndexForWorldPoint(
      second_metadata, introspection->page_table_entries, second_dynamic_world);

  ASSERT_TRUE(static_current_lod_page_index.has_value());
  ASSERT_TRUE(moved_dynamic_current_lod_page_index.has_value());
  ASSERT_LT(
    *static_current_lod_page_index, introspection->page_table_entries.size());
  ASSERT_LT(*moved_dynamic_current_lod_page_index,
    introspection->page_table_entries.size());
  ASSERT_NE(
    *static_current_lod_page_index, *moved_dynamic_current_lod_page_index)
    << "first_static_page_index=" << *first_static_page_index
    << " static_current_lod_page_index=" << *static_current_lod_page_index
    << " moved_dynamic_current_lod_page_index="
    << *moved_dynamic_current_lod_page_index << " static_coverage={"
    << DescribeCurrentLodCoverageForWorldPoint(
         second_metadata, introspection->page_table_entries, static_world)
    << "}"
    << " moved_dynamic_coverage={"
    << DescribeCurrentLodCoverageForWorldPoint(second_metadata,
         introspection->page_table_entries, second_dynamic_world)
    << "}"
    << " selected_page_count=" << introspection->selected_page_count
    << " resident_page_count=" << introspection->resident_page_count
    << " mapped_page_count=" << introspection->resolve_stats.mapped_page_count
    << " requested_list_count=" << introspection->requested_page_list_count
    << " dirty_list_count=" << introspection->dirty_page_list_count
    << " clean_list_count=" << introspection->clean_page_list_count;

  const auto static_page_flags
    = introspection->page_flags_entries[*static_current_lod_page_index];
  const auto moved_dynamic_page_flags
    = introspection->page_flags_entries[*moved_dynamic_current_lod_page_index];
  const auto static_page_debug = DescribeCurrentLodCoverageForWorldPoint(
    second_metadata, introspection->page_table_entries, static_world);
  const auto moved_dynamic_page_debug = DescribeCurrentLodCoverageForWorldPoint(
    second_metadata, introspection->page_table_entries, second_dynamic_world);

  EXPECT_TRUE(oxygen::renderer::HasVirtualShadowPageFlag(
    static_page_flags, oxygen::renderer::VirtualShadowPageFlag::kAllocated))
    << "static_current=" << *static_current_lod_page_index
    << " first_static=" << *first_static_page_index << " static_flags=0x"
    << std::hex << static_page_flags << std::dec << " static_coverage={"
    << static_page_debug << "}"
    << " cache_layout_compatible=" << introspection->cache_layout_compatible
    << " depth_guardband_valid=" << introspection->depth_guardband_valid
    << " clip0_cache_valid=" << introspection->clipmap_cache_valid[0]
    << " clip0_cache_status="
    << static_cast<std::uint32_t>(introspection->clipmap_cache_status[0])
    << " clean_pages=" << introspection->clean_page_count
    << " dirty_pages=" << introspection->dirty_page_count
    << " pending_pages=" << introspection->pending_page_count;
  EXPECT_FALSE(oxygen::renderer::HasVirtualShadowPageFlag(static_page_flags,
    oxygen::renderer::VirtualShadowPageFlag::kDynamicUncached))
    << "static_current=" << *static_current_lod_page_index
    << " first_static=" << *first_static_page_index << " static_flags=0x"
    << std::hex << static_page_flags << std::dec << " static_coverage={"
    << static_page_debug << "}"
    << " cache_layout_compatible=" << introspection->cache_layout_compatible
    << " depth_guardband_valid=" << introspection->depth_guardband_valid
    << " clip0_cache_valid=" << introspection->clipmap_cache_valid[0]
    << " clip0_cache_status="
    << static_cast<std::uint32_t>(introspection->clipmap_cache_status[0])
    << " clean_pages=" << introspection->clean_page_count
    << " dirty_pages=" << introspection->dirty_page_count
    << " pending_pages=" << introspection->pending_page_count;
  EXPECT_FALSE(oxygen::renderer::HasVirtualShadowPageFlag(static_page_flags,
    oxygen::renderer::VirtualShadowPageFlag::kStaticUncached))
    << "static_current=" << *static_current_lod_page_index
    << " first_static=" << *first_static_page_index << " static_flags=0x"
    << std::hex << static_page_flags << std::dec << " static_coverage={"
    << static_page_debug << "}"
    << " cache_layout_compatible=" << introspection->cache_layout_compatible
    << " depth_guardband_valid=" << introspection->depth_guardband_valid
    << " clip0_cache_valid=" << introspection->clipmap_cache_valid[0]
    << " clip0_cache_status="
    << static_cast<std::uint32_t>(introspection->clipmap_cache_status[0])
    << " clean_pages=" << introspection->clean_page_count
    << " dirty_pages=" << introspection->dirty_page_count
    << " pending_pages=" << introspection->pending_page_count;

  EXPECT_TRUE(
    oxygen::renderer::HasVirtualShadowPageFlag(moved_dynamic_page_flags,
      oxygen::renderer::VirtualShadowPageFlag::kAllocated))
    << "dynamic_current=" << *moved_dynamic_current_lod_page_index
    << " dynamic_flags=0x" << std::hex << moved_dynamic_page_flags << std::dec
    << " dynamic_coverage={" << moved_dynamic_page_debug << "}";
  EXPECT_TRUE(
    oxygen::renderer::HasVirtualShadowPageFlag(moved_dynamic_page_flags,
      oxygen::renderer::VirtualShadowPageFlag::kDynamicUncached))
    << "dynamic_current=" << *moved_dynamic_current_lod_page_index
    << " dynamic_flags=0x" << std::hex << moved_dynamic_page_flags << std::dec
    << " dynamic_coverage={" << moved_dynamic_page_debug << "}";
  EXPECT_FALSE(
    oxygen::renderer::HasVirtualShadowPageFlag(moved_dynamic_page_flags,
      oxygen::renderer::VirtualShadowPageFlag::kStaticUncached))
    << "dynamic_current=" << *moved_dynamic_current_lod_page_index
    << " dynamic_flags=0x" << std::hex << moved_dynamic_page_flags << std::dec
    << " dynamic_coverage={" << moved_dynamic_page_debug << "}";
}

NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageFlagsPropagateHierarchyToCoarserPages)
{
  GTEST_SKIP()
    << "Superseded by the explicit Phase 6 hierarchy-visibility contract "
       "test and the live fallback consumer path using published page flags. "
       "The old broad publish scenario depended on pre-split CPU flag "
       "propagation details.";

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
  const std::array<glm::vec4, 1> probe_shadow_casters {
    glm::vec4(0.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> probe_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.05F),
  };

  (void)shadow_manager->PublishForView(oxygen::ViewId { 238 }, view_constants,
    manager, probe_shadow_casters, probe_visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* bootstrap_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 238 });
  ASSERT_NE(bootstrap_introspection, nullptr);
  ASSERT_FALSE(bootstrap_introspection->directional_virtual_metadata.empty());

  const auto layout = BuildVirtualFeedbackLayout(
    bootstrap_introspection->directional_virtual_metadata.front());
  ASSERT_GT(layout.clip_level_count, 1U);
  const auto tracked_page_x = layout.pages_per_axis / 2U;
  const auto tracked_page_y = layout.pages_per_axis / 2U;
  const auto tracked_world = DirectionalVirtualPageWorldCenter(
    bootstrap_introspection->directional_virtual_metadata.front(), 0U,
    tracked_page_x, tracked_page_y);
  const float tracked_radius
    = bootstrap_introspection->directional_virtual_metadata.front()
        .clip_metadata[0]
        .origin_page_scale.z
    * 0.35F;
  const std::array<glm::vec4, 1> shadow_casters {
    glm::vec4(tracked_world, tracked_radius),
  };
  const std::array<glm::vec4, 1> visible_receivers {
    glm::vec4(
      tracked_world.x, tracked_world.y, tracked_world.z, tracked_radius),
  };
  const auto tracked_page = GlobalPageIndexForWorldPoint(
    bootstrap_introspection->directional_virtual_metadata.front(), 0U,
    tracked_world);
  ASSERT_TRUE(tracked_page.has_value());

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 238 });
  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 238 },
    MakeVirtualRequestFeedback(
      layout, SequenceNumber { 1 }, { *tracked_page }));
  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  (void)shadow_manager->PublishForView(oxygen::ViewId { 238 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 238 });
  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 238 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_FALSE(introspection->page_flags_entries.empty());

  const auto tracked_local_page_index = *tracked_page % layout.pages_per_level;
  const auto tracked_page_index = tracked_local_page_index;
  ASSERT_LT(tracked_page_index, introspection->page_flags_entries.size());
  const auto tracked_page_flags
    = introspection->page_flags_entries[tracked_page_index];
  ASSERT_TRUE(oxygen::renderer::HasVirtualShadowPageFlag(tracked_page_flags,
    oxygen::renderer::VirtualShadowPageFlag::kDetailGeometry));

  const auto parent_clip_index = 1U;
  const auto propagated_parent_page_index = GlobalPageIndexForWorldPoint(
    introspection->directional_virtual_metadata.front(), parent_clip_index,
    tracked_world);
  ASSERT_TRUE(propagated_parent_page_index.has_value());
  ASSERT_LT(
    *propagated_parent_page_index, introspection->page_flags_entries.size());
  const auto parent_flags
    = introspection->page_flags_entries[*propagated_parent_page_index];
  EXPECT_TRUE(oxygen::renderer::HasVirtualShadowPageFlag(parent_flags,
    oxygen::renderer::VirtualShadowPageFlag::kHierarchyAllocatedDescendant));
  EXPECT_TRUE(oxygen::renderer::HasVirtualShadowPageFlag(parent_flags,
    oxygen::renderer::VirtualShadowPageFlag::kHierarchyDetailDescendant));
}

NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPrepareVirtualPageTableResources_UploadsPhysicalPageManagementBuffers)
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

  (void)shadow_manager->PublishForView(oxygen::ViewId { 239 }, view_constants,
    manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));

  shadow_manager->ResolveVirtualCurrentFrame(oxygen::ViewId { 239 });
  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 239 });
  ASSERT_NE(introspection, nullptr);
  ASSERT_FALSE(introspection->physical_page_metadata_entries.empty());
  ASSERT_FALSE(introspection->physical_page_list_entries.empty());
  EXPECT_EQ(introspection->requested_page_list_count
      + introspection->dirty_page_list_count
      + introspection->clean_page_list_count
      + introspection->available_page_list_count,
    introspection->physical_page_list_entries.size());
  EXPECT_NE(
    introspection->physical_page_metadata_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(introspection->physical_page_lists_srv, kInvalidShaderVisibleIndex);

  GfxPtr()->buffer_log_ = {};
  auto recorder = GfxPtr()->AcquireCommandRecorder(
    SingleQueueStrategy().KeyFor(oxygen::graphics::QueueRole::kGraphics),
    "VirtualPhysicalPageManagementUploadAfterResolve", false);
  ASSERT_NE(recorder, nullptr);

  shadow_manager->PrepareVirtualPageTableResources(
    oxygen::ViewId { 239 }, *recorder);

  const auto& copy_log = GfxPtr()->buffer_log_;
  ASSERT_TRUE(copy_log.copy_called);

  auto matches_expected_upload
    = [](const oxygen::renderer::testing::BufferCommandLog::CopyEvent& copy,
        const void* expected_bytes, const std::size_t expected_size) -> bool {
    if (copy.src == nullptr || copy.size != expected_size) {
      return false;
    }

    auto* upload_buffer = const_cast<oxygen::graphics::Buffer*>(copy.src);
    upload_buffer->UnMap();
    auto* upload_bytes = static_cast<std::byte*>(upload_buffer->Map());
    if (upload_bytes == nullptr) {
      return false;
    }

    return std::memcmp(upload_bytes, expected_bytes, expected_size) == 0;
  };

  EXPECT_NE(
    std::find_if(copy_log.copies.begin(), copy_log.copies.end(),
      [&](const auto& copy) {
        return matches_expected_upload(copy,
          introspection->physical_page_metadata_entries.data(),
          introspection->physical_page_metadata_entries.size()
            * sizeof(oxygen::renderer::VirtualShadowPhysicalPageMetadata));
      }),
    copy_log.copies.end());
  EXPECT_NE(
    std::find_if(copy_log.copies.begin(), copy_log.copies.end(),
      [&](const auto& copy) {
        return matches_expected_upload(copy,
          introspection->physical_page_list_entries.data(),
          introspection->physical_page_list_entries.size()
            * sizeof(oxygen::renderer::VirtualShadowPhysicalPageListEntry));
      }),
    copy_log.copies.end());
}

//! Phase 6 page management must keep the CPU mirror coherent with reuse and
//! new allocations while the live page table/page flags are GPU-authored from
//! the physical-page metadata and list outputs.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualPageManagementOutputsStayCoherentAfterReuseAndAllocation)
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
    glm::vec4(5.0F, 0.0F, 0.5F, 0.5F),
  };
  const std::array<glm::vec4, 1> first_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.1F),
  };
  const std::array<glm::vec4, 2> second_visible_receivers {
    glm::vec4(0.0F, 0.0F, 0.0F, 0.1F),
    glm::vec4(5.0F, 0.0F, 0.0F, 0.1F),
  };

  const auto bootstrap = shadow_manager->PublishForView(oxygen::ViewId { 240 },
    view_constants, manager, shadow_casters, first_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  ASSERT_NE(bootstrap.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 240 });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 240 },
    view_constants, manager, shadow_casters, first_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 240 });

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 3 }, Slot { 2 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 240 },
    view_constants, manager, shadow_casters, second_visible_receivers,
    &synthetic_sun, std::chrono::milliseconds(16));
  const auto* introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 240 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(introspection, nullptr);
  ASSERT_FALSE(introspection->directional_virtual_metadata.empty());
  ASSERT_FALSE(introspection->page_table_entries.empty());
  ASSERT_EQ(introspection->page_table_entries.size(),
    introspection->page_flags_entries.size());
  ASSERT_FALSE(introspection->physical_page_list_entries.empty());
  EXPECT_GT(introspection->allocated_page_count, 0U);
  EXPECT_GE(introspection->resolve_stats.resident_entry_count,
    introspection->allocated_page_count);

  const auto second_layout = BuildVirtualFeedbackLayout(
    introspection->directional_virtual_metadata.front());
  const auto active_page_list_count = introspection->requested_page_list_count
    + introspection->dirty_page_list_count
    + introspection->clean_page_list_count;
  ASSERT_LE(
    active_page_list_count, introspection->physical_page_list_entries.size());

  for (std::size_t i = 0U; i < active_page_list_count; ++i) {
    const auto& entry = introspection->physical_page_list_entries[i];
    const auto page_index
      = LocalPageIndexForResidentKey(second_layout, entry.resident_key);
    ASSERT_TRUE(page_index.has_value());
    ASSERT_LT(*page_index, introspection->page_table_entries.size());

    const auto packed_entry = introspection->page_table_entries[*page_index];
    EXPECT_NE(packed_entry, 0U);
    EXPECT_TRUE(
      oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(packed_entry));
    constexpr auto kBaseFlagsMask
      = oxygen::renderer::ToMask(
          oxygen::renderer::VirtualShadowPageFlag::kAllocated)
      | oxygen::renderer::ToMask(
        oxygen::renderer::VirtualShadowPageFlag::kDynamicUncached)
      | oxygen::renderer::ToMask(
        oxygen::renderer::VirtualShadowPageFlag::kStaticUncached);
    EXPECT_EQ(introspection->page_flags_entries[*page_index] & kBaseFlagsMask,
      entry.page_flags & kBaseFlagsMask);
    EXPECT_TRUE(oxygen::renderer::HasVirtualShadowPageFlag(
      entry.page_flags, oxygen::renderer::VirtualShadowPageFlag::kAllocated));
  }
}

//! The bridge resolve snapshot must mirror resident pages deterministically so
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
  EXPECT_NE(first_introspection->resolve_resident_pages_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_NE(first_introspection->resolve_stats_srv, kInvalidShaderVisibleIndex);
  ASSERT_EQ(first_introspection->resolve_resident_page_entries.size(),
    first_introspection->resident_page_count);
  EXPECT_EQ(first_introspection->resolve_stats.resident_entry_count,
    first_introspection->resident_page_count);
  EXPECT_EQ(first_introspection->resolve_stats.pending_raster_page_count,
    first_introspection->pending_raster_page_count);

  std::uint32_t entry_clean_count = 0U;
  std::uint32_t entry_dirty_count = 0U;
  std::uint32_t entry_pending_count = 0U;
  std::uint64_t previous_key = 0U;
  bool first_entry = true;
  for (const auto& entry : first_introspection->resolve_resident_page_entries) {
    if (!first_entry) {
      EXPECT_LT(previous_key, entry.resident_key);
    }
    first_entry = false;
    previous_key = entry.resident_key;

    switch (static_cast<oxygen::renderer::VirtualPageResidencyState>(
      entry.residency_state)) {
    case oxygen::renderer::VirtualPageResidencyState::kResidentClean:
      ++entry_clean_count;
      break;
    case oxygen::renderer::VirtualPageResidencyState::kResidentDirty:
      ++entry_dirty_count;
      break;
    case oxygen::renderer::VirtualPageResidencyState::kPendingRender:
      ++entry_pending_count;
      break;
    case oxygen::renderer::VirtualPageResidencyState::kUnmapped:
      break;
    }
  }
  EXPECT_EQ(entry_clean_count, first_introspection->clean_page_count);
  EXPECT_EQ(entry_dirty_count, first_introspection->dirty_page_count);
  EXPECT_EQ(entry_pending_count, first_introspection->pending_page_count);

  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 228 });
  const auto* executed_introspection
    = shadow_manager->TryGetVirtualViewIntrospection(oxygen::ViewId { 228 });
  ASSERT_NE(executed_introspection, nullptr);
  EXPECT_TRUE(executed_introspection->has_persistent_gpu_residency_state);
  EXPECT_NE(executed_introspection->resolve_resident_pages_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_NE(executed_introspection->resolve_stats_srv, kInvalidShaderVisibleIndex);
}

//! Bridge resolve resources must stay stable per view across republishes so the
//! later resolve pass can own one persistent residency surface.
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
  ASSERT_NE(first_introspection->resolve_resident_pages_srv,
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
  EXPECT_EQ(second_introspection->resolve_resident_pages_srv,
    first_introspection->resolve_resident_pages_srv);
  EXPECT_EQ(second_introspection->resolve_stats_srv,
    first_introspection->resolve_stats_srv);
}

//! Same-frame request feedback must not replace the just-rendered page table.
//! Virtual feedback is produced after the virtual raster pass, so it is only
//! valid for the next frame's publication.
NOLINT_TEST_F(LightManagerTest,
  ShadowManagerPublishForView_VirtualFeedbackIsConsumedNextFrame)
{
  GTEST_SKIP()
    << "Superseded by the Phase 7 same-frame GPU marking contract. "
       "Readback feedback remains telemetry-only and is not allowed to "
       "replace current-frame page publication on the next frame.";

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

  const auto first = shadow_manager->PublishForView(oxygen::ViewId { 25 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 25 });
  ASSERT_NE(first.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_FALSE(first_introspection->directional_virtual_metadata.empty());
  const auto layout = BuildVirtualFeedbackLayout(
    first_introspection->directional_virtual_metadata.front());
  ASSERT_GT(layout.pages_per_level, 1U);
  const auto requested_page = FindMappedPageInClip(
    layout, first_introspection->page_table_entries, 0U, 1U);
  ASSERT_TRUE(requested_page.has_value());
  const auto requested_resident_keys = RequestedResidentKeys(
    layout, std::span<const std::uint32_t> { &*requested_page, 1U });
  ASSERT_EQ(requested_resident_keys.size(), 1U);
  ASSERT_GT(first_introspection->page_table_entries.size(), *requested_page);
  EXPECT_NE(first_introspection->page_table_entries[*requested_page], 0U);
  shadow_manager->MarkVirtualRasterExecuted(oxygen::ViewId { 25 });

  shadow_manager->SubmitVirtualRequestFeedback(oxygen::ViewId { 25 },
    MakeVirtualRequestFeedback(
      layout, SequenceNumber { 1 }, { *requested_page }));

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 25 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 25 });
  ASSERT_NE(second.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_introspection, nullptr);
  ASSERT_FALSE(second_introspection->directional_virtual_metadata.empty());
  const auto second_layout = BuildVirtualFeedbackLayout(
    second_introspection->directional_virtual_metadata.front());
  const auto second_translated_page_index = LocalPageIndexForResidentKey(
    second_layout, requested_resident_keys.front());
  ASSERT_TRUE(second_translated_page_index.has_value());
  ASSERT_LT(*second_translated_page_index,
    second_introspection->page_table_entries.size());
  EXPECT_FALSE(second_introspection->used_request_feedback);
  EXPECT_FALSE(oxygen::renderer::VirtualShadowPageTableEntryHasCurrentLod(
    second_introspection->page_table_entries[*second_translated_page_index]));

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto third = shadow_manager->PublishForView(oxygen::ViewId { 25 },
    view_constants, manager, shadow_casters, {}, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* third_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 25 });
  ASSERT_NE(third.virtual_shadow_page_table_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(third_introspection, nullptr);
  ASSERT_FALSE(third_introspection->directional_virtual_metadata.empty());
  const auto third_layout = BuildVirtualFeedbackLayout(
    third_introspection->directional_virtual_metadata.front());
  const auto translated_page_index = LocalPageIndexForResidentKey(
    third_layout, requested_resident_keys.front());
  ASSERT_TRUE(translated_page_index.has_value());
  ASSERT_LT(
    *translated_page_index, third_introspection->page_table_entries.size());
  EXPECT_TRUE(third_introspection->used_request_feedback);
  EXPECT_TRUE(oxygen::renderer::VirtualShadowPageTableEntryHasAnyLod(
    third_introspection->page_table_entries[*translated_page_index]));
}

//! After the resolved-schedule bridge is retired, the live resolved page set
//! must stay authoritative across frames without any auxiliary CPU schedule.
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
  const auto* first_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 230 });
  const auto* first_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 230 });

  ASSERT_NE(first.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(first_introspection, nullptr);
  ASSERT_NE(first_plan, nullptr);
  ASSERT_FALSE(first_introspection->directional_virtual_metadata.empty());
  const auto first_resolved_pages = DeriveResolvedRasterPages(*first_introspection);
  ASSERT_FALSE(first_resolved_pages.empty());
  ASSERT_FALSE(first_plan->resolved_pages.empty());

  const auto layout = BuildVirtualFeedbackLayout(
    first_introspection->directional_virtual_metadata.front());
  const auto coarse_backbone_begin
    = layout.clip_level_count > 3U ? layout.clip_level_count - 3U : 0U;

  std::optional<std::uint64_t> requested_fine_resident_key {};
  std::optional<std::uint64_t> omitted_fine_resident_key {};
  std::optional<std::uint64_t> coarse_resident_key {};
  for (const auto& job : first_resolved_pages) {
    if (job.clip_level < coarse_backbone_begin) {
      if (!requested_fine_resident_key.has_value()) {
        requested_fine_resident_key = job.resident_key;
      } else if (job.resident_key != *requested_fine_resident_key) {
        omitted_fine_resident_key = job.resident_key;
      }
    } else if (!coarse_resident_key.has_value()) {
      coarse_resident_key = job.resident_key;
    }
  }

  ASSERT_TRUE(requested_fine_resident_key.has_value());
  ASSERT_TRUE(omitted_fine_resident_key.has_value());
  ASSERT_TRUE(coarse_resident_key.has_value());

  AdvanceRendererFrame(
    manager, *shadow_manager, view_constants, SequenceNumber { 2 }, Slot { 1 });

  const auto second = shadow_manager->PublishForView(oxygen::ViewId { 230 },
    view_constants, manager, shadow_casters, visible_receivers, &synthetic_sun,
    std::chrono::milliseconds(16));
  const auto* second_introspection
    = ResolveVirtualViewIntrospection(*shadow_manager, oxygen::ViewId { 230 });
  const auto* second_plan
    = ResolveVirtualRenderPlan(*shadow_manager, oxygen::ViewId { 230 });

  ASSERT_NE(second.shadow_instance_metadata_srv, kInvalidShaderVisibleIndex);
  ASSERT_NE(second_introspection, nullptr);
  ASSERT_NE(second_plan, nullptr);
  const auto second_resolved_pages = DeriveResolvedRasterPages(*second_introspection);
  ASSERT_FALSE(second_resolved_pages.empty());
  ASSERT_FALSE(second_introspection->directional_virtual_metadata.empty());

  const auto second_layout = BuildVirtualFeedbackLayout(
    second_introspection->directional_virtual_metadata.front());
  const auto second_requested_fine_page_index
    = LocalPageIndexForResidentKey(second_layout, *requested_fine_resident_key);
  const auto second_omitted_fine_page_index
    = LocalPageIndexForResidentKey(second_layout, *omitted_fine_resident_key);
  const auto second_coarse_page_index
    = LocalPageIndexForResidentKey(second_layout, *coarse_resident_key);
  ASSERT_TRUE(second_requested_fine_page_index.has_value());
  ASSERT_TRUE(second_omitted_fine_page_index.has_value());
  ASSERT_TRUE(second_coarse_page_index.has_value());

  ASSERT_EQ(second_plan->resolved_pages.size(), second_resolved_pages.size());
  EXPECT_EQ(second_introspection->pending_raster_page_count,
    second_resolved_pages.size());

  const auto contains_resident_key
    = [](const auto jobs, const std::uint64_t resident_key) {
        return std::any_of(
          jobs.begin(), jobs.end(), [resident_key](const auto& job) {
            return job.resident_key == resident_key;
          });
      };

  EXPECT_TRUE(contains_resident_key(
    second_plan->resolved_pages, *requested_fine_resident_key));
  EXPECT_TRUE(
    contains_resident_key(second_plan->resolved_pages, *coarse_resident_key));
  EXPECT_TRUE(contains_resident_key(
    second_plan->resolved_pages, *omitted_fine_resident_key));
  EXPECT_NE(
    second_introspection->page_table_entries[*second_requested_fine_page_index],
    0U);
  EXPECT_NE(
    second_introspection->page_table_entries[*second_omitted_fine_page_index],
    0U);
  EXPECT_NE(
    second_introspection->page_table_entries[*second_coarse_page_index], 0U);
}

} // namespace
