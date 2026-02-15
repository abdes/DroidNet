//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Runtime/Internal/ForwardPipelineImpl.h"

namespace oxygen::examples {

ForwardPipeline::ForwardPipeline(observer_ptr<AsyncEngine> engine) noexcept
  : impl_(std::make_unique<internal::ForwardPipelineImpl>(engine))
{
}

ForwardPipeline::~ForwardPipeline() = default;

auto ForwardPipeline::GetSupportedFeatures() const -> PipelineFeature
{
  return PipelineFeature::kOpaqueShading | PipelineFeature::kTransparentShading
    | PipelineFeature::kLightCulling;
}

auto ForwardPipeline::OnFrameStart(
  observer_ptr<engine::FrameContext> /*context*/, engine::Renderer& renderer)
  -> void
{
  impl_->ApplySettings();
  renderer.SetAtmosphereBlueNoiseEnabled(
    impl_->frame_settings.atmosphere_blue_noise_enabled);
}

auto ForwardPipeline::OnPublishViews(observer_ptr<engine::FrameContext> context,
  engine::Renderer& renderer, scene::Scene& scene,
  std::span<const CompositionView> view_descs,
  graphics::Framebuffer* composite_target) -> co::Co<>
{
  if (impl_->auto_exposure_config) {
    if (const auto env = scene.GetEnvironment()) {
      if (const auto pp
        = env->TryGetSystem<scene::environment::PostProcessVolume>();
        pp && pp->IsEnabled()) {
        impl_->auto_exposure_config->metering_mode
          = pp->GetAutoExposureMeteringMode();
      }
    }
  }
  auto graphics = impl_->engine->GetGraphics().lock();
  CHECK_NOTNULL_F(graphics.get(), "Graphics backend is not available");
  impl_->PublishView(view_descs, observer_ptr { composite_target }, *graphics,
    *context, renderer);
  impl_->UnpublishView(*context, renderer);
  co_return;
}

auto ForwardPipeline::OnPreRender(observer_ptr<engine::FrameContext> context,
  engine::Renderer& renderer, std::span<const CompositionView> /*view_descs*/)
  -> co::Co<>
{
  impl_->RegisterRenderGraphs(renderer);
  impl_->BuildFramePlan(context->GetScene());
  impl_->PlanCompositingTasks();

  co_return;
}

auto ForwardPipeline::OnCompositing(
  observer_ptr<engine::FrameContext> /*frame_ctx*/,
  engine::Renderer& /*renderer*/, graphics::Framebuffer* final_output)
  -> co::Co<engine::CompositionSubmission>
{
  co_return impl_->BuildCompositionSubmission(final_output);
}

auto ForwardPipeline::ClearBackbufferReferences() -> void
{
  impl_->ClearBackbufferReferences();
}

// Stubs for configuration methods
auto ForwardPipeline::SetShaderDebugMode(engine::ShaderDebugMode mode) -> void
{
  impl_->SetShaderDebugMode(mode);
}

auto ForwardPipeline::SetRenderMode(RenderMode mode) -> void
{
  impl_->SetRenderMode(mode);
}

auto ForwardPipeline::SetGpuDebugPassEnabled(bool enabled) -> void
{
  impl_->SetGpuDebugPassEnabled(enabled);
}

auto ForwardPipeline::SetAtmosphereBlueNoiseEnabled(bool enabled) -> void
{
  impl_->SetAtmosphereBlueNoiseEnabled(enabled);
}

auto ForwardPipeline::SetGpuDebugMouseDownPosition(
  std::optional<SubPixelPosition> position) -> void
{
  impl_->SetGpuDebugMouseDownPosition(position);
}

auto ForwardPipeline::SetWireframeColor(const graphics::Color& color) -> void
{
  DLOG_F(1, "SetWireframeColor ({}, {}, {}, {})", color.r, color.g, color.b,
    color.a);
  impl_->SetWireframeColor(color);
}

auto ForwardPipeline::SetLightCullingVisualizationMode(
  engine::ShaderDebugMode mode) -> void
{
  impl_->SetLightCullingVisualizationMode(mode);
}

auto ForwardPipeline::SetClusterDepthSlices(uint32_t slices) -> void
{
  impl_->SetClusterDepthSlices(slices);
}

auto ForwardPipeline::SetExposureMode(engine::ExposureMode mode) -> void
{
  DLOG_F(1, "SetExposureMode {}", mode);
  impl_->SetExposureMode(mode);
}

auto ForwardPipeline::SetExposureValue(float value) -> void
{
  DLOG_F(1, "SetExposureValue {}", value);
  impl_->SetExposureValue(value);
}

auto ForwardPipeline::SetToneMapper(engine::ToneMapper mode) -> void
{
  LOG_F(INFO, "ForwardPipeline: SetToneMapper {}", mode);
  impl_->SetToneMapper(mode);
}

auto ForwardPipeline::SetGroundGridConfig(
  const engine::GroundGridPassConfig& config) -> void
{
  static std::atomic<bool> logged_once { false };
  if (!logged_once.exchange(true)) {
    LOG_F(INFO, "ForwardPipeline: SetGroundGridConfig");
  }
  impl_->SetGroundGridConfig(config);
}

auto ForwardPipeline::SetAutoExposureAdaptationSpeedUp(float speed) -> void
{
  impl_->SetAutoExposureAdaptationSpeedUp(speed);
}

auto ForwardPipeline::SetAutoExposureAdaptationSpeedDown(float speed) -> void
{
  impl_->SetAutoExposureAdaptationSpeedDown(speed);
}

auto ForwardPipeline::SetAutoExposureLowPercentile(float percentile) -> void
{
  impl_->SetAutoExposureLowPercentile(percentile);
}

auto ForwardPipeline::SetAutoExposureHighPercentile(float percentile) -> void
{
  impl_->SetAutoExposureHighPercentile(percentile);
}

auto ForwardPipeline::SetAutoExposureMinLogLuminance(float luminance) -> void
{
  impl_->SetAutoExposureMinLogLuminance(luminance);
}
auto ForwardPipeline::SetAutoExposureLogLuminanceRange(float range) -> void
{
  impl_->SetAutoExposureLogLuminanceRange(range);
}
auto ForwardPipeline::SetAutoExposureTargetLuminance(float luminance) -> void
{
  impl_->SetAutoExposureTargetLuminance(luminance);
}
auto ForwardPipeline::SetAutoExposureSpotMeterRadius(float radius) -> void
{
  impl_->SetAutoExposureSpotMeterRadius(radius);
}
auto ForwardPipeline::SetAutoExposureMeteringMode(engine::MeteringMode mode)
  -> void
{
  impl_->SetAutoExposureMeteringMode(mode);
}

auto ForwardPipeline::ResetAutoExposure(float initial_ev) -> void
{
  impl_->ResetAutoExposure(initial_ev);
}

auto ForwardPipeline::UpdateShaderPassConfig(
  const engine::ShaderPassConfig& config) -> void
{
  if (impl_->shader_pass_config) {
    *impl_->shader_pass_config = config;
  }
}

auto ForwardPipeline::SetGamma(float gamma) -> void { impl_->SetGamma(gamma); }

auto ForwardPipeline::UpdateTransparentPassConfig(
  const engine::TransparentPassConfig& config) -> void
{
  if (impl_->transparent_pass_config) {
    *impl_->transparent_pass_config = config;
  }
}

auto ForwardPipeline::UpdateLightCullingPassConfig(
  const engine::LightCullingPassConfig& config) -> void
{
  if (impl_->light_culling_pass_config) {
    *impl_->light_culling_pass_config = config;
  }
}

} // namespace oxygen::examples
