//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>

#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>

namespace oxygen::vortex {

namespace {

  auto ClampExtent(glm::uvec2 extent) -> glm::uvec2
  {
    extent.x = std::max(extent.x, 1U);
    extent.y = std::max(extent.y, 1U);
    return extent;
  }

  auto ResolveDefaultShadingMode(const CapabilitySet capabilities)
    -> ShadingMode
  {
    return HasAllCapabilities(
             capabilities, RendererCapabilityFamily::kDeferredShading)
      ? ShadingMode::kDeferred
      : ShadingMode::kForward;
  }

} // namespace

auto SceneRenderBuilder::Build(Renderer& renderer, Graphics& gfx,
  const CapabilitySet capabilities, glm::uvec2 initial_viewport_extent)
  -> std::unique_ptr<SceneRenderer>
{
  const auto clamped_extent = ClampExtent(initial_viewport_extent);
  auto config = SceneTexturesConfig {
    .extent = clamped_extent,
    .enable_velocity = true,
    .enable_custom_depth = true,
    .gbuffer_count = static_cast<std::uint32_t>(GBufferIndex::kActiveCount),
    .msaa_sample_count = 1,
  };

  return std::make_unique<SceneRenderer>(
    renderer, gfx, config, ResolveDefaultShadingMode(capabilities));
}

} // namespace oxygen::vortex
