//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <glm/vec2.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class Renderer;

namespace shadows::internal {

class ConventionalShadowTargetAllocator {
public:
  struct DirectionalAllocation {
    std::shared_ptr<graphics::Texture> surface {};
    ShaderVisibleIndex surface_srv { kInvalidShaderVisibleIndex };
    glm::uvec2 resolution { 0U, 0U };
    std::uint32_t cascade_count { 0U };
  };

  OXGN_VRTX_API explicit ConventionalShadowTargetAllocator(Renderer& renderer);
  OXGN_VRTX_API ~ConventionalShadowTargetAllocator();

  ConventionalShadowTargetAllocator(
    const ConventionalShadowTargetAllocator&) = delete;
  auto operator=(const ConventionalShadowTargetAllocator&)
    -> ConventionalShadowTargetAllocator& = delete;
  ConventionalShadowTargetAllocator(ConventionalShadowTargetAllocator&&)
    = delete;
  auto operator=(ConventionalShadowTargetAllocator&&)
    -> ConventionalShadowTargetAllocator& = delete;

  OXGN_VRTX_API auto OnFrameStart() -> void;
  [[nodiscard]] OXGN_VRTX_API auto AcquireDirectionalSurface(
    std::uint32_t cascade_count) -> DirectionalAllocation;

private:
  auto EnsureDirectionalSurface(std::uint32_t cascade_count) -> void;
  auto RegisterDirectionalSurfaceSrv() -> ShaderVisibleIndex;

  Renderer& renderer_;
  std::shared_ptr<graphics::Texture> directional_surface_ {};
  ShaderVisibleIndex directional_surface_srv_ { kInvalidShaderVisibleIndex };
  glm::uvec2 directional_resolution_ { 0U, 0U };
  std::uint32_t directional_array_size_ { 0U };
};

} // namespace shadows::internal
} // namespace oxygen::vortex
