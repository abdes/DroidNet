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
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Vortex/Shadows/Internal/SharedShadowMap.h>
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
    struct LocalAcquisition {
      std::shared_ptr<ShadowMapOwner> owner;
      std::shared_ptr<ShadowMapOwner> previous;
      std::shared_ptr<ShadowMapOwner>* alias { nullptr };
      bool reused { false };
      bool in_place { false };
      bool committed { false };
      LocalAcquisition() = default;
      LocalAcquisition(const LocalAcquisition&) = delete;
      auto operator=(const LocalAcquisition&) -> LocalAcquisition& = delete;
      LocalAcquisition(LocalAcquisition&&) noexcept = default;
      OXGN_VRTX_API ~LocalAcquisition();
      OXGN_VRTX_API auto Commit() noexcept -> void;
    };
    OXGN_VRTX_API auto PrepareLocalFamily(
      std::span<const LocalShadowRequest* const> requests) -> void;
    [[nodiscard]] OXGN_VRTX_API auto AcquireLocalMap(
      ViewId view, const LocalShadowRequest& request) -> LocalAcquisition;
    OXGN_VRTX_API auto RetainDirectionalSurfaces(
      std::span<const LightSelectionIndex> selections) -> void;
    [[nodiscard]] OXGN_VRTX_API auto AcquireDirectionalSurface(ViewId view_id,
      LightSelectionIndex selection_index, std::uint32_t cascade_count,
      scene::ShadowResolutionHint resolution_hint) -> DirectionalAllocation;
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
      std::unordered_map<scene::NodeHandle, std::shared_ptr<ShadowMapOwner>>
        local_owners;
      std::unordered_map<LightSelectionIndex, SurfaceAllocation> directional;
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
    auto CreateLocalBacking(std::uint32_t resolution, bool cube)
      -> std::shared_ptr<SharedShadowBacking>;
    auto AcquirePhysicalSlot(std::uint32_t resolution, bool cube)
      -> std::shared_ptr<ShadowSlotCore>;
    auto EnsureSlotViews(const ShadowSlotCore& slot) -> void;
    auto PruneLocalChunks() -> void;
    auto IsRequested(const LocalShadowContentKey& key) const -> bool;
    std::shared_ptr<ShadowSlotPool> local_pool_ {
      std::make_shared<ShadowSlotPool>()
    };
    std::vector<std::shared_ptr<SharedShadowBacking>> local_chunks_;
    std::unordered_multimap<std::uint64_t, std::weak_ptr<ShadowMapOwner>>
      local_content_;
    std::vector<const LocalShadowContentKey*> requested_content_;
  };

} // namespace shadows::internal
} // namespace oxygen::vortex
