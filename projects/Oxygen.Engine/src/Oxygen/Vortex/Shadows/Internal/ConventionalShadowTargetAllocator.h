//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <unordered_map>

#include <glm/vec2.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Nexus/FrameDrivenIndexReuse.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class Renderer;

namespace shadows::internal {

  class ConventionalShadowTargetAllocator {
  public:
    struct LocalSlot {
      nexus::VersionedIndex<ShadowSlotIndex> handle;
      std::uint32_t resolution { 0U };
      std::uint32_t chunk { 0U };
      std::uint32_t offset { 0U };
      bool cube { false };
    };
    struct LocalSelection {
      LightSelectionIndex selection_index;
      LocalSlot slot;
    };
    struct DirectionalAllocation {
      std::shared_ptr<graphics::Texture> surface;
      ShaderVisibleIndex surface_srv { kInvalidShaderVisibleIndex };
      glm::uvec2 resolution { 0U, 0U };
      std::uint32_t cascade_count { 0U };
    };

    struct SpotAllocation {
      std::shared_ptr<graphics::Texture> surface;
      ShaderVisibleIndex surface_srv { kInvalidShaderVisibleIndex };
      glm::uvec2 resolution { 0U, 0U };
      std::uint32_t shadow_count { 0U };
    };

    struct PointAllocation {
      std::shared_ptr<graphics::Texture> surface;
      ShaderVisibleIndex surface_srv { kInvalidShaderVisibleIndex };
      glm::uvec2 resolution { 0U, 0U };
      std::uint32_t shadow_count { 0U };
    };

    OXGN_VRTX_API explicit ConventionalShadowTargetAllocator(
      Renderer& renderer);
    OXGN_VRTX_API ~ConventionalShadowTargetAllocator();

    ConventionalShadowTargetAllocator(const ConventionalShadowTargetAllocator&)
      = delete;
    auto operator=(const ConventionalShadowTargetAllocator&)
      -> ConventionalShadowTargetAllocator& = delete;
    ConventionalShadowTargetAllocator(ConventionalShadowTargetAllocator&&)
      = delete;
    auto operator=(ConventionalShadowTargetAllocator&&)
      -> ConventionalShadowTargetAllocator& = delete;

    OXGN_VRTX_API auto OnFrameStart(
      frame::SequenceNumber sequence, frame::Slot slot) -> void;
    OXGN_VRTX_API auto RetainLocalSources(ViewId view_id,
      std::uint64_t scene_generation,
      std::span<const FrameLocalLightSelection> lights) -> void;
    [[nodiscard]] OXGN_VRTX_API auto AcquireLocalSlot(ViewId view_id,
      scene::NodeHandle source, std::uint32_t resolution, bool cube)
      -> LocalSlot;
    OXGN_VRTX_API auto RetainDirectionalSurfaces(
      std::span<const LightSelectionIndex> selections) -> void;
    [[nodiscard]] OXGN_VRTX_API auto AcquireDirectionalSurface(ViewId view_id,
      LightSelectionIndex selection_index, std::uint32_t cascade_count,
      scene::ShadowResolutionHint resolution_hint) -> DirectionalAllocation;
    [[nodiscard]] OXGN_VRTX_API auto AcquireSpotSurface(ViewId view_id,
      std::uint32_t shadow_count, std::uint32_t resolution, std::uint32_t chunk)
      -> SpotAllocation;
    [[nodiscard]] OXGN_VRTX_API auto AcquirePointSurface(ViewId view_id,
      std::uint32_t shadow_count, std::uint32_t resolution, std::uint32_t chunk)
      -> PointAllocation;
    [[nodiscard]] OXGN_VRTX_API auto ResolveLocalResolution(
      scene::ShadowResolutionHint hint) const -> std::uint32_t;
    [[nodiscard]] OXGN_VRTX_API static auto LocalChunkCapacity(
      std::uint32_t resolution, bool cube) -> std::uint32_t;

  private:
    struct SurfaceAllocation {
      std::shared_ptr<graphics::Texture> surface;
      ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
      glm::uvec2 resolution { 0U };
      std::uint32_t layers { 0U };
      frame::SequenceNumber last_used { 0U };
    };
    struct ViewAllocations {
      std::uint64_t scene_generation { 0U };
      std::unordered_map<scene::NodeHandle, LocalSlot> local_owners;
      std::unordered_map<LightSelectionIndex, SurfaceAllocation> directional;
      std::map<std::pair<std::uint32_t, std::uint32_t>, SurfaceAllocation> spot;
      std::map<std::pair<std::uint32_t, std::uint32_t>, SurfaceAllocation>
        point;
      frame::SequenceNumber last_used { 0U };
    };
    auto AcquireSurface(SurfaceAllocation& current, std::uint32_t layers,
      std::uint32_t resolution, bool cube, const char* name,
      std::uint32_t capacity_layers = 0U) -> bool;
    auto Retire(SurfaceAllocation& allocation) -> void;
    auto Retire(ViewAllocations& allocations) -> void;
    auto Touch(ViewId view_id) -> ViewAllocations&;
    Renderer& renderer_;
    frame::SequenceNumber current_sequence_ { 0U };
    std::unordered_map<ViewId, ViewAllocations> views_;
    struct SlotLocation {
      ViewId view_id { kInvalidViewId };
      std::uint32_t resolution { 0U };
      std::uint32_t ordinal { 0U };
      bool cube { false };
      bool occupied { false };
    };
    std::vector<SlotLocation> slots_;
    std::vector<ShadowSlotIndex> free_slots_;
    graphics::detail::DeferredReclaimer slot_reclaimer_;
    nexus::FrameDrivenIndexReuse<ShadowSlotIndex> slot_reuse_;
  };

} // namespace shadows::internal
} // namespace oxygen::vortex
