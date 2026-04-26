//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Translucency/TranslucencyMeshProcessor.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Translucency/TranslucencyModule.h>

namespace oxygen::vortex {

struct TranslucencyPipelineCacheKey {
  Format scene_color_format { Format::kUnknown };
  Format depth_format { Format::kUnknown };
  std::uint32_t sample_count { 1U };
  std::uint32_t sample_quality { 0U };
  bool reverse_z { true };

  auto operator==(const TranslucencyPipelineCacheKey&) const -> bool = default;
};

struct TranslucencyPipelineCacheEntry {
  TranslucencyPipelineCacheKey key {};
  graphics::GraphicsPipelineDesc desc;
};

struct TranslucencyPipelineCache {
  TranslucencyPipelineCache();

  std::vector<graphics::RootBindingItem> root_bindings {};
  std::vector<TranslucencyPipelineCacheEntry> entries {};
};

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

    for (std::uint32_t index = 0U; index < bindless_d3d12::kRootParamTableCount;
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
          table.count = range.num_descriptors
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
        binding.data = graphics::PushConstantsBinding {
          .size = desc.constants_count,
        };
        break;
      }

      bindings.emplace_back(binding);
    }

    return bindings;
  }

  auto BuildTranslucencyFramebuffer(SceneTextures& scene_textures)
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

  auto NeedsFramebufferRebuild(
    const std::shared_ptr<graphics::Framebuffer>& framebuffer,
    const SceneTextures& scene_textures) -> bool
  {
    if (!framebuffer) {
      return true;
    }

    const auto& desc = framebuffer->GetDescriptor();
    return desc.color_attachments.size() != 1U
      || desc.color_attachments[0].texture.get()
      != scene_textures.GetSceneColorResource().get()
      || desc.depth_attachment.texture.get()
      != scene_textures.GetSceneDepthResource().get()
      || !desc.depth_attachment.is_read_only;
  }

  auto MakeAlphaBlendTarget() -> graphics::BlendTargetDesc
  {
    return {
      .blend_enable = true,
      .src_blend = graphics::BlendFactor::kSrcAlpha,
      .dest_blend = graphics::BlendFactor::kInvSrcAlpha,
      .blend_op = graphics::BlendOp::kAdd,
      .src_blend_alpha = graphics::BlendFactor::kZero,
      .dest_blend_alpha = graphics::BlendFactor::kOne,
      .blend_op_alpha = graphics::BlendOp::kAdd,
      .write_mask = graphics::ColorWriteMask::kAll,
    };
  }

  auto BuildTranslucencyPipelineDesc(const SceneTextures& scene_textures,
    const bool reverse_z,
    std::span<const graphics::RootBindingItem> root_bindings)
    -> graphics::GraphicsPipelineDesc
  {
    auto pixel_defines = std::vector<graphics::ShaderDefine> {
      graphics::ShaderDefine {
        .name = "OXYGEN_HDR_OUTPUT",
        .value = "1",
      },
    };

    const auto depth_state = graphics::DepthStencilStateDesc {
      .depth_test_enable = true,
      .depth_write_enable = false,
      .depth_func = reverse_z ? graphics::CompareOp::kGreaterOrEqual
                              : graphics::CompareOp::kLessOrEqual,
      .stencil_enable = false,
    };

    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Vortex/Stages/Translucency/ForwardMesh_VS.hlsl",
        .entry_point = "VS",
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Vortex/Stages/Translucency/ForwardMesh_PS.hlsl",
        .entry_point = "PS",
        .defines = std::move(pixel_defines),
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
      .SetDepthStencilState(depth_state)
      .SetBlendState({ MakeAlphaBlendTarget() })
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .color_target_formats = std::vector<
          Format> { scene_textures.GetSceneColor().GetDescriptor().format },
        .depth_stencil_format
        = scene_textures.GetSceneDepth().GetDescriptor().format,
        .sample_count
        = scene_textures.GetSceneColor().GetDescriptor().sample_count,
        .sample_quality
        = scene_textures.GetSceneColor().GetDescriptor().sample_quality,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName("Vortex.Translucency.Standard")
      .Build();
  }

  auto MakePipelineCacheKey(const SceneTextures& scene_textures,
    const bool reverse_z) -> TranslucencyPipelineCacheKey
  {
    const auto& scene_color_desc
      = scene_textures.GetSceneColor().GetDescriptor();
    const auto& scene_depth_desc
      = scene_textures.GetSceneDepth().GetDescriptor();
    return TranslucencyPipelineCacheKey {
      .scene_color_format = scene_color_desc.format,
      .depth_format = scene_depth_desc.format,
      .sample_count = scene_color_desc.sample_count,
      .sample_quality = scene_color_desc.sample_quality,
      .reverse_z = reverse_z,
    };
  }

  auto GetCachedTranslucencyPipelineDesc(TranslucencyPipelineCache& cache,
    const SceneTextures& scene_textures, const bool reverse_z)
    -> const graphics::GraphicsPipelineDesc&
  {
    const auto key = MakePipelineCacheKey(scene_textures, reverse_z);
    const auto found = std::ranges::find_if(
      cache.entries, [&](const TranslucencyPipelineCacheEntry& entry) {
        return entry.key == key;
      });
    if (found != cache.entries.end()) {
      return found->desc;
    }

    auto desc = BuildTranslucencyPipelineDesc(scene_textures, reverse_z,
      std::span<const graphics::RootBindingItem>(
        cache.root_bindings.data(), cache.root_bindings.size()));
    cache.entries.push_back(TranslucencyPipelineCacheEntry {
      .key = key,
      .desc = std::move(desc),
    });
    return cache.entries.back().desc;
  }

  auto BeginPersistentWriteTarget(
    graphics::CommandRecorder& recorder, graphics::Texture& texture) -> void
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

  auto SetViewportAndScissor(graphics::CommandRecorder& recorder,
    const RenderContext& ctx, const SceneTextures& scene_textures) -> void
  {
    const auto extent = scene_textures.GetExtent();
    if (ctx.current_view.resolved_view != nullptr) {
      const auto clamped
        = oxygen::vortex::internal::ResolveClampedViewportState(
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

} // namespace

TranslucencyPipelineCache::TranslucencyPipelineCache()
  : root_bindings(BuildVortexRootBindings())
{
}

TranslucencyModule::TranslucencyModule(Renderer& renderer)
  : renderer_(renderer)
  , mesh_processor_(std::make_unique<TranslucencyMeshProcessor>(renderer))
  , pipeline_cache_(std::make_unique<TranslucencyPipelineCache>())
{
}

TranslucencyModule::~TranslucencyModule() = default;

auto TranslucencyModule::Execute(RenderContext& ctx,
  SceneTextures& scene_textures) -> TranslucencyExecutionResult
{
  auto result = TranslucencyExecutionResult {
    .requested = mesh_processor_ != nullptr
      && ctx.current_view.prepared_frame != nullptr
      && ctx.current_view.prepared_frame->IsValid(),
  };
  if (!result.requested) {
    result.skip_reason = TranslucencySkipReason::kNotRequested;
    return result;
  }
  result.skip_reason = TranslucencySkipReason::kNone;

  mesh_processor_->BuildDrawCommands(
    *ctx.current_view.prepared_frame, ctx.current_view.resolved_view.get());
  result.draw_count
    = static_cast<std::uint32_t>(mesh_processor_->GetDrawCommands().size());
  if (result.draw_count == 0U) {
    result.skip_reason = TranslucencySkipReason::kNoDraws;
    return result;
  }

  auto* gfx = renderer_.GetGraphics().get();
  if (gfx == nullptr) {
    LOG_F(WARNING, "Vortex.Stage18.Translucency skipped: graphics unavailable");
    result.skip_reason = TranslucencySkipReason::kMissingGraphics;
    return result;
  }
  if (ctx.view_constants == nullptr) {
    LOG_F(WARNING,
      "Vortex.Stage18.Translucency skipped: view constants unavailable");
    result.skip_reason = TranslucencySkipReason::kMissingViewConstants;
    return result;
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key, "Vortex Translucency");
  if (!recorder) {
    LOG_F(WARNING,
      "Vortex.Stage18.Translucency skipped: command recorder unavailable");
    result.skip_reason = TranslucencySkipReason::kRecorderUnavailable;
    return result;
  }

  graphics::GpuEventScope stage_scope(*recorder, "Vortex.Stage18.Translucency",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);

  if (NeedsFramebufferRebuild(framebuffer_, scene_textures)) {
    framebuffer_
      = gfx->CreateFramebuffer(BuildTranslucencyFramebuffer(scene_textures));
  }

  BeginPersistentWriteTarget(*recorder, scene_textures.GetSceneColor());
  BeginPersistentWriteTarget(*recorder, scene_textures.GetSceneDepth());
  recorder->RequireResourceState(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  recorder->RequireResourceState(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  recorder->FlushBarriers();
  recorder->BindFrameBuffer(*framebuffer_);
  SetViewportAndScissor(*recorder, ctx, scene_textures);

  const auto reverse_z = ctx.current_view.resolved_view == nullptr
    || ctx.current_view.resolved_view->ReverseZ();
  recorder->SetPipelineState(GetCachedTranslucencyPipelineDesc(
    *pipeline_cache_, scene_textures, reverse_z));

  const auto root_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  const auto view_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);
  recorder->SetGraphicsRootConstantBufferView(
    view_constants_param, ctx.view_constants->GetGPUVirtualAddress());
  recorder->SetGraphicsRoot32BitConstant(
    root_constants_param, kInvalidShaderVisibleIndex.get(), 1U);

  for (const auto& draw_command : mesh_processor_->GetDrawCommands()) {
    recorder->SetGraphicsRoot32BitConstant(
      root_constants_param, draw_command.draw_index, 0U);
    recorder->Draw(draw_command.is_indexed ? draw_command.index_count
                                           : draw_command.vertex_count,
      draw_command.instance_count, 0U, draw_command.start_instance);
  }

  recorder->RequireResourceStateFinal(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  recorder->RequireResourceStateFinal(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);

  result.executed = true;
  result.skip_reason = TranslucencySkipReason::kNone;
  return result;
}

} // namespace oxygen::vortex
