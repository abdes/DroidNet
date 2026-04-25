//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Shadows/Passes/CascadeShadowPass.h>

#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Shadows/Internal/CascadeShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowCasterCulling.h>
#include <Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.h>

namespace oxygen::vortex::shadows {

CascadeShadowPass::CascadeShadowPass(Renderer& renderer)
  : renderer_(renderer)
  , cascade_setup_(std::make_unique<internal::CascadeShadowSetup>())
  , allocator_(
      std::make_unique<internal::ConventionalShadowTargetAllocator>(renderer))
  , shadow_caster_culling_(std::make_unique<internal::ShadowCasterCulling>())
  , depth_pass_(std::make_unique<ShadowDepthPass>(renderer))
{
}

CascadeShadowPass::~CascadeShadowPass() = default;

auto CascadeShadowPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  allocator_->OnFrameStart();
  depth_pass_->OnFrameStart(sequence, slot);
}

auto CascadeShadowPass::RenderDirectionalView(
  const PreparedViewShadowInput& view_input,
  const FrameDirectionalLightSelection& directional_light) -> ViewShadowPassState
{
  auto state = ViewShadowPassState {};
  const auto allocation
    = allocator_->AcquireDirectionalSurface(directional_light.cascade_count);
  state.frame_data = cascade_setup_->BuildDirectionalFrameData(
    view_input, directional_light, allocation);
  state.shadow_surface = allocation.surface;

  if (view_input.prepared_scene != nullptr) {
    shadow_caster_culling_->BuildDrawCommands(*view_input.prepared_scene);
    state.shadow_caster_draw_count = static_cast<std::uint32_t>(
      shadow_caster_culling_->GetDrawCommands().size());
  }

  const auto render_state = allocation.surface != nullptr
    ? depth_pass_->Record(view_input, allocation.surface, state.frame_data,
        shadow_caster_culling_->GetDrawCommands())
    : ShadowDepthPass::RenderState {};
  state.rendered_cascade_count = render_state.rendered_cascade_count;
  state.rendered_draw_count = render_state.rendered_draw_count;
  return state;
}

} // namespace oxygen::vortex::shadows
