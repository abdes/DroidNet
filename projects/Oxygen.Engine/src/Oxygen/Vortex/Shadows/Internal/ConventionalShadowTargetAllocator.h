//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

#include <glm/vec2.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class Renderer;

namespace shadows::internal {

  class ConventionalShadowTargetAllocator {
  public:
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

    OXGN_VRTX_API auto OnFrameStart(frame::SequenceNumber sequence) -> void;
    OXGN_VRTX_API auto RetainDirectionalSurfaces(
      std::span<const LightSelectionIndex> selections) -> void;
    [[nodiscard]] OXGN_VRTX_API auto AcquireDirectionalSurface(ViewId view_id,
      LightSelectionIndex selection_index, std::uint32_t cascade_count,
      scene::ShadowResolutionHint resolution_hint) -> DirectionalAllocation;
    [[nodiscard]] OXGN_VRTX_API auto AcquireSpotSurface(ViewId view_id,
      std::uint32_t shadow_count, scene::ShadowResolutionHint resolution_hint)
      -> SpotAllocation;
    [[nodiscard]] OXGN_VRTX_API auto AcquirePointSurface(ViewId view_id,
      std::uint32_t shadow_count, scene::ShadowResolutionHint resolution_hint)
      -> PointAllocation;

  private:
    struct SurfaceAllocation {
      std::shared_ptr<graphics::Texture> surface;
      ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
      glm::uvec2 resolution { 0U };
      std::uint32_t layers { 0U };
    };
    struct ViewAllocations {
      std::unordered_map<LightSelectionIndex, SurfaceAllocation> directional;
      SurfaceAllocation spot;
      SurfaceAllocation point;
      frame::SequenceNumber last_used { 0U };
    };
    auto AcquireSurface(SurfaceAllocation& current, std::uint32_t layers,
      std::uint32_t resolution, bool cube, const char* name) -> bool;
    auto Retire(SurfaceAllocation& allocation) -> void;
    auto Retire(ViewAllocations& allocations) -> void;
    auto Touch(ViewId view_id) -> ViewAllocations&;
    Renderer& renderer_;
    frame::SequenceNumber current_sequence_ { 0U };
    std::unordered_map<ViewId, ViewAllocations> views_;
  };

} // namespace shadows::internal
} // namespace oxygen::vortex
