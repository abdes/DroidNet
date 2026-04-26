//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Shadows/Types/DirectionalShadowFrameData.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class Renderer;

namespace shadows {

class ShadowDepthPass;

namespace internal {
class CascadeShadowSetup;
class ConventionalShadowTargetAllocator;
class ShadowCasterCulling;
class SpotShadowSetup;
} // namespace internal

class CascadeShadowPass {
public:
  struct ViewShadowPassState {
    DirectionalShadowFrameData frame_data {};
    std::shared_ptr<graphics::Texture> shadow_surface {};
    std::uint32_t shadow_caster_draw_count { 0U };
    std::uint32_t rendered_cascade_count { 0U };
    std::uint32_t rendered_draw_count { 0U };
  };

  struct ViewSpotShadowPassState {
    ShadowFrameBindings bindings {};
    std::shared_ptr<graphics::Texture> shadow_surface {};
    std::uint32_t shadow_caster_draw_count { 0U };
    std::uint32_t rendered_shadow_count { 0U };
    std::uint32_t rendered_draw_count { 0U };
  };

  OXGN_VRTX_API explicit CascadeShadowPass(Renderer& renderer);
  OXGN_VRTX_API ~CascadeShadowPass();

  CascadeShadowPass(const CascadeShadowPass&) = delete;
  auto operator=(const CascadeShadowPass&) -> CascadeShadowPass& = delete;
  CascadeShadowPass(CascadeShadowPass&&) = delete;
  auto operator=(CascadeShadowPass&&) -> CascadeShadowPass& = delete;

  OXGN_VRTX_API auto OnFrameStart(
    frame::SequenceNumber sequence, frame::Slot slot) -> void;
  [[nodiscard]] OXGN_VRTX_API auto RenderDirectionalView(
    const PreparedViewShadowInput& view_input,
    const FrameDirectionalLightSelection& directional_light) -> ViewShadowPassState;
  [[nodiscard]] OXGN_VRTX_API auto RenderSpotView(
    const PreparedViewShadowInput& view_input,
    std::span<const FrameLocalLightSelection> local_lights)
    -> ViewSpotShadowPassState;

private:
  Renderer& renderer_;
  std::unique_ptr<internal::CascadeShadowSetup> cascade_setup_;
  std::unique_ptr<internal::SpotShadowSetup> spot_setup_;
  std::unique_ptr<internal::ConventionalShadowTargetAllocator> allocator_;
  std::unique_ptr<internal::ShadowCasterCulling> shadow_caster_culling_;
  std::unique_ptr<ShadowDepthPass> depth_pass_;
};

} // namespace shadows
} // namespace oxygen::vortex
