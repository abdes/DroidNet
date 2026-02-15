//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Runtime/Internal/FramePlanBuilder.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Runtime/Internal/ForwardPipelineImpl.h"

namespace oxygen::examples::internal {

namespace {

  struct FramePlanDebugModeIntent {
    bool is_non_ibl { false };
  };

  auto EvaluateFramePlanDebugModeIntent(engine::ShaderDebugMode mode)
    -> FramePlanDebugModeIntent
  {
    const auto is_non_ibl = [&] {
      switch (mode) {
      case engine::ShaderDebugMode::kLightCullingHeatMap:
      case engine::ShaderDebugMode::kDepthSlice:
      case engine::ShaderDebugMode::kClusterIndex:
      case engine::ShaderDebugMode::kBaseColor:
      case engine::ShaderDebugMode::kUv0:
      case engine::ShaderDebugMode::kOpacity:
      case engine::ShaderDebugMode::kWorldNormals:
      case engine::ShaderDebugMode::kRoughness:
      case engine::ShaderDebugMode::kMetalness:
        return true;
      case engine::ShaderDebugMode::kIblSpecular:
      case engine::ShaderDebugMode::kIblRawSky:
      case engine::ShaderDebugMode::kIblIrradiance:
      case engine::ShaderDebugMode::kIblFaceIndex:
      case engine::ShaderDebugMode::kDisabled:
      default:
        return false;
      }
    }();

    return FramePlanDebugModeIntent { .is_non_ibl = is_non_ibl };
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
  frame_render_mode_ = inputs.frame_settings.render_mode;
  frame_wire_color_ = inputs.frame_settings.wire_color;
  frame_shader_debug_mode_ = inputs.shader_pass_config
    ? inputs.shader_pass_config->debug_mode
    : engine::ShaderDebugMode::kDisabled;
  frame_gpu_debug_mouse_down_position_
    = inputs.frame_settings.gpu_debug_mouse_down_position;

  const auto sky_state = EvaluateSkyState(scene);
  frame_view_packets_.reserve(ordered_active_views.size());
  for (auto* view : ordered_active_views) {
    if (view->registered_view_id == kInvalidViewId) {
      continue;
    }
    frame_view_packet_index_.emplace(
      view->registered_view_id, frame_view_packets_.size());
    frame_view_packets_.emplace_back(
      observer_ptr { view }, EvaluateViewRenderPlan(*view, sky_state, inputs));
  }
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
  plan_spec.effective_render_mode = frame_render_mode_;
  const bool is_scene_view = view.intent.camera.has_value();
  plan_spec.intent = is_scene_view ? ViewRenderIntent::kSceneAndComposite
                                   : ViewRenderIntent::kCompositeOnly;

  if (view.intent.force_wireframe) {
    plan_spec.effective_render_mode = RenderMode::kWireframe;
  }

  const bool has_hdr_resources
    = view.has_hdr && view.hdr_texture && view.hdr_framebuffer;
  const bool has_sdr_resources = view.sdr_texture && view.sdr_framebuffer;
  CHECK_F(
    has_sdr_resources, "View '{}' missing SDR resources", view.intent.name);
  if (plan_spec.intent == ViewRenderIntent::kSceneAndComposite) {
    CHECK_F(has_hdr_resources, "Scene view '{}' missing HDR resources",
      view.intent.name);
  }

  plan_spec.tone_map_policy
    = (is_scene_view
        && plan_spec.effective_render_mode == RenderMode::kWireframe)
    ? ToneMapPolicy::kNeutral
    : ToneMapPolicy::kConfigured;

  plan_spec.run_overlay_wireframe = is_scene_view
    && (inputs.frame_settings.render_mode == RenderMode::kOverlayWireframe)
    && (plan_spec.effective_render_mode != RenderMode::kWireframe);

  const auto debug_intent
    = EvaluateFramePlanDebugModeIntent(frame_shader_debug_mode_);
  const bool run_scene_passes
    = (plan_spec.intent == ViewRenderIntent::kSceneAndComposite)
    && (plan_spec.effective_render_mode != RenderMode::kWireframe);
  plan_spec.run_sky_pass = run_scene_passes
    && (sky_state.sky_atmo_enabled || sky_state.sky_sphere_enabled)
    && !debug_intent.is_non_ibl;
  plan_spec.run_sky_lut_update = run_scene_passes && sky_state.sky_atmo_enabled;

  const ViewRenderPlan plan(plan_spec);
  DLOG_F(2,
    "ViewRenderPlan view='{}' mode={} intent={} tone_map={} overlay={} sky={} "
    "lut={}",
    view.intent.name, plan.EffectiveRenderMode(), plan.Intent(),
    plan.GetToneMapPolicy(), plan.RunOverlayWireframe(), plan.RunSkyPass(),
    plan.RunSkyLutUpdate());

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

} // namespace oxygen::examples::internal
