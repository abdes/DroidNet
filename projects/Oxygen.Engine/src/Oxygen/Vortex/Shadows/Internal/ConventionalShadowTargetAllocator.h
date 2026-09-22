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

    OXGN_VRTX_API auto OnFrameStart() -> void;
    OXGN_VRTX_API auto RetainDirectionalSurfaces(
      std::span<const LightSelectionIndex> selections) -> void;
    [[nodiscard]] OXGN_VRTX_API auto AcquireDirectionalSurface(
      LightSelectionIndex selection_index, std::uint32_t cascade_count,
      scene::ShadowResolutionHint resolution_hint) -> DirectionalAllocation;
    [[nodiscard]] OXGN_VRTX_API auto AcquireSpotSurface(
      std::uint32_t shadow_count, scene::ShadowResolutionHint resolution_hint)
      -> SpotAllocation;
    [[nodiscard]] OXGN_VRTX_API auto AcquirePointSurface(
      std::uint32_t shadow_count, scene::ShadowResolutionHint resolution_hint)
      -> PointAllocation;

  private:
    auto EnsureDirectionalSurface(LightSelectionIndex selection_index,
      std::uint32_t cascade_count, scene::ShadowResolutionHint resolution_hint)
      -> void;
    auto RegisterDirectionalSurfaceSrv(
      const std::shared_ptr<graphics::Texture>& surface) -> ShaderVisibleIndex;
    auto EnsureSpotSurface(std::uint32_t shadow_count,
      scene::ShadowResolutionHint resolution_hint) -> void;
    auto RegisterSpotSurfaceSrv() -> ShaderVisibleIndex;
    auto EnsurePointSurface(std::uint32_t shadow_count,
      scene::ShadowResolutionHint resolution_hint) -> void;
    auto RegisterPointSurfaceSrv() -> ShaderVisibleIndex;

    Renderer& renderer_;
    std::unordered_map<LightSelectionIndex, DirectionalAllocation>
      directional_allocations_;
    std::shared_ptr<graphics::Texture> spot_surface_;
    ShaderVisibleIndex spot_surface_srv_ { kInvalidShaderVisibleIndex };
    glm::uvec2 spot_resolution_ { 0U, 0U };
    std::uint32_t spot_array_size_ { 0U };
    std::shared_ptr<graphics::Texture> point_surface_;
    ShaderVisibleIndex point_surface_srv_ { kInvalidShaderVisibleIndex };
    glm::uvec2 point_resolution_ { 0U, 0U };
    std::uint32_t point_shadow_count_ { 0U };
  };

} // namespace shadows::internal
} // namespace oxygen::vortex
