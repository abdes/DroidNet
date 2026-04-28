//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Internal/AuxiliaryDependencyGraph.h>
#include <Oxygen/Vortex/Internal/CompositionViewImpl.h>
#include <Oxygen/Vortex/SceneRenderer/Internal/FramePlanBuilder.h>

namespace oxygen::vortex::internal {

namespace {

  struct FramePlanDebugModeIntent {
    bool is_non_ibl { false };
    bool requires_neutral_tonemap { false };
  };

  auto EvaluateFramePlanDebugModeIntent(ShaderDebugMode mode)
    -> FramePlanDebugModeIntent
  {
    return FramePlanDebugModeIntent {
      .is_non_ibl = IsNonIblDebugMode(mode),
      .requires_neutral_tonemap = mode != ShaderDebugMode::kDisabled,
    };
  }

} // namespace

FramePlanBuilder::~FramePlanBuilder() = default;

void FramePlanBuilder::BuildFrameViewPackets(observer_ptr<scene::Scene> scene,
  std::span<CompositionViewImpl* const> ordered_active_views,
  const Inputs& inputs)
{
  frame_view_packets_.clear();
  frame_view_packet_index_.clear();
  frame_auto_exposure_reset_ = inputs.pending_auto_exposure_reset;
  frame_gpu_debug_pass_enabled_ = inputs.frame_settings.gpu_debug_pass_enabled;
  frame_want_auto_exposure_ = inputs.tone_map_pass_config
    && inputs.tone_map_pass_config->exposure_mode
      == engine::ExposureMode::kAuto;
  frame_wire_color_ = inputs.frame_settings.wire_color;
  frame_gpu_debug_mouse_down_position_
    = inputs.frame_settings.gpu_debug_mouse_down_position;

  const auto sky_state = EvaluateSkyState(scene);
  frame_view_packets_.reserve(ordered_active_views.size());
  for (auto* view : ordered_active_views) {
    CHECK_NOTNULL_F(inputs.resolve_published_view_id);
    const auto published_view_id
      = inputs.resolve_published_view_id(view->GetDescriptor().id);
    if (published_view_id == kInvalidViewId) {
      continue;
    }
    frame_view_packet_index_.emplace(
      published_view_id, frame_view_packets_.size());
    frame_view_packets_.emplace_back(observer_ptr { view }, published_view_id,
      view->GetDescriptor().view_state_handle,
      EvaluateViewRenderPlan(*view, sky_state, inputs));
  }

  const auto aux_plan = AuxiliaryDependencyGraph::Build(frame_view_packets_);
  auto ordered_packets = std::vector<FrameViewPacket> {};
  ordered_packets.reserve(frame_view_packets_.size());
  frame_view_packet_index_.clear();
  for (const auto old_index : aux_plan.ordered_packet_indices) {
    auto packet = std::move(frame_view_packets_.at(old_index));
    packet.SetResolvedAuxInputs(
      aux_plan.resolved_inputs_by_packet.at(old_index));
    frame_view_packet_index_.emplace(
      packet.PublishedViewId(), ordered_packets.size());
    ordered_packets.push_back(std::move(packet));
  }
  frame_view_packets_ = std::move(ordered_packets);
}

auto FramePlanBuilder::EvaluateSkyState(observer_ptr<scene::Scene> scene) const
  -> SkyState
{
  SkyState state {};
  if (!scene) {
    return state;
  }

  const auto env = scene->GetEnvironment();
  if (!env) {
    return state;
  }

  if (const auto atmo = env->TryGetSystem<scene::environment::SkyAtmosphere>();
    atmo && atmo->IsEnabled()) {
    state.sky_atmo_enabled = true;
  }
  if (const auto sphere = env->TryGetSystem<scene::environment::SkySphere>();
    sphere && sphere->IsEnabled()) {
    state.sky_sphere_enabled = true;
  }
  return state;
}

auto FramePlanBuilder::EvaluateViewRenderPlan(const CompositionViewImpl& view,
  const SkyState& sky_state, const Inputs& inputs) const -> ViewRenderPlan
{
  ViewRenderPlan::Spec plan_spec {};
  const auto& descriptor = view.GetDescriptor();
  const auto frame_shader_debug_mode = inputs.shader_pass_config
    ? inputs.shader_pass_config->debug_mode
    : ShaderDebugMode::kDisabled;
  plan_spec.effective_render_mode
    = descriptor.render_settings.render_mode.value_or(
      inputs.frame_settings.render_mode);
  plan_spec.effective_shader_debug_mode
    = descriptor.render_settings.shader_debug_mode.value_or(
      frame_shader_debug_mode);
  CHECK_F(descriptor.view_kind != CompositionView::ViewKind::kCompositionOnly
      || !descriptor.camera.has_value(),
    "Composition-only view '{}' cannot carry a scene camera",
    descriptor.name);
  CHECK_F(descriptor.view_kind == CompositionView::ViewKind::kCompositionOnly
      || descriptor.camera.has_value(),
    "Scene view '{}' requires a camera unless it is composition-only",
    descriptor.name);

  const bool is_scene_view
    = descriptor.view_kind != CompositionView::ViewKind::kCompositionOnly;
  plan_spec.intent = is_scene_view ? ViewRenderIntent::kSceneAndComposite
                                   : ViewRenderIntent::kCompositeOnly;

  if (view.GetDescriptor().force_wireframe) {
    plan_spec.effective_render_mode = RenderMode::kWireframe;
  }

  const bool has_hdr_resources = view.UsesHdrRenderTargets()
    && view.GetHdrTexture() && view.GetHdrFramebuffer();
  const bool has_sdr_resources
    = view.GetSdrTexture() && view.GetSdrFramebuffer();
  CHECK_F(has_sdr_resources, "View '{}' missing SDR resources",
    view.GetDescriptor().name);
  if (plan_spec.intent == ViewRenderIntent::kSceneAndComposite) {
    CHECK_F(has_hdr_resources, "Scene view '{}' missing HDR resources",
      view.GetDescriptor().name);
  }

  const auto debug_intent
    = EvaluateFramePlanDebugModeIntent(plan_spec.effective_shader_debug_mode);
  plan_spec.tone_map_policy
    = (is_scene_view
        && (plan_spec.effective_render_mode == RenderMode::kWireframe
          || debug_intent.requires_neutral_tonemap))
    ? ToneMapPolicy::kNeutral
    : ToneMapPolicy::kConfigured;

  plan_spec.run_overlay_wireframe = is_scene_view
    && (plan_spec.effective_render_mode == RenderMode::kOverlayWireframe)
    && (plan_spec.effective_render_mode != RenderMode::kWireframe);

  const bool run_scene_passes
    = (plan_spec.intent == ViewRenderIntent::kSceneAndComposite)
    && (plan_spec.effective_render_mode != RenderMode::kWireframe);
  plan_spec.depth_prepass_mode = run_scene_passes
    ? descriptor.render_settings.depth_prepass_mode.value_or(
        inputs.frame_settings.depth_prepass_mode)
    : DepthPrePassMode::kDisabled;
  plan_spec.run_sky_pass = run_scene_passes
    && (sky_state.sky_atmo_enabled || sky_state.sky_sphere_enabled)
    && !debug_intent.is_non_ibl;
  plan_spec.run_sky_lut_update = run_scene_passes && sky_state.sky_atmo_enabled;

  const ViewRenderPlan plan(plan_spec);
  DLOG_F(2,
    "ViewRenderPlan view='{}' mode={} debug={} intent={} tone_map={} "
    "depth_prepass={} overlay={} sky={} lut={}",
    view.GetDescriptor().name, plan.EffectiveRenderMode(),
    plan.EffectiveShaderDebugMode(), plan.Intent(), plan.GetToneMapPolicy(),
    plan.GetDepthPrePassMode(),
    plan.RunOverlayWireframe(), plan.RunSkyPass(), plan.RunSkyLutUpdate());

  return plan;
}

auto FramePlanBuilder::FindFrameViewPacket(ViewId id) const
  -> const FrameViewPacket*
{
  const auto it = frame_view_packet_index_.find(id);
  if (it == frame_view_packet_index_.end()) {
    return nullptr;
  }
  const auto index = it->second;
  if (index >= frame_view_packets_.size()) {
    return nullptr;
  }
  return &frame_view_packets_[index];
}

} // namespace oxygen::vortex::internal
