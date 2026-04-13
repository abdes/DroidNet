//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex::detail {

[[nodiscard]] inline auto MakeFramebufferSizedView(
  const observer_ptr<const graphics::Framebuffer> framebuffer) -> View
{
  CHECK_NOTNULL_F(
    framebuffer.get(), "Facade preset requires a valid output framebuffer");
  CHECK_F(!framebuffer->GetDescriptor().color_attachments.empty()
      && framebuffer->GetDescriptor().color_attachments[0].texture != nullptr,
    "Facade preset requires an output framebuffer with a color attachment");

  const auto& color_texture
    = framebuffer->GetDescriptor().color_attachments[0].texture;
  auto view = View {};
  view.viewport = {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(color_texture->GetDescriptor().width),
    .height = static_cast<float>(color_texture->GetDescriptor().height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  return view;
}

} // namespace oxygen::vortex::detail

namespace oxygen::vortex::harness::single_pass::presets {

[[nodiscard]] inline auto ForResolvedViewGraphicsPass(Renderer& renderer,
  Renderer::FrameSessionInput frame_session,
  const observer_ptr<const graphics::Framebuffer> framebuffer,
  Renderer::ResolvedViewInput resolved_view)
  -> Renderer::SinglePassHarnessFacade
{
  auto facade = renderer.ForSinglePassHarness();
  facade.SetFrameSession(frame_session);
  facade.SetOutputTarget(
    Renderer::OutputTargetInput { .framebuffer = framebuffer });
  facade.SetResolvedView(resolved_view);
  return facade;
}

[[nodiscard]] inline auto ForFullscreenGraphicsPass(Renderer& renderer,
  Renderer::FrameSessionInput frame_session,
  const observer_ptr<const graphics::Framebuffer> framebuffer,
  const ViewId view_id = ViewId { 1U }) -> Renderer::SinglePassHarnessFacade
{
  auto facade = renderer.ForSinglePassHarness();
  facade.SetFrameSession(frame_session);
  facade.SetOutputTarget(
    Renderer::OutputTargetInput { .framebuffer = framebuffer });
  facade.SetCoreShaderInputs(Renderer::CoreShaderInputsInput {
    .view_id = view_id,
    .value = ViewConstants {},
  });
  return facade;
}

[[nodiscard]] inline auto ForPreparedSceneGraphicsPass(Renderer& renderer,
  Renderer::FrameSessionInput frame_session,
  const observer_ptr<const graphics::Framebuffer> framebuffer,
  Renderer::ResolvedViewInput resolved_view,
  Renderer::PreparedFrameInput prepared_frame)
  -> Renderer::SinglePassHarnessFacade
{
  auto facade = renderer.ForSinglePassHarness();
  facade.SetFrameSession(frame_session);
  facade.SetOutputTarget(
    Renderer::OutputTargetInput { .framebuffer = framebuffer });
  facade.SetResolvedView(resolved_view);
  facade.SetPreparedFrame(prepared_frame);
  return facade;
}

[[nodiscard]] inline auto ForPreparedSceneGraphicsPass(Renderer& renderer,
  Renderer::FrameSessionInput frame_session,
  const observer_ptr<const graphics::Framebuffer> framebuffer,
  Renderer::ResolvedViewInput resolved_view,
  Renderer::PreparedFrameInput prepared_frame,
  Renderer::CoreShaderInputsInput core_shader_inputs)
  -> Renderer::SinglePassHarnessFacade
{
  auto facade = ForPreparedSceneGraphicsPass(
    renderer, frame_session, framebuffer, resolved_view, prepared_frame);
  facade.SetCoreShaderInputs(core_shader_inputs);
  return facade;
}

} // namespace oxygen::vortex::harness::single_pass::presets

namespace oxygen::vortex::harness::render_graph::presets {

[[nodiscard]] inline auto ForSingleViewGraph(Renderer& renderer,
  Renderer::FrameSessionInput frame_session,
  const observer_ptr<const graphics::Framebuffer> framebuffer,
  Renderer::ResolvedViewInput resolved_view,
  Renderer::RenderGraphHarnessInput graph) -> Renderer::RenderGraphHarnessFacade
{
  auto facade = renderer.ForRenderGraphHarness();
  facade.SetFrameSession(frame_session);
  facade.SetOutputTarget(
    Renderer::OutputTargetInput { .framebuffer = framebuffer });
  facade.SetResolvedView(resolved_view);
  facade.SetRenderGraph(std::move(graph));
  return facade;
}

[[nodiscard]] inline auto ForSingleViewGraph(Renderer& renderer,
  Renderer::FrameSessionInput frame_session,
  const observer_ptr<const graphics::Framebuffer> framebuffer,
  Renderer::ResolvedViewInput resolved_view,
  Renderer::PreparedFrameInput prepared_frame,
  Renderer::RenderGraphHarnessInput graph) -> Renderer::RenderGraphHarnessFacade
{
  auto facade = ForSingleViewGraph(
    renderer, frame_session, framebuffer, resolved_view, std::move(graph));
  facade.SetPreparedFrame(prepared_frame);
  return facade;
}

[[nodiscard]] inline auto ForSingleViewGraph(Renderer& renderer,
  Renderer::FrameSessionInput frame_session,
  const observer_ptr<const graphics::Framebuffer> framebuffer,
  Renderer::ResolvedViewInput resolved_view,
  Renderer::PreparedFrameInput prepared_frame,
  Renderer::CoreShaderInputsInput core_shader_inputs,
  Renderer::RenderGraphHarnessInput graph) -> Renderer::RenderGraphHarnessFacade
{
  auto facade = ForSingleViewGraph(renderer, frame_session, framebuffer,
    resolved_view, prepared_frame, std::move(graph));
  facade.SetCoreShaderInputs(core_shader_inputs);
  return facade;
}

} // namespace oxygen::vortex::harness::render_graph::presets

namespace oxygen::vortex::offscreen::scene::presets {

[[nodiscard]] inline auto ForPreview(Renderer& renderer,
  Renderer::FrameSessionInput frame_session,
  const observer_ptr<oxygen::scene::Scene> scene_source,
  const oxygen::scene::SceneNode& camera,
  const observer_ptr<const graphics::Framebuffer> framebuffer,
  std::optional<Renderer::OffscreenPipelineInput> pipeline = std::nullopt)
  -> Renderer::OffscreenSceneFacade
{
  auto facade = renderer.ForOffscreenScene();
  facade.SetFrameSession(frame_session);
  facade.SetSceneSource(Renderer::SceneSourceInput { .scene = scene_source });
  facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera("Preview",
    kInvalidViewId, detail::MakeFramebufferSizedView(framebuffer), camera)
      .SetWithAtmosphere(true));
  facade.SetOutputTarget(
    Renderer::OutputTargetInput { .framebuffer = framebuffer });
  if (pipeline.has_value()) {
    facade.SetPipeline(*pipeline);
  }
  return facade;
}

[[nodiscard]] inline auto ForCapture(Renderer& renderer,
  Renderer::FrameSessionInput frame_session,
  const observer_ptr<oxygen::scene::Scene> scene_source,
  const oxygen::scene::SceneNode& camera,
  const observer_ptr<const graphics::Framebuffer> framebuffer,
  std::optional<Renderer::OffscreenPipelineInput> pipeline = std::nullopt)
  -> Renderer::OffscreenSceneFacade
{
  auto facade = renderer.ForOffscreenScene();
  facade.SetFrameSession(frame_session);
  facade.SetSceneSource(Renderer::SceneSourceInput { .scene = scene_source });
  facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera("Capture",
    kInvalidViewId, detail::MakeFramebufferSizedView(framebuffer), camera)
      .SetWithAtmosphere(false));
  facade.SetOutputTarget(
    Renderer::OutputTargetInput { .framebuffer = framebuffer });
  if (pipeline.has_value()) {
    facade.SetPipeline(*pipeline);
  }
  return facade;
}

} // namespace oxygen::vortex::offscreen::scene::presets
