//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/IndirectLighting/IndirectLightingService.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Types/EnvironmentFrameBindings.h>
namespace oxygen::vortex {
auto IndirectLightingService::Record(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const SceneTextures& textures,
  const EnvironmentFrameBindings& bindings) -> bool
{
  constexpr auto required = kEnvironmentContractFlagSkyLightAuthoredEnabled
    | kEnvironmentContractFlagSkyLightIblValid;
  if ((bindings.contract_flags & required) != required
    || !bindings.brdf_lut_srv.IsValid() || !ctx.view_constants
    || textures.GetGBufferCount() < 3U)
    return false;
  auto graphics = renderer_.GetGraphics();
  if (!graphics)
    return false;
  if (!framebuffer_
    || framebuffer_->GetDescriptor().color_attachments[0].texture.get()
      != textures.GetSceneColorResource().get()) {
    if (framebuffer_)
      graphics->RegisterDeferredRelease(std::move(framebuffer_));
    framebuffer_ = graphics->CreateFramebuffer(
      graphics::FramebufferDesc {}.AddColorAttachment(
        textures.GetSceneColorResource()));
  }
  if (!framebuffer_)
    return false;
  namespace root = bindless::generated::d3d12;
  using graphics::ResourceStates;
  const auto track
    = [&](const graphics::Texture& texture, ResourceStates desired) {
        if (!recorder.IsResourceTracked(texture)
          && !recorder.AdoptKnownResourceState(texture))
          recorder.BeginTrackingResourceState(
            texture, texture.GetDescriptor().initial_state, true);
        recorder.RequireResourceState(texture, desired);
      };
  track(textures.GetSceneColor(), ResourceStates::kRenderTarget);
  track(textures.GetSceneDepth(), ResourceStates::kShaderResource);
  for (unsigned i = 0; i < textures.GetGBufferCount(); ++i)
    track(textures.GetGBuffer(static_cast<GBufferIndex>(i)),
      ResourceStates::kShaderResource);
  if (const auto& exposure = ctx.current_view.frame_exposure) {
    const auto& status = *exposure->current_state->status_buffer;
    if (!recorder.IsResourceTracked(status)
      && !recorder.AdoptKnownResourceState(status))
      recorder.BeginTrackingResourceState(
        status, ResourceStates::kCommon, false);
    recorder.RequireResourceState(status, ResourceStates::kUnorderedAccess);
  }
  const auto root_bindings = internal::BuildVortexRootBindings();
  const auto pipeline
    = graphics::GraphicsPipelineDesc::Builder {}
        .SetVertexShader({ .stage = ShaderType::kVertex,
          .source_path = "Vortex/Services/IndirectLighting/DeferredIbl.hlsl",
          .entry_point = "DeferredIblVS" })
        .SetPixelShader({ .stage = ShaderType::kPixel,
          .source_path = "Vortex/Services/IndirectLighting/DeferredIbl.hlsl",
          .entry_point = "DeferredIblPS" })
        .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
        .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
        .SetDepthStencilState(graphics::DepthStencilStateDesc::Disabled())
        .SetBlendState({ graphics::BlendTargetDesc { .blend_enable = true,
          .src_blend = graphics::BlendFactor::kOne,
          .dest_blend = graphics::BlendFactor::kOne,
          .blend_op = graphics::BlendOp::kAdd,
          .src_blend_alpha = graphics::BlendFactor::kZero,
          .dest_blend_alpha = graphics::BlendFactor::kOne,
          .blend_op_alpha = graphics::BlendOp::kAdd } })
        .SetFramebufferLayout({ .color_target_formats
          = { textures.GetSceneColor().GetDescriptor().format },
          .sample_count = textures.GetSceneColor().GetDescriptor().sample_count,
          .sample_quality
          = textures.GetSceneColor().GetDescriptor().sample_quality })
        .SetRootBindings(root_bindings)
        .SetDebugName("Vortex.Stage13.IndirectLighting")
        .Build();
  const graphics::GpuEventScope scope(recorder,
    "Vortex.Stage13.IndirectLighting",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  recorder.FlushBarriers();
  recorder.BindFrameBuffer(*framebuffer_);
  const auto extent = textures.GetExtent();
  if (ctx.current_view.resolved_view) {
    const auto viewport = internal::ResolveClampedViewportState(
      ctx.current_view.resolved_view->Viewport(),
      ctx.current_view.resolved_view->Scissor(), extent.x, extent.y);
    recorder.SetViewport(viewport.viewport);
    recorder.SetScissors(viewport.scissors);
  } else {
    recorder.SetViewport({ .width = static_cast<float>(extent.x),
      .height = static_cast<float>(extent.y),
      .max_depth = 1.0F });
    recorder.SetScissors({ .right = static_cast<std::int32_t>(extent.x),
      .bottom = static_cast<std::int32_t>(extent.y) });
  }
  recorder.SetPipelineState(pipeline);
  recorder.SetGraphicsRootConstantBufferView(
    static_cast<std::uint32_t>(root::RootParam::kViewConstants),
    ctx.view_constants->GetGPUVirtualAddress());
  recorder.Draw(3U, 1U, 0U, 0U);
  return true;
}
} // namespace oxygen::vortex
