//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <glm/vec4.hpp>

#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Lighting/Internal/ForwardLightPublisher.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::lighting::internal {

namespace {

struct ClusterLightRange {
  std::uint32_t light_list_offset { 0U };
  std::uint32_t light_count { 0U };
};

} // namespace

ForwardLightPublisher::ForwardLightPublisher(Renderer& renderer)
  : renderer_(renderer)
{
}

auto ForwardLightPublisher::EnsurePublishResources() -> bool
{
  if (lighting_bindings_publisher_ != nullptr && local_light_buffer_ != nullptr
    && light_view_data_buffer_ != nullptr && grid_metadata_buffer_ != nullptr
    && grid_indirection_buffer_ != nullptr
    && directional_light_indices_buffer_ != nullptr) {
    return true;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  auto& staging = renderer_.GetStagingProvider();
  auto inline_transfers = observer_ptr { &renderer_.GetInlineTransfersCoordinator() };
  if (lighting_bindings_publisher_ == nullptr) {
    lighting_bindings_publisher_
      = std::make_unique<::oxygen::vortex::internal::PerViewStructuredPublisher<
        LightingFrameBindings>>(
        observer_ptr { gfx.get() }, staging, inline_transfers, "LightingFrameBindings");
  }
  if (local_light_buffer_ == nullptr) {
    local_light_buffer_ = std::make_unique<upload::TransientStructuredBuffer>(
      observer_ptr { gfx.get() }, staging,
      static_cast<std::uint32_t>(sizeof(ForwardLocalLightRecord)), inline_transfers,
      "LightingService.LocalLights");
  }
  if (light_view_data_buffer_ == nullptr) {
    light_view_data_buffer_ = std::make_unique<upload::TransientStructuredBuffer>(
      observer_ptr { gfx.get() }, staging, sizeof(std::uint32_t), inline_transfers,
      "LightingService.LightViewData");
  }
  if (grid_metadata_buffer_ == nullptr) {
    grid_metadata_buffer_ = std::make_unique<upload::TransientStructuredBuffer>(
      observer_ptr { gfx.get() }, staging,
      static_cast<std::uint32_t>(sizeof(LightGridMetadata)), inline_transfers,
      "LightingService.GridMetadata");
  }
  if (grid_indirection_buffer_ == nullptr) {
    grid_indirection_buffer_ = std::make_unique<upload::TransientStructuredBuffer>(
      observer_ptr { gfx.get() }, staging,
      static_cast<std::uint32_t>(sizeof(ClusterLightRange)), inline_transfers,
      "LightingService.GridIndirection");
  }
  if (directional_light_indices_buffer_ == nullptr) {
    directional_light_indices_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging, sizeof(std::uint32_t),
        inline_transfers, "LightingService.DirectionalIndices");
  }
  return true;
}

auto ForwardLightPublisher::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_sequence_ = sequence;
  current_slot_ = slot;
  published_views_.clear();
  if (!EnsurePublishResources()) {
    return;
  }

  lighting_bindings_publisher_->OnFrameStart(sequence, slot);
  local_light_buffer_->OnFrameStart(sequence, slot);
  light_view_data_buffer_->OnFrameStart(sequence, slot);
  grid_metadata_buffer_->OnFrameStart(sequence, slot);
  grid_indirection_buffer_->OnFrameStart(sequence, slot);
  directional_light_indices_buffer_->OnFrameStart(sequence, slot);
}

auto ForwardLightPublisher::Publish(const BuiltLightGridFrame& built_frame) -> void
{
  published_views_.clear();
  if (!EnsurePublishResources()) {
    return;
  }

  auto local_light_buffer_srv = ShaderVisibleIndex { kInvalidShaderVisibleIndex };
  if (!built_frame.local_light_records.empty()) {
    if (auto allocation = local_light_buffer_->Allocate(
          static_cast<std::uint32_t>(built_frame.local_light_records.size()));
      allocation && allocation->TryWriteRange(
        std::span(built_frame.local_light_records))) {
      local_light_buffer_srv = allocation->srv;
    }
  }

  for (const auto& view : built_frame.per_view) {
    auto bindings = view.bindings;
    bindings.local_light_buffer_srv = local_light_buffer_srv;

    if (auto metadata_alloc = grid_metadata_buffer_->Allocate(1U);
      metadata_alloc && metadata_alloc->TryWriteObject(view.metadata)) {
      bindings.grid_metadata_buffer_srv = metadata_alloc->srv;
    }

    const auto light_list_size = (std::max)(bindings.local_light_count, 1U);
    auto light_indices = std::vector<std::uint32_t>(light_list_size, 0U);
    for (std::uint32_t i = 0; i < bindings.local_light_count; ++i) {
      light_indices[i] = i;
    }
    if (auto light_view_alloc = light_view_data_buffer_->Allocate(light_list_size);
      light_view_alloc
      && light_view_alloc->TryWriteRange(std::span(light_indices))) {
      bindings.light_view_data_srv = light_view_alloc->srv;
    }

    const auto cluster_count = (std::max)(view.metadata.num_grid_cells, 1U);
    auto cluster_ranges = std::vector<ClusterLightRange>(cluster_count);
    for (auto& entry : cluster_ranges) {
      entry.light_list_offset = 0U;
      entry.light_count = bindings.local_light_count;
    }
    if (auto cluster_alloc = grid_indirection_buffer_->Allocate(cluster_count);
      cluster_alloc
      && cluster_alloc->TryWriteRange(std::span(cluster_ranges))) {
      bindings.grid_indirection_srv = cluster_alloc->srv;
    }

    if (!built_frame.directional_light_indices.empty()) {
      if (auto directional_alloc = directional_light_indices_buffer_->Allocate(
            static_cast<std::uint32_t>(built_frame.directional_light_indices.size()));
        directional_alloc
        && directional_alloc->TryWriteRange(
          std::span(built_frame.directional_light_indices))) {
        bindings.directional_light_indices_srv = directional_alloc->srv;
      }
    }

    const auto slot
      = lighting_bindings_publisher_->Publish(view.view_id, bindings);
    published_views_.insert_or_assign(
      view.view_id, PublishedLightingView { .slot = slot, .bindings = bindings });
  }
}

auto ForwardLightPublisher::InspectBindings(const ViewId view_id) const
  -> const LightingFrameBindings*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? &it->second.bindings : nullptr;
}

auto ForwardLightPublisher::ResolveBindingSlot(const ViewId view_id) const
  -> ShaderVisibleIndex
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? it->second.slot
                                      : ShaderVisibleIndex { kInvalidShaderVisibleIndex };
}

} // namespace oxygen::vortex::lighting::internal
