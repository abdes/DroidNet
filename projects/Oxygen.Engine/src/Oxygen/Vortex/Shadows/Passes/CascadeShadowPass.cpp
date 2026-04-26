//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Shadows/Passes/CascadeShadowPass.h>

#include <algorithm>
#include <vector>

#include <glm/vec4.hpp>

#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Shadows/Internal/CascadeShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowCasterCulling.h>
#include <Oxygen/Vortex/Shadows/Internal/SpotShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.h>

namespace oxygen::vortex::shadows {

CascadeShadowPass::CascadeShadowPass(Renderer& renderer)
  : renderer_(renderer)
  , cascade_setup_(std::make_unique<internal::CascadeShadowSetup>())
  , spot_setup_(std::make_unique<internal::SpotShadowSetup>())
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
  const auto allocation = allocator_->AcquireDirectionalSurface(
    directional_light.cascade_count, directional_light.shadow_resolution_hint);
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

auto CascadeShadowPass::RenderSpotView(
  const PreparedViewShadowInput& view_input,
  const std::span<const FrameLocalLightSelection> local_lights)
  -> ViewSpotShadowPassState
{
  auto state = ViewSpotShadowPassState {};
  auto shadowed_spot_count = 0U;
  auto resolution_hint = 0U;
  for (const auto& light : local_lights) {
    if (light.kind == LocalLightKind::kSpot
      && (light.flags & kLocalLightFlagCastsShadows) != 0U) {
      ++shadowed_spot_count;
      resolution_hint = (std::max)(resolution_hint, light.shadow_resolution_hint);
    }
  }
  if (shadowed_spot_count == 0U) {
    return state;
  }

  const auto allocation
    = allocator_->AcquireSpotSurface(shadowed_spot_count, resolution_hint);
  state.bindings = spot_setup_->BuildSpotFrameBindings(
    view_input, local_lights, allocation);
  state.shadow_surface = allocation.surface;

  if (view_input.prepared_scene != nullptr) {
    shadow_caster_culling_->BuildDrawCommands(*view_input.prepared_scene);
    state.shadow_caster_draw_count = static_cast<std::uint32_t>(
      shadow_caster_culling_->GetDrawCommands().size());
  }

  auto depth_slices = std::vector<ShadowDepthPass::DepthSlice> {};
  depth_slices.reserve(state.bindings.spot_shadow_count);
  for (std::uint32_t spot_index = 0U;
       spot_index < state.bindings.spot_shadow_count; ++spot_index) {
    const auto& spot = state.bindings.spot_shadows[spot_index];
    depth_slices.push_back(ShadowDepthPass::DepthSlice {
      .light_view_projection = spot.light_view_projection,
      .shadow_bias_parameters = glm::vec4(spot.direction_and_bias.w,
        spot.sampling_metadata1.z, 1.0F, 0.0F),
      .light_direction_to_source = glm::vec4(
        glm::vec3(spot.direction_and_bias), 0.0F),
      .light_position_and_inv_range = spot.position_and_inv_range,
      .target_slice = spot_index,
    });
  }

  const auto render_state = allocation.surface != nullptr
    ? depth_pass_->RecordSlices(view_input, allocation.surface,
        std::span(depth_slices), shadow_caster_culling_->GetDrawCommands())
    : ShadowDepthPass::RenderState {};
  state.rendered_shadow_count = render_state.rendered_cascade_count;
  state.rendered_draw_count = render_state.rendered_draw_count;
  return state;
}

} // namespace oxygen::vortex::shadows
