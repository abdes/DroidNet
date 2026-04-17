//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Passes/FogPass.h>

#include <limits>
#include <vector>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>

namespace oxygen::vortex::environment {

namespace {

namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

auto RangeTypeToViewType(const bindless_d3d12::RangeType type)
  -> graphics::ResourceViewType
{
  using graphics::ResourceViewType;

  switch (type) {
  case bindless_d3d12::RangeType::SRV:
    return ResourceViewType::kRawBuffer_SRV;
  case bindless_d3d12::RangeType::Sampler:
    return ResourceViewType::kSampler;
  case bindless_d3d12::RangeType::UAV:
    return ResourceViewType::kRawBuffer_UAV;
  default:
    return ResourceViewType::kNone;
  }
}

auto BuildVortexRootBindings() -> std::vector<graphics::RootBindingItem>
{
  std::vector<graphics::RootBindingItem> bindings;
  bindings.reserve(bindless_d3d12::kRootParamTableCount);

  for (std::uint32_t index = 0; index < bindless_d3d12::kRootParamTableCount;
    ++index) {
    const auto& desc = bindless_d3d12::kRootParamTable.at(index);
    graphics::RootBindingDesc binding {};
    binding.binding_slot_desc.register_index = desc.shader_register;
    binding.binding_slot_desc.register_space = desc.register_space;
    binding.visibility = graphics::ShaderStageFlags::kAll;

    switch (desc.kind) {
    case bindless_d3d12::RootParamKind::DescriptorTable: {
      graphics::DescriptorTableBinding table {};
      if (desc.ranges_count > 0U && desc.ranges.data() != nullptr) {
        const auto& range = desc.ranges.front();
        table.view_type = RangeTypeToViewType(
          static_cast<bindless_d3d12::RangeType>(range.range_type));
        table.base_index = range.base_register;
        table.count
          = range.num_descriptors
              == (std::numeric_limits<std::uint32_t>::max)()
            ? (std::numeric_limits<std::uint32_t>::max)()
            : range.num_descriptors;
      }
      binding.data = table;
      break;
    }
    case bindless_d3d12::RootParamKind::CBV:
      binding.data = graphics::DirectBufferBinding {};
      break;
    case bindless_d3d12::RootParamKind::RootConstants:
      binding.data
        = graphics::PushConstantsBinding { .size = desc.constants_count };
      break;
    }

    bindings.emplace_back(binding);
  }

  return bindings;
}

auto TrackTextureFromKnownOrInitial(graphics::CommandRecorder& recorder,
  const graphics::Texture& texture) -> void
{
  if (recorder.IsResourceTracked(texture) || recorder.AdoptKnownResourceState(texture)) {
    return;
  }

  auto initial = texture.GetDescriptor().initial_state;
  if (initial == graphics::ResourceStates::kUnknown
    || initial == graphics::ResourceStates::kUndefined) {
    initial = graphics::ResourceStates::kCommon;
  }
  recorder.BeginTrackingResourceState(texture, initial);
}

auto BuildFogFramebuffer(const SceneTextures& scene_textures)
  -> graphics::FramebufferDesc
{
  auto desc = graphics::FramebufferDesc {};
  desc.AddColorAttachment({
    .texture = scene_textures.GetSceneColorResource(),
    .format = scene_textures.GetSceneColor().GetDescriptor().format,
  });
  desc.SetDepthAttachment({
    .texture = scene_textures.GetSceneDepthResource(),
    .format = scene_textures.GetSceneDepth().GetDescriptor().format,
    .is_read_only = true,
  });
  return desc;
}

auto SetViewportAndScissor(graphics::CommandRecorder& recorder,
  const RenderContext& ctx, const SceneTextures& scene_textures) -> void
{
  const auto extent = scene_textures.GetExtent();
  if (ctx.current_view.resolved_view != nullptr) {
    const auto clamped = oxygen::vortex::internal::ResolveClampedViewportState(
      ctx.current_view.resolved_view->Viewport(),
      ctx.current_view.resolved_view->Scissor(), extent.x, extent.y);
    recorder.SetViewport(clamped.viewport);
    recorder.SetScissors(clamped.scissors);
    return;
  }

  recorder.SetViewport({
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(extent.x),
    .height = static_cast<float>(extent.y),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  });
  recorder.SetScissors({
    .left = 0,
    .top = 0,
    .right = static_cast<std::int32_t>(extent.x),
    .bottom = static_cast<std::int32_t>(extent.y),
  });
}

auto BuildFogPipelineDesc(const SceneTextures& scene_textures)
  -> graphics::GraphicsPipelineDesc
{
  auto root_bindings = BuildVortexRootBindings();
  const auto alpha_blend = graphics::BlendTargetDesc {
    .blend_enable = true,
    .src_blend = graphics::BlendFactor::kSrcAlpha,
    .dest_blend = graphics::BlendFactor::kInvSrcAlpha,
    .blend_op = graphics::BlendOp::kAdd,
    .src_blend_alpha = graphics::BlendFactor::kOne,
    .dest_blend_alpha = graphics::BlendFactor::kInvSrcAlpha,
    .blend_op_alpha = graphics::BlendOp::kAdd,
    .write_mask = graphics::ColorWriteMask::kAll,
  };
  return graphics::GraphicsPipelineDesc::Builder {}
    .SetVertexShader(graphics::ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "Vortex/Services/Environment/Fog.hlsl",
      .entry_point = "VortexFogPassVS",
    })
    .SetPixelShader(graphics::ShaderRequest {
      .stage = ShaderType::kPixel,
      .source_path = "Vortex/Services/Environment/Fog.hlsl",
      .entry_point = "VortexFogPassPS",
    })
    .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
    .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
    .SetDepthStencilState(graphics::DepthStencilStateDesc::Disabled())
    .SetBlendState({ alpha_blend })
    .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
      .color_target_formats = {
        scene_textures.GetSceneColor().GetDescriptor().format,
      },
      .depth_stencil_format = scene_textures.GetSceneDepth().GetDescriptor().format,
      .sample_count = scene_textures.GetSceneColor().GetDescriptor().sample_count,
      .sample_quality = scene_textures.GetSceneColor().GetDescriptor().sample_quality,
    })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size()))
    .SetDebugName("Vortex.Environment.Fog")
    .Build();
}

} // namespace

FogPass::FogPass(Renderer& renderer) : renderer_(renderer) { }

FogPass::~FogPass() = default;

auto FogPass::Record(
  RenderContext& ctx, const SceneTextures& scene_textures) const -> RecordState
{
  const auto requested = ctx.current_view.view_id != kInvalidViewId;
  auto state = RecordState {
    .requested = requested,
    .executed = requested
      && renderer_.HasCapability(RendererCapabilityFamily::kEnvironmentLighting),
  };

  if (!state.executed) {
    return state;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    state.executed = false;
    return state;
  }

  auto framebuffer = gfx->CreateFramebuffer(BuildFogFramebuffer(scene_textures));
  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key, "EnvironmentLightingService Fog");
  if (!recorder) {
    state.executed = false;
    return state;
  }

  graphics::GpuEventScope pass_scope(*recorder, "Vortex.Stage15.Fog",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);
  TrackTextureFromKnownOrInitial(*recorder, scene_textures.GetSceneColor());
  TrackTextureFromKnownOrInitial(*recorder, scene_textures.GetSceneDepth());
  recorder->RequireResourceState(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  recorder->RequireResourceState(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  recorder->FlushBarriers();
  recorder->BindFrameBuffer(*framebuffer);
  SetViewportAndScissor(*recorder, ctx, scene_textures);
  recorder->SetPipelineState(BuildFogPipelineDesc(scene_textures));
  if (ctx.view_constants != nullptr) {
    recorder->SetGraphicsRootConstantBufferView(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
      ctx.view_constants->GetGPUVirtualAddress());
  }
  recorder->SetGraphicsRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    0U, 0U);
  recorder->SetGraphicsRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    0U, 1U);
  recorder->Draw(3U, 1U, 0U, 0U);
  recorder->RequireResourceStateFinal(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  recorder->RequireResourceStateFinal(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  gfx->RegisterDeferredRelease(std::move(framebuffer));
  state.draw_count = 1U;
  state.bound_scene_color = true;
  state.sampled_scene_depth = true;
  return state;
}

} // namespace oxygen::vortex::environment
