//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <limits>
#include <vector>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h>
#include <Oxygen/Vortex/Types/PassMask.h>

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
        table.view_type = RangeTypeToViewType(
          static_cast<bindless_d3d12::RangeType>(range.range_type));
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

auto AddBooleanDefine(const bool enabled, std::string_view name,
  std::vector<graphics::ShaderDefine>& defines) -> void
{
  if (enabled) {
    defines.push_back(
      graphics::ShaderDefine { .name = std::string(name), .value = "1" });
  }
}

auto AdoptOrBeginPersistentState(graphics::CommandRecorder& recorder,
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

auto BuildDepthPrepassFramebuffer(
  SceneTextures& scene_textures, const bool writes_velocity)
  -> graphics::FramebufferDesc
{
  auto desc = graphics::FramebufferDesc {};
  if (writes_velocity && scene_textures.GetVelocity() != nullptr) {
    desc.AddColorAttachment({
      .texture = scene_textures.GetVelocityResource(),
      .format = scene_textures.GetVelocity()->GetDescriptor().format,
    });
  }
  desc.SetDepthAttachment({
    .texture = scene_textures.GetSceneDepthResource(),
    .format = scene_textures.GetSceneDepth().GetDescriptor().format,
    .is_read_only = false,
  });
  return desc;
}

auto NeedsFramebufferRebuild(
  const std::shared_ptr<graphics::Framebuffer>& framebuffer,
  const SceneTextures& scene_textures, const bool writes_velocity) -> bool
{
  if (!framebuffer) {
    return true;
  }

  const auto& desc = framebuffer->GetDescriptor();
  const auto expected_color_attachments = writes_velocity ? 1U : 0U;
  if (desc.color_attachments.size() != expected_color_attachments
    || desc.depth_attachment.texture.get()
      != scene_textures.GetSceneDepthResource().get()) {
    return true;
  }

  if (!writes_velocity) {
    return false;
  }

  return scene_textures.GetVelocity() == nullptr
    || desc.color_attachments[0].texture.get()
      != scene_textures.GetVelocityResource().get();
}

auto BuildDepthPrepassPipelineDesc(const SceneTextures& scene_textures,
  const bool writes_velocity, const bool alpha_test, const bool reverse_z)
  -> graphics::GraphicsPipelineDesc
{
  auto root_bindings = BuildVortexRootBindings();

  auto defines = std::vector<graphics::ShaderDefine> {};
  AddBooleanDefine(writes_velocity, "HAS_VELOCITY", defines);
  AddBooleanDefine(alpha_test, "ALPHA_TEST", defines);

  auto blend_targets = std::vector<graphics::BlendTargetDesc> {};
  if (writes_velocity && scene_textures.GetVelocity() != nullptr) {
    blend_targets.push_back(graphics::BlendTargetDesc {
      .blend_enable = false,
      .write_mask = graphics::ColorWriteMask::kAll,
    });
  }

  auto color_formats = std::vector<Format> {};
  if (writes_velocity && scene_textures.GetVelocity() != nullptr) {
    color_formats.push_back(scene_textures.GetVelocity()->GetDescriptor().format);
  }

  return graphics::GraphicsPipelineDesc::Builder {}
    .SetVertexShader(graphics::ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "Vortex/Stages/DepthPrepass/DepthPrepass.hlsl",
      .entry_point = "DepthPrepassVS",
      .defines = defines,
    })
    .SetPixelShader(graphics::ShaderRequest {
      .stage = ShaderType::kPixel,
      .source_path = "Vortex/Stages/DepthPrepass/DepthPrepass.hlsl",
      .entry_point = "DepthPrepassPS",
      .defines = defines,
    })
    .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
    .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
    .SetDepthStencilState(graphics::DepthStencilStateDesc {
      .depth_test_enable = true,
      .depth_write_enable = true,
      .depth_func = reverse_z ? graphics::CompareOp::kGreaterOrEqual
                              : graphics::CompareOp::kLessOrEqual,
      .stencil_enable = false,
    })
    .SetBlendState(std::move(blend_targets))
    .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
      .color_target_formats = std::move(color_formats),
      .depth_stencil_format = scene_textures.GetSceneDepth().GetDescriptor().format,
      .sample_count = scene_textures.GetSceneDepth().GetDescriptor().sample_count,
      .sample_quality = scene_textures.GetSceneDepth().GetDescriptor().sample_quality,
    })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size()))
    .SetDebugName(alpha_test
        ? (writes_velocity ? "Vortex.DepthPrepass.MaskedVelocity"
                           : "Vortex.DepthPrepass.Masked")
        : (writes_velocity ? "Vortex.DepthPrepass.OpaqueVelocity"
                           : "Vortex.DepthPrepass.Opaque"))
    .Build();
}

auto BeginDepthPrepassResourceTracking(graphics::CommandRecorder& recorder,
  SceneTextures& scene_textures, const bool writes_velocity) -> void
{
  AdoptOrBeginPersistentState(recorder, scene_textures.GetSceneDepth());
  AdoptOrBeginPersistentState(recorder, scene_textures.GetPartialDepth());
  recorder.RequireResourceState(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthWrite);
  if (writes_velocity && scene_textures.GetVelocity() != nullptr) {
    AdoptOrBeginPersistentState(recorder, *scene_textures.GetVelocity());
    recorder.RequireResourceState(
      *scene_textures.GetVelocity(), graphics::ResourceStates::kRenderTarget);
  }
}

auto TransitionDepthPrepassFinalStates(graphics::CommandRecorder& recorder,
  SceneTextures& scene_textures, const bool writes_velocity) -> void
{
  recorder.RequireResourceStateFinal(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  recorder.RequireResourceStateFinal(
    scene_textures.GetPartialDepth(), graphics::ResourceStates::kShaderResource);
  if (writes_velocity && scene_textures.GetVelocity() != nullptr) {
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

auto IsMaskedDraw(
  const PreparedSceneFrame& prepared_frame, const DrawCommand& draw_command)
  -> bool
{
  const auto metadata = prepared_frame.GetDrawMetadata();
  return draw_command.draw_index < metadata.size()
    && metadata[draw_command.draw_index].flags.IsSet(PassMaskBit::kMasked);
}

auto CopySceneDepthToPartialDepth(graphics::CommandRecorder& recorder,
  SceneTextures& scene_textures) -> void
{
  recorder.RequireResourceState(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    scene_textures.GetPartialDepth(), graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();

  recorder.CopyTexture(scene_textures.GetSceneDepth(), graphics::TextureSlice {},
    graphics::TextureSubResourceSet::EntireTexture(),
    scene_textures.GetPartialDepth(), graphics::TextureSlice {},
    graphics::TextureSubResourceSet::EntireTexture());
}

} // namespace

DepthPrepassModule::DepthPrepassModule(
  Renderer& renderer, const SceneTexturesConfig& scene_textures_config)
  : renderer_(renderer)
  , mesh_processor_(std::make_unique<DepthPrepassMeshProcessor>(renderer))
{
  static_cast<void>(scene_textures_config);
}

DepthPrepassModule::~DepthPrepassModule() = default;

void DepthPrepassModule::Execute(
  RenderContext& ctx, SceneTextures& scene_textures)
{
  has_published_depth_products_ = false;
  completeness_ = DepthPrePassCompleteness::kIncomplete;
  if (config_.mode == DepthPrePassMode::kDisabled) {
    completeness_ = DepthPrePassCompleteness::kDisabled;
    return;
  }

  const auto has_current_view_payload = mesh_processor_ != nullptr
    && ctx.current_view.prepared_frame != nullptr
    && ctx.current_view.prepared_frame->IsValid();
  const auto writes_velocity
    = config_.write_velocity && scene_textures.GetVelocity() != nullptr;
  if (!has_current_view_payload || ctx.view_constants == nullptr) {
    return;
  }

  mesh_processor_->BuildDrawCommands(
    *ctx.current_view.prepared_frame, ctx.current_view.resolved_view.get(),
    config_.mode == DepthPrePassMode::kOpaqueAndMasked);

  auto* gfx = renderer_.GetGraphics().get();
  if (gfx == nullptr) {
    return;
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key, "Vortex DepthPrepass");
  if (!recorder) {
    return;
  }
  graphics::GpuEventScope stage_scope(*recorder, "Vortex.Stage3.DepthPrepass",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);

  const auto reverse_z = ctx.current_view.resolved_view == nullptr
    || ctx.current_view.resolved_view->ReverseZ();
  BeginDepthPrepassResourceTracking(*recorder, scene_textures, writes_velocity);

  auto& framebuffer = writes_velocity ? depth_velocity_framebuffer_
                                      : depth_framebuffer_;
  if (NeedsFramebufferRebuild(framebuffer, scene_textures, writes_velocity)) {
    framebuffer = gfx->CreateFramebuffer(
      BuildDepthPrepassFramebuffer(scene_textures, writes_velocity));
  }

  recorder->FlushBarriers();
  recorder->ClearFramebuffer(
    *framebuffer, std::nullopt, reverse_z ? 0.0F : 1.0F, 0U);
  recorder->BindFrameBuffer(*framebuffer);
  SetViewportAndScissor(*recorder, ctx, scene_textures);
  const auto root_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  const auto view_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);

  auto current_alpha_test = std::optional<bool> {};
  for (const auto& draw_command : mesh_processor_->GetDrawCommands()) {
    const auto alpha_test
      = IsMaskedDraw(*ctx.current_view.prepared_frame, draw_command);
    if (!current_alpha_test.has_value()
      || current_alpha_test.value() != alpha_test) {
      recorder->SetPipelineState(BuildDepthPrepassPipelineDesc(
        scene_textures, writes_velocity, alpha_test, reverse_z));
      recorder->SetGraphicsRootConstantBufferView(
        view_constants_param, ctx.view_constants->GetGPUVirtualAddress());
      recorder->SetGraphicsRoot32BitConstant(
        root_constants_param, kInvalidShaderVisibleIndex.get(), 1U);
      current_alpha_test = alpha_test;
    }

    recorder->SetGraphicsRoot32BitConstant(
      root_constants_param, draw_command.draw_index, 0U);
    recorder->Draw(draw_command.index_count, draw_command.instance_count, 0U,
      draw_command.start_instance);
  }

  CopySceneDepthToPartialDepth(*recorder, scene_textures);
  TransitionDepthPrepassFinalStates(*recorder, scene_textures, writes_velocity);

  completeness_ = DepthPrePassCompleteness::kComplete;
  has_published_depth_products_ = true;
}

void DepthPrepassModule::SetConfig(const DepthPrepassConfig& config)
{
  config_ = config;
}

auto DepthPrepassModule::GetCompleteness() const -> DepthPrePassCompleteness
{
  return completeness_;
}

auto DepthPrepassModule::HasPublishedDepthProducts() const -> bool
{
  return has_published_depth_products_;
}

} // namespace oxygen::vortex
