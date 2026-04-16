//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <vector>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h>

namespace oxygen::vortex {

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
        table.view_type
          = RangeTypeToViewType(static_cast<bindless_d3d12::RangeType>(
            range.range_type));
        table.base_index = range.base_register;
        table.count
          = range.num_descriptors == (std::numeric_limits<std::uint32_t>::max)()
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

auto BuildBasePassFramebuffer(SceneTextures& scene_textures,
  const bool depth_read_only) -> graphics::FramebufferDesc
{
  auto desc = graphics::FramebufferDesc {};
  desc.AddColorAttachment({
    .texture = scene_textures.GetGBufferResource(GBufferIndex::kNormal),
    .format = scene_textures.GetGBufferNormal().GetDescriptor().format,
  });
  desc.AddColorAttachment({
    .texture = scene_textures.GetGBufferResource(GBufferIndex::kMaterial),
    .format = scene_textures.GetGBufferMaterial().GetDescriptor().format,
  });
  desc.AddColorAttachment({
    .texture = scene_textures.GetGBufferResource(GBufferIndex::kBaseColor),
    .format = scene_textures.GetGBufferBaseColor().GetDescriptor().format,
  });
  desc.AddColorAttachment({
    .texture = scene_textures.GetGBufferResource(GBufferIndex::kCustomData),
    .format = scene_textures.GetGBufferCustomData().GetDescriptor().format,
  });
  desc.AddColorAttachment({
    .texture = scene_textures.GetSceneColorResource(),
    .format = scene_textures.GetSceneColor().GetDescriptor().format,
  });
  desc.SetDepthAttachment({
    .texture = scene_textures.GetSceneDepthResource(),
    .format = scene_textures.GetSceneDepth().GetDescriptor().format,
    .is_read_only = depth_read_only,
  });
  return desc;
}

auto BuildBasePassColorClearFramebuffer(SceneTextures& scene_textures)
  -> graphics::FramebufferDesc
{
  auto desc = graphics::FramebufferDesc {};
  desc.AddColorAttachment({
    .texture = scene_textures.GetGBufferResource(GBufferIndex::kNormal),
    .format = scene_textures.GetGBufferNormal().GetDescriptor().format,
  });
  desc.AddColorAttachment({
    .texture = scene_textures.GetGBufferResource(GBufferIndex::kMaterial),
    .format = scene_textures.GetGBufferMaterial().GetDescriptor().format,
  });
  desc.AddColorAttachment({
    .texture = scene_textures.GetGBufferResource(GBufferIndex::kBaseColor),
    .format = scene_textures.GetGBufferBaseColor().GetDescriptor().format,
  });
  desc.AddColorAttachment({
    .texture = scene_textures.GetGBufferResource(GBufferIndex::kCustomData),
    .format = scene_textures.GetGBufferCustomData().GetDescriptor().format,
  });
  desc.AddColorAttachment({
    .texture = scene_textures.GetSceneColorResource(),
    .format = scene_textures.GetSceneColor().GetDescriptor().format,
  });
  return desc;
}

auto NeedsFramebufferRebuild(const std::shared_ptr<graphics::Framebuffer>& framebuffer,
  const SceneTextures& scene_textures) -> bool
{
  if (!framebuffer) {
    return true;
  }

  const auto& desc = framebuffer->GetDescriptor();
  if (desc.color_attachments.size() != 5U
    || desc.depth_attachment.texture.get()
      != scene_textures.GetSceneDepthResource().get()) {
    return true;
  }

  return desc.color_attachments[0].texture.get()
      != scene_textures.GetGBufferResource(GBufferIndex::kNormal).get()
    || desc.color_attachments[1].texture.get()
      != scene_textures.GetGBufferResource(GBufferIndex::kMaterial).get()
    || desc.color_attachments[2].texture.get()
      != scene_textures.GetGBufferResource(GBufferIndex::kBaseColor).get()
    || desc.color_attachments[3].texture.get()
      != scene_textures.GetGBufferResource(GBufferIndex::kCustomData).get()
    || desc.color_attachments[4].texture.get()
      != scene_textures.GetSceneColorResource().get();
}

auto NeedsColorClearFramebufferRebuild(
  const std::shared_ptr<graphics::Framebuffer>& framebuffer,
  const SceneTextures& scene_textures) -> bool
{
  if (!framebuffer) {
    return true;
  }

  const auto& desc = framebuffer->GetDescriptor();
  if (desc.color_attachments.size() != 5U || desc.depth_attachment.texture) {
    return true;
  }

  return desc.color_attachments[0].texture.get()
      != scene_textures.GetGBufferResource(GBufferIndex::kNormal).get()
    || desc.color_attachments[1].texture.get()
      != scene_textures.GetGBufferResource(GBufferIndex::kMaterial).get()
    || desc.color_attachments[2].texture.get()
      != scene_textures.GetGBufferResource(GBufferIndex::kBaseColor).get()
    || desc.color_attachments[3].texture.get()
      != scene_textures.GetGBufferResource(GBufferIndex::kCustomData).get()
    || desc.color_attachments[4].texture.get()
      != scene_textures.GetSceneColorResource().get();
}

auto BuildBasePassPipelineDesc(const SceneTextures& scene_textures,
  const BasePassConfig& config, const bool reverse_z)
  -> graphics::GraphicsPipelineDesc
{
  auto root_bindings = BuildVortexRootBindings();

  auto blend_targets = std::vector<graphics::BlendTargetDesc>(5);
  for (auto& blend_target : blend_targets) {
    blend_target.blend_enable = false;
    blend_target.write_mask = graphics::ColorWriteMask::kAll;
  }

  const auto depth_state = graphics::DepthStencilStateDesc {
    .depth_test_enable = true,
    .depth_write_enable = !config.early_z_pass_done,
    .depth_func = reverse_z ? graphics::CompareOp::kGreaterOrEqual
                            : graphics::CompareOp::kLessOrEqual,
    .stencil_enable = false,
  };

  return graphics::GraphicsPipelineDesc::Builder {}
    .SetVertexShader(graphics::ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "Vortex/Stages/BasePass/BasePassGBuffer.hlsl",
      .entry_point = "BasePassGBufferVS",
    })
    .SetPixelShader(graphics::ShaderRequest {
      .stage = ShaderType::kPixel,
      .source_path = "Vortex/Stages/BasePass/BasePassGBuffer.hlsl",
      .entry_point = "BasePassGBufferPS",
    })
    .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
    .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
    .SetDepthStencilState(depth_state)
    .SetBlendState(std::move(blend_targets))
    .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
      .color_target_formats = {
        scene_textures.GetGBufferNormal().GetDescriptor().format,
        scene_textures.GetGBufferMaterial().GetDescriptor().format,
        scene_textures.GetGBufferBaseColor().GetDescriptor().format,
        scene_textures.GetGBufferCustomData().GetDescriptor().format,
        scene_textures.GetSceneColor().GetDescriptor().format,
      },
      .depth_stencil_format = scene_textures.GetSceneDepth().GetDescriptor().format,
      .sample_count = scene_textures.GetSceneColor().GetDescriptor().sample_count,
      .sample_quality
      = scene_textures.GetSceneColor().GetDescriptor().sample_quality,
    })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size()))
    .SetDebugName("Vortex.BasePass.GBuffer")
    .Build();
}

auto BeginPersistentWriteTarget(graphics::CommandRecorder& recorder,
  graphics::Texture& texture) -> void
{
  if (!recorder.AdoptKnownResourceState(texture)) {
    auto initial = texture.GetDescriptor().initial_state;
    if (initial == graphics::ResourceStates::kUnknown
      || initial == graphics::ResourceStates::kUndefined) {
      initial = graphics::ResourceStates::kCommon;
    }
    recorder.BeginTrackingResourceState(texture, initial);
  }
}

auto BeginBasePassResourceTracking(graphics::CommandRecorder& recorder,
  SceneTextures& scene_textures, const BasePassConfig& config) -> void
{
  BeginPersistentWriteTarget(recorder, scene_textures.GetGBufferNormal());
  BeginPersistentWriteTarget(recorder, scene_textures.GetGBufferMaterial());
  BeginPersistentWriteTarget(recorder, scene_textures.GetGBufferBaseColor());
  BeginPersistentWriteTarget(recorder, scene_textures.GetGBufferCustomData());
  BeginPersistentWriteTarget(recorder, scene_textures.GetSceneColor());
  BeginPersistentWriteTarget(recorder, scene_textures.GetSceneDepth());
  if (scene_textures.GetVelocity() != nullptr) {
    BeginPersistentWriteTarget(recorder, *scene_textures.GetVelocity());
  }

  recorder.RequireResourceState(
    scene_textures.GetGBufferNormal(), graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceState(
    scene_textures.GetGBufferMaterial(), graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceState(
    scene_textures.GetGBufferBaseColor(), graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceState(
    scene_textures.GetGBufferCustomData(), graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceState(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceState(scene_textures.GetSceneDepth(),
    config.early_z_pass_done ? graphics::ResourceStates::kDepthRead
                             : graphics::ResourceStates::kDepthWrite);
  if (scene_textures.GetVelocity() != nullptr) {
    recorder.RequireResourceState(
      *scene_textures.GetVelocity(), graphics::ResourceStates::kShaderResource);
  }
}

auto TransitionBasePassFinalStates(graphics::CommandRecorder& recorder,
  SceneTextures& scene_textures) -> void
{
  recorder.RequireResourceStateFinal(
    scene_textures.GetGBufferNormal(), graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceStateFinal(scene_textures.GetGBufferMaterial(),
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceStateFinal(scene_textures.GetGBufferBaseColor(),
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceStateFinal(scene_textures.GetGBufferCustomData(),
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceStateFinal(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  recorder.RequireResourceStateFinal(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  if (scene_textures.GetVelocity() != nullptr) {
    recorder.RequireResourceStateFinal(
      *scene_textures.GetVelocity(), graphics::ResourceStates::kShaderResource);
  }
}

auto SetViewportAndScissor(graphics::CommandRecorder& recorder,
  const RenderContext& ctx, const SceneTextures& scene_textures) -> void
{
  if (ctx.current_view.resolved_view != nullptr) {
    recorder.SetViewport(ctx.current_view.resolved_view->Viewport());
    recorder.SetScissors(ctx.current_view.resolved_view->Scissor());
    return;
  }

  const auto extent = scene_textures.GetExtent();
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

} // namespace

BasePassModule::BasePassModule(
  Renderer& renderer, const SceneTexturesConfig& scene_textures_config)
  : renderer_(renderer)
  , mesh_processor_(std::make_unique<BasePassMeshProcessor>(renderer))
{
  static_cast<void>(scene_textures_config);
}

BasePassModule::~BasePassModule() = default;

void BasePassModule::Execute(RenderContext& ctx, SceneTextures& scene_textures)
{
  has_published_base_pass_products_ = false;
  has_completed_velocity_for_dynamic_geometry_ = false;
  if (config_.shading_mode != ShadingMode::kDeferred) {
    return;
  }

  const auto has_current_view_payload = mesh_processor_ != nullptr
    && ctx.current_view.prepared_frame != nullptr
    && ctx.current_view.prepared_frame->IsValid();
  if (!has_current_view_payload) {
    return;
  }

  const auto writes_velocity
    = config_.write_velocity && scene_textures.GetVelocity() != nullptr;
  mesh_processor_->BuildDrawCommands(
    *ctx.current_view.prepared_frame, config_.shading_mode, writes_velocity);

  auto* gfx = renderer_.GetGraphics().get();
  if (gfx == nullptr || ctx.view_constants == nullptr) {
    return;
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key, "Vortex BasePass");
  if (!recorder) {
    return;
  }
  graphics::GpuEventScope stage_scope(*recorder, "Vortex.Stage9.BasePass",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);

  has_published_base_pass_products_ = true;
  has_completed_velocity_for_dynamic_geometry_
    = !config_.write_velocity || writes_velocity;

  BeginBasePassResourceTracking(*recorder, scene_textures, config_);
  const auto reverse_z = ctx.current_view.resolved_view == nullptr
    || ctx.current_view.resolved_view->ReverseZ();

  if (NeedsFramebufferRebuild(framebuffer_, scene_textures)) {
    framebuffer_ = gfx->CreateFramebuffer(
      BuildBasePassFramebuffer(scene_textures, config_.early_z_pass_done));
  }
  if (config_.early_z_pass_done
    && NeedsColorClearFramebufferRebuild(color_clear_framebuffer_, scene_textures)) {
    color_clear_framebuffer_
      = gfx->CreateFramebuffer(BuildBasePassColorClearFramebuffer(scene_textures));
  }

  recorder->FlushBarriers();

  if (!config_.early_z_pass_done) {
    recorder->ClearFramebuffer(*framebuffer_, std::nullopt,
      reverse_z ? 0.0F : 1.0F, 0U);
  } else if (color_clear_framebuffer_) {
    recorder->ClearFramebuffer(*color_clear_framebuffer_);
  }
  recorder->BindFrameBuffer(*framebuffer_);
  SetViewportAndScissor(*recorder, ctx, scene_textures);
  auto pipeline_desc
    = BuildBasePassPipelineDesc(scene_textures, config_, reverse_z);
  recorder->SetPipelineState(std::move(pipeline_desc));
  recorder->SetGraphicsRootConstantBufferView(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
    ctx.view_constants->GetGPUVirtualAddress());
  recorder->SetGraphicsRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    kInvalidShaderVisibleIndex.get(), 1U);

  for (const auto& draw_command : mesh_processor_->GetDrawCommands()) {
    recorder->SetGraphicsRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
      draw_command.draw_index, 0U);
    recorder->Draw(draw_command.is_indexed ? draw_command.index_count
                                           : draw_command.vertex_count,
      draw_command.instance_count, 0U, draw_command.start_instance);
  }

  TransitionBasePassFinalStates(*recorder, scene_textures);

  has_published_base_pass_products_ = true;
}

void BasePassModule::SetConfig(const BasePassConfig& config)
{
  config_ = config;
}

auto BasePassModule::HasPublishedBasePassProducts() const -> bool
{
  return has_published_base_pass_products_;
}

auto BasePassModule::HasCompletedVelocityForDynamicGeometry() const -> bool
{
  return has_completed_velocity_for_dynamic_geometry_;
}

} // namespace oxygen::vortex
