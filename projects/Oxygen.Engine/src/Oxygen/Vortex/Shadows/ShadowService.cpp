//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Shadows/ShadowService.h>

#include <algorithm>
#include <memory>
#include <span>

#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Shadows/Passes/CascadeShadowPass.h>

namespace oxygen::vortex {

ShadowService::ShadowService(Renderer& renderer)
  : renderer_(renderer)
  , cascade_shadow_pass_(std::make_unique<shadows::CascadeShadowPass>(renderer))
{
}

ShadowService::~ShadowService() = default;

auto ShadowService::EnsurePublishResources() -> bool
{
  if (bindings_publisher_ != nullptr) {
    return true;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  bindings_publisher_
    = std::make_unique<internal::PerViewStructuredPublisher<ShadowFrameBindings>>(
      observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
      observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
      "ShadowFrameBindings");
  return true;
}

auto ShadowService::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_sequence_ = sequence;
  current_slot_ = slot;
  published_views_.clear();
  last_render_state_ = {
    .frame_sequence = sequence,
    .frame_slot = slot,
  };
  cascade_shadow_pass_->OnFrameStart(sequence, slot);
  if (EnsurePublishResources()) {
    bindings_publisher_->OnFrameStart(sequence, slot);
  }
}

auto ShadowService::PublishShadowBindings(
  const ViewId view_id, const ShadowFrameBindings& bindings) -> ShaderVisibleIndex
{
  if (!EnsurePublishResources()) {
    return kInvalidShaderVisibleIndex;
  }
  return bindings_publisher_->Publish(view_id, bindings);
}

auto ShadowService::RenderShadowDepths(const FrameShadowInputs& inputs) -> void
{
  last_render_state_.published_view_count = 0U;
  last_render_state_.directional_view_count = 0U;
  last_render_state_.spot_view_count = 0U;
  last_render_state_.point_view_count = 0U;
  last_render_state_.rendered_cascade_count = 0U;
  last_render_state_.rendered_spot_shadow_count = 0U;
  last_render_state_.rendered_point_shadow_count = 0U;
  last_render_state_.rendered_draw_count = 0U;
  last_render_state_.shadow_caster_draw_count = 0U;
  last_render_state_.selection_epoch
    = inputs.frame_light_set != nullptr ? inputs.frame_light_set->selection_epoch
                                        : 0U;

  const auto* directional_light = inputs.frame_light_set != nullptr
      && inputs.frame_light_set->directional_light.has_value()
      && inputs.frame_light_set->directional_light->cascade_count > 0U
      && (inputs.frame_light_set->directional_light->shadow_flags
          & kDirectionalLightShadowFlagCastsShadows)
        != 0U
    ? &*inputs.frame_light_set->directional_light
    : nullptr;
  for (const auto& view_input : inputs.active_views) {
    auto view_data = DirectionalShadowFrameData {};
    auto shadow_surface = std::shared_ptr<graphics::Texture> {};
    auto spot_shadow_surface = std::shared_ptr<graphics::Texture> {};
    auto point_shadow_surface = std::shared_ptr<graphics::Texture> {};
    auto rendered_cascade_count = 0U;
    auto rendered_spot_shadow_count = 0U;
    auto rendered_point_shadow_count = 0U;
    auto rendered_draw_count = 0U;
    auto shadow_caster_draw_count = 0U;

    if (directional_light != nullptr) {
      const auto view_state = cascade_shadow_pass_->RenderDirectionalView(
        view_input, *directional_light);
      view_data = view_state.frame_data;
      shadow_surface = view_state.shadow_surface;
      rendered_cascade_count = view_state.rendered_cascade_count;
      rendered_draw_count = view_state.rendered_draw_count;
      shadow_caster_draw_count = view_state.shadow_caster_draw_count;
    }

    if (inputs.frame_light_set != nullptr
      && !inputs.frame_light_set->local_lights.empty()) {
      const auto spot_state = cascade_shadow_pass_->RenderSpotView(
        view_input, std::span(inputs.frame_light_set->local_lights));
      view_data.bindings.spot_shadow_surface_handle
        = spot_state.bindings.spot_shadow_surface_handle;
      view_data.bindings.spot_shadow_count = spot_state.bindings.spot_shadow_count;
      view_data.bindings.technique_flags |= spot_state.bindings.technique_flags;
      view_data.bindings.sampling_contract_flags
        |= spot_state.bindings.sampling_contract_flags;
      view_data.bindings.spot_shadows = spot_state.bindings.spot_shadows;
      spot_shadow_surface = spot_state.shadow_surface;
      rendered_spot_shadow_count = spot_state.rendered_shadow_count;
      rendered_draw_count += spot_state.rendered_draw_count;
      shadow_caster_draw_count = (std::max)(
        shadow_caster_draw_count, spot_state.shadow_caster_draw_count);

      const auto point_state = cascade_shadow_pass_->RenderPointView(
        view_input, std::span(inputs.frame_light_set->local_lights));
      view_data.bindings.point_shadow_surface_handle
        = point_state.bindings.point_shadow_surface_handle;
      view_data.bindings.point_shadow_count
        = point_state.bindings.point_shadow_count;
      view_data.bindings.technique_flags |= point_state.bindings.technique_flags;
      view_data.bindings.sampling_contract_flags
        |= point_state.bindings.sampling_contract_flags;
      view_data.bindings.point_shadows = point_state.bindings.point_shadows;
      point_shadow_surface = point_state.shadow_surface;
      rendered_point_shadow_count += point_state.rendered_shadow_count;
      rendered_draw_count += point_state.rendered_draw_count;
      shadow_caster_draw_count = (std::max)(
        shadow_caster_draw_count, point_state.shadow_caster_draw_count);
    }

    const auto slot = PublishShadowBindings(view_input.view_id, view_data.bindings);
    published_views_.insert_or_assign(view_input.view_id,
      PublishedView {
        .slot = slot,
        .data = view_data,
        .surface = shadow_surface,
        .spot_surface = spot_shadow_surface,
        .point_surface = point_shadow_surface,
      });

    last_render_state_.published_view_count += 1U;
    last_render_state_.rendered_cascade_count += rendered_cascade_count;
    last_render_state_.rendered_spot_shadow_count += rendered_spot_shadow_count;
    last_render_state_.rendered_point_shadow_count
      += rendered_point_shadow_count;
    last_render_state_.rendered_draw_count += rendered_draw_count;
    last_render_state_.shadow_caster_draw_count += shadow_caster_draw_count;
    if (view_data.bindings.cascade_count > 0U) {
      last_render_state_.directional_view_count += 1U;
    }
    if (view_data.bindings.spot_shadow_count > 0U) {
      last_render_state_.spot_view_count += 1U;
    }
    if (view_data.bindings.point_shadow_count > 0U) {
      last_render_state_.point_view_count += 1U;
    }
  }
}

auto ShadowService::InspectShadowData(const ViewId view_id) const
  -> const DirectionalShadowFrameData*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? &it->second.data : nullptr;
}

auto ShadowService::InspectShadowSurface(const ViewId view_id) const
  -> const graphics::Texture*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? it->second.surface.get() : nullptr;
}

auto ShadowService::InspectSpotShadowSurface(const ViewId view_id) const
  -> const graphics::Texture*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? it->second.spot_surface.get() : nullptr;
}

auto ShadowService::InspectPointShadowSurface(const ViewId view_id) const
  -> const graphics::Texture*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? it->second.point_surface.get() : nullptr;
}

auto ShadowService::ResolveShadowFrameSlot(const ViewId view_id) const
  -> ShaderVisibleIndex
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? it->second.slot
                                      : ShaderVisibleIndex { kInvalidShaderVisibleIndex };
}

} // namespace oxygen::vortex
