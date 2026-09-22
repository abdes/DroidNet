//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Lighting/Internal/ForwardLightPublisher.h>
#include <Oxygen/Vortex/Lighting/Internal/LightGridBuilder.h>
#include <Oxygen/Vortex/Lighting/Types/ClusterLightRange.h>
#include <Oxygen/Vortex/Lighting/Types/DirectionalLightForwardData.h>
#include <Oxygen/Vortex/Lighting/Types/ForwardLocalLightRecord.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridBuildStatus.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridMetadata.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Shadows/Types/LightShadowReference.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::lighting::internal {

ForwardLightPublisher::ForwardLightPublisher(Renderer& renderer)
  : renderer_(renderer)
{
}

auto ForwardLightPublisher::EnsurePublishResources() -> bool
{
  if (lighting_bindings_publisher_ != nullptr && local_light_buffer_ != nullptr
    && grid_metadata_buffer_ != nullptr && grid_indirection_buffer_ != nullptr
    && directional_light_buffer_ != nullptr && build_status_buffer_ != nullptr
    && local_shadow_map_buffer_ != nullptr
    && directional_shadow_map_buffer_ != nullptr) {
    return true;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  auto& staging = renderer_.GetStagingProvider();
  auto inline_transfers
    = observer_ptr { &renderer_.GetInlineTransfersCoordinator() };
  if (lighting_bindings_publisher_ == nullptr) {
    lighting_bindings_publisher_
      = std::make_unique<::oxygen::vortex::internal::PerViewStructuredPublisher<
        LightingFrameBindings>>(observer_ptr { gfx.get() }, staging,
        inline_transfers, "LightingFrameBindings");
  }
  if (local_light_buffer_ == nullptr) {
    local_light_buffer_ = std::make_unique<upload::TransientStructuredBuffer>(
      observer_ptr { gfx.get() }, staging,
      static_cast<std::uint32_t>(sizeof(ForwardLocalLightRecord)),
      inline_transfers, "LightingService.LocalLights");
  }
  if (grid_metadata_buffer_ == nullptr) {
    grid_metadata_buffer_ = std::make_unique<upload::TransientStructuredBuffer>(
      observer_ptr { gfx.get() }, staging,
      static_cast<std::uint32_t>(sizeof(LightGridMetadata)), inline_transfers,
      "LightingService.GridMetadata");
  }
  if (grid_indirection_buffer_ == nullptr) {
    grid_indirection_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging,
        static_cast<std::uint32_t>(sizeof(ClusterLightRange)), inline_transfers,
        "LightingService.GridIndirection");
  }
  if (directional_light_buffer_ == nullptr) {
    directional_light_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging,
        static_cast<std::uint32_t>(sizeof(DirectionalLightForwardData)),
        inline_transfers, "LightingService.DirectionalLights");
  }
  if (build_status_buffer_ == nullptr) {
    build_status_buffer_ = std::make_unique<upload::TransientStructuredBuffer>(
      observer_ptr { gfx.get() }, staging,
      static_cast<std::uint32_t>(sizeof(LightGridBuildStatus)),
      inline_transfers, "LightingService.BuildStatus");
  }
  if (local_shadow_map_buffer_ == nullptr) {
    local_shadow_map_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging,
        static_cast<std::uint32_t>(sizeof(LightShadowReference)),
        inline_transfers, "LightingService.LocalShadowMap");
  }
  if (directional_shadow_map_buffer_ == nullptr) {
    directional_shadow_map_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, staging,
        static_cast<std::uint32_t>(sizeof(LightShadowReference)),
        inline_transfers, "LightingService.DirectionalShadowMap");
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
  grid_metadata_buffer_->OnFrameStart(sequence, slot);
  grid_indirection_buffer_->OnFrameStart(sequence, slot);
  directional_light_buffer_->OnFrameStart(sequence, slot);
  build_status_buffer_->OnFrameStart(sequence, slot);
  local_shadow_map_buffer_->OnFrameStart(sequence, slot);
  directional_shadow_map_buffer_->OnFrameStart(sequence, slot);
}

auto ForwardLightPublisher::Publish(const BuiltLightGridFrame& built_frame)
  -> std::expected<void, LightingPreparationFailure>
{
  published_views_.clear();
  if (built_frame.per_view.empty()) {
    return {};
  }
  auto failure = LightingPreparationFailure {
    .error = LightingPreparationError::kAllocationFailed,
  };
  if (!EnsurePublishResources()) {
    return std::unexpected(failure);
  }
  const auto write = []<typename T, std::size_t Extent>(
                       upload::TransientStructuredBuffer& buffer,
                       const std::span<T, Extent> values,
                       ShaderVisibleIndex& descriptor) -> bool {
    if (values.empty()) {
      descriptor = kInvalidShaderVisibleIndex;
      return true;
    }
    auto allocation
      = buffer.Allocate(static_cast<std::uint32_t>(values.size()));
    if (!allocation || !allocation->TryWriteRange(values)) {
      return false;
    }
    descriptor = allocation->srv;
    return true;
  };
  auto local_srv = kInvalidShaderVisibleIndex;
  auto directional_srv = kInvalidShaderVisibleIndex;
  if (!write(*local_light_buffer_, std::span(built_frame.evaluation.local),
        local_srv)
    || !write(*directional_light_buffer_,
      std::span(built_frame.evaluation.directional), directional_srv)) {
    return std::unexpected(failure);
  }
  auto local_shadow_map = std::vector<LightShadowReference> {};
  auto directional_shadow_map = std::vector<LightShadowReference> {};
  std::uint32_t point_index = 0U;
  std::uint32_t spot_index = 0U;
  for (const auto& light : built_frame.evaluation.local) {
    auto reference
      = LightShadowReference { .selection_index = light.selection_index };
    if ((light.flags & kLocalLightFlagCastsShadows) != 0U) {
      const auto point
        = light.kind == static_cast<std::uint32_t>(LocalLightKind::kPoint);
      auto& index = point ? point_index : spot_index;
      reference.projection_kind = point ? kShadowProjectionLocalCube
                                        : kShadowProjectionLocalProjected2D;
      reference.record_index = ShadowRecordIndex { index++ };
      reference.coverage_state = light.range_m == 0.0F
        ? kShadowCoverageNoInfluence
        : kShadowCoverageComplete;
    }
    local_shadow_map.push_back(reference);
  }
  for (const auto& light : built_frame.evaluation.directional) {
    auto reference
      = LightShadowReference { .selection_index = light.selection_index };
    if ((light.flags & kDirectionalLightShadowFlagCastsShadows) != 0U) {
      reference.projection_kind = kShadowProjectionCascaded2D;
      reference.record_index
        = ShadowRecordIndex { light.selection_index.get() };
      reference.coverage_state = kShadowCoverageComplete;
    }
    directional_shadow_map.push_back(reference);
  }
  auto local_shadow_srv = kInvalidShaderVisibleIndex;
  auto directional_shadow_srv = kInvalidShaderVisibleIndex;
  if (!write(*local_shadow_map_buffer_, std::span(local_shadow_map),
        local_shadow_srv)
    || !write(*directional_shadow_map_buffer_,
      std::span(directional_shadow_map), directional_shadow_srv)) {
    return std::unexpected(failure);
  }
  auto candidate = std::unordered_map<ViewId, PublishedLightingView> {};
  for (const auto& view : built_frame.per_view) {
    failure.view_id = view.view_id;
    auto bindings = view.bindings;
    bindings.local_records_srv = local_srv;
    bindings.directional_records_srv = directional_srv;
    bindings.local_shadow_map_srv = local_shadow_srv;
    bindings.directional_shadow_map_srv = directional_shadow_srv;
    auto status = LightGridBuildStatus {
      .state = kLightGridBuildValid,
      .selection_revision = bindings.selection_revision,
    };
    if (bindings.local_count != 0U) {
      const auto metadata = std::array { view.metadata };
      auto ranges = std::vector<ClusterLightRange>(bindings.cluster_count,
        ClusterLightRange {
          .offset = kCompleteLightListOffset,
          .count = bindings.local_count,
        });
      if (!write(*grid_metadata_buffer_, std::span(metadata),
            bindings.grid_metadata_srv)
        || !write(*grid_indirection_buffer_, std::span(ranges),
          bindings.cluster_ranges_srv)) {
        return std::unexpected(failure);
      }
      status.fallback_cell_count = bindings.cluster_count;
      const auto required = static_cast<std::uint64_t>(bindings.cluster_count)
        * bindings.local_count;
      status.required_index_count = {
        static_cast<std::uint32_t>(required),
        static_cast<std::uint32_t>(required
          >> static_cast<unsigned>(std::numeric_limits<std::uint32_t>::digits)),
      };
    } else {
      bindings.cluster_count = 0U;
    }
    const auto statuses = std::array { status };
    if (!write(*build_status_buffer_, std::span(statuses),
          bindings.build_status_srv)) {
      return std::unexpected(failure);
    }
    bindings.publication_state
      = bindings.local_count == 0U && bindings.directional_count == 0U
      ? kLightingPublicationEmpty
      : kLightingPublicationRecorded;
    const auto slot
      = lighting_bindings_publisher_->Publish(view.view_id, bindings);
    if (slot == kInvalidShaderVisibleIndex) {
      return std::unexpected(failure);
    }
    candidate.insert_or_assign(view.view_id,
      PublishedLightingView { .slot = slot, .bindings = bindings });
  }
  published_views_ = std::move(candidate);
  return {};
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
  return it != published_views_.end()
    ? it->second.slot
    : ShaderVisibleIndex { kInvalidShaderVisibleIndex };
}

} // namespace oxygen::vortex::lighting::internal
