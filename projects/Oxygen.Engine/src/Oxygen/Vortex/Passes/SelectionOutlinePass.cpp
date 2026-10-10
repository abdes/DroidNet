//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec4.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/Internal/MeshRasterState.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/Passes/RenderPass.h>
#include <Oxygen/Vortex/Passes/SelectionOutlinePass.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/AcceptedDrawView.h>
#include <Oxygen/Vortex/Types/ViewOutline.h>

namespace oxygen::vortex {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr auto kMaskFormat = Format::kRG8UNorm;
  constexpr float kOutlinedLevel = 0.5F;
  constexpr float kActiveLevel = 1.0F;
  constexpr float kMaxRadius = 3.0F;

  struct alignas(16) SelectionOutlineMaskConstants {
    std::uint32_t scene_depth_srv { kInvalidShaderVisibleIndex.get() };
    float level { 0.0F };
    std::uint32_t reverse_z { 1U };
    std::uint32_t test_occlusion { 0U };
  };
  static_assert(sizeof(SelectionOutlineMaskConstants) == 16U);

  struct alignas(16) SelectionOutlineCompositeConstants {
    glm::vec4 color { 0.0F };
    glm::vec4 active_color { 0.0F };
    std::uint32_t mask_srv { kInvalidShaderVisibleIndex.get() };
    float radius { 2.0F };
    float occluded_alpha { 0.35F };
    std::uint32_t pad0 { 0U };
  };
  static_assert(sizeof(SelectionOutlineCompositeConstants) == 48U);

  auto TrackTexture(graphics::CommandRecorder& recorder,
    const graphics::Texture& texture) -> void
  {
    if (recorder.IsResourceTracked(texture)
      || recorder.AdoptKnownResourceState(texture)) {
      return;
    }
    auto initial = texture.GetDescriptor().initial_state;
    if (initial == graphics::ResourceStates::kUnknown
      || initial == graphics::ResourceStates::kUndefined) {
      initial = graphics::ResourceStates::kCommon;
    }
    recorder.BeginTrackingResourceState(texture, initial, false);
  }

  auto ToVec4(const graphics::Color& color) -> glm::vec4
  {
    return { color.r, color.g, color.b, color.a };
  }

  auto BuildMaskPipelineDesc(const bool alpha_test)
    -> graphics::GraphicsPipelineDesc
  {
    auto root_bindings = RenderPass::BuildRootBindings();
    auto defines = std::vector<graphics::ShaderDefine> {};
    if (alpha_test) {
      defines.push_back({ .name = "ALPHA_TEST", .value = "1" });
    }
    const auto max_blend = graphics::BlendTargetDesc {
      .blend_enable = true,
      .src_blend = graphics::BlendFactor::kOne,
      .dest_blend = graphics::BlendFactor::kOne,
      .blend_op = graphics::BlendOp::kMax,
      .src_blend_alpha = graphics::BlendFactor::kOne,
      .dest_blend_alpha = graphics::BlendFactor::kOne,
      .blend_op_alpha = graphics::BlendOp::kMax,
      .write_mask = graphics::ColorWriteMask::kAll,
    };
    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Vortex/Stages/DepthPrepass/DepthPrepass.hlsl",
        .entry_point = "DepthPrepassVS",
        .defines = defines,
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Vortex/Services/Editor/SelectionOutline.hlsl",
        .entry_point = "VortexSelectionOutlineMaskPS",
        .defines = defines,
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
      .SetDepthStencilState(graphics::DepthStencilStateDesc {
        .depth_test_enable = false,
        .depth_write_enable = false,
        .depth_func = graphics::CompareOp::kAlways,
        .stencil_enable = false,
      })
      .SetBlendState({ max_blend })
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .color_target_formats = { kMaskFormat },
        .depth_stencil_format = std::nullopt,
        .sample_count = 1U,
        .sample_quality = 0U,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName(alpha_test ? "Vortex.Stage20.SelectionOutlineMask.Masked"
                               : "Vortex.Stage20.SelectionOutlineMask")
      .Build();
  }

  auto BuildCompositePipelineDesc(const graphics::Texture& target)
    -> graphics::GraphicsPipelineDesc
  {
    auto root_bindings = RenderPass::BuildRootBindings();
    const auto alpha_blend = graphics::BlendTargetDesc {
      .blend_enable = true,
      .src_blend = graphics::BlendFactor::kSrcAlpha,
      .dest_blend = graphics::BlendFactor::kInvSrcAlpha,
      .blend_op = graphics::BlendOp::kAdd,
      .src_blend_alpha = graphics::BlendFactor::kZero,
      .dest_blend_alpha = graphics::BlendFactor::kOne,
      .blend_op_alpha = graphics::BlendOp::kAdd,
      .write_mask = graphics::ColorWriteMask::kAll,
    };
    const auto& desc = target.GetDescriptor();
    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Vortex/Services/Editor/SelectionOutlineComposite.hlsl",
        .entry_point = "VortexSelectionOutlineCompositeVS",
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Vortex/Services/Editor/SelectionOutlineComposite.hlsl",
        .entry_point = "VortexSelectionOutlineCompositePS",
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
      .SetDepthStencilState(graphics::DepthStencilStateDesc {
        .depth_test_enable = false,
        .depth_write_enable = false,
        .depth_func = graphics::CompareOp::kAlways,
        .stencil_enable = false,
      })
      .SetBlendState({ alpha_blend })
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .color_target_formats = { desc.format },
        .depth_stencil_format = std::nullopt,
        .sample_count = desc.sample_count,
        .sample_quality = desc.sample_quality,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName("Vortex.Stage20.SelectionOutlineComposite")
      .Build();
  }

  auto SetViewportAndScissor(graphics::CommandRecorder& recorder,
    const RenderContext& ctx, const std::uint32_t width,
    const std::uint32_t height) -> void
  {
    const auto& view = *ctx.current_view.resolved_view;
    const auto clamped = internal::ResolveClampedViewportState(
      view.Viewport(), view.Scissor(), width, height);
    recorder.SetViewport(clamped.viewport);
    recorder.SetScissors(clamped.scissors);
  }

} // namespace

struct SelectionOutlinePass::MaskConstants : SelectionOutlineMaskConstants { };
struct SelectionOutlinePass::CompositeConstants
  : SelectionOutlineCompositeConstants { };

SelectionOutlinePass::SelectionOutlinePass(Renderer& renderer)
  : renderer_(renderer)
{
}

SelectionOutlinePass::~SelectionOutlinePass()
{
  for (auto& [_, mask] : masks_) {
    RetireMask(mask);
  }
}

auto SelectionOutlinePass::Record(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const ViewOutline& outline,
  const Inputs& inputs) -> std::uint32_t
{
  const auto view_id = ctx.current_view.view_id;
  if (outline.IsEmpty() || inputs.target == nullptr || view_id == kInvalidViewId
    || ctx.current_view.resolved_view == nullptr
    || ctx.current_view.prepared_frame == nullptr
    || ctx.view_constants == nullptr
    || inputs.target->GetDescriptor().color_attachments.empty()) {
    return 0U;
  }
  const auto& prepared_frame = *ctx.current_view.prepared_frame;
  const auto metadata = prepared_frame.GetDrawMetadata();
  const auto sources = prepared_frame.draw_sources;
  if (metadata.empty() || sources.size() != metadata.size()) {
    return 0U;
  }

  // Active wins over outlined for a node listed in both.
  auto levels = std::unordered_map<scene::NodeHandle, float> {};
  levels.reserve(outline.nodes.size() + outline.active_nodes.size());
  for (const auto& node : outline.nodes) {
    levels.emplace(node, kOutlinedLevel);
  }
  for (const auto& node : outline.active_nodes) {
    levels.insert_or_assign(node, kActiveLevel);
  }

  struct OutlinedDraw {
    std::uint32_t draw_index { 0U };
    float level { 0.0F };
  };
  auto draws = std::vector<OutlinedDraw> {};
  const auto accept_mask = PassMask {
    PassMaskBit::kOpaque,
    PassMaskBit::kMasked,
    PassMaskBit::kTransparent,
  };
  for (const auto [draw, draw_index] :
    AcceptedDrawView(prepared_frame, accept_mask)) {
    if (!draw->flags.IsSet(PassMaskBit::kMainViewVisible)) {
      continue;
    }
    if (const auto it = levels.find(sources[draw_index].node);
      it != levels.end()) {
      draws.push_back({ .draw_index = draw_index, .level = it->second });
    }
  }
  if (draws.empty()) {
    return 0U;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return 0U;
  }
  const auto target_color
    = inputs.target->GetDescriptor().color_attachments.front().texture;
  if (target_color == nullptr) {
    return 0U;
  }
  const auto width = target_color->GetDescriptor().width;
  const auto height = target_color->GetDescriptor().height;
  auto* mask = EnsureMask(view_id, width, height);
  if (mask == nullptr) {
    return 0U;
  }

  BeginConstantsFrame(ctx);
  const auto test_occlusion
    = inputs.scene_depth != nullptr && inputs.scene_depth_srv.IsValid();
  const auto publish_mask_constants = [&](const float level) {
    auto constants = MaskConstants {};
    constants.scene_depth_srv = inputs.scene_depth_srv.get();
    constants.level = level;
    constants.reverse_z = inputs.reverse_z ? 1U : 0U;
    constants.test_occlusion = test_occlusion ? 1U : 0U;
    return mask_constants_->Publish(view_id, constants);
  };
  const auto outlined_constants = publish_mask_constants(kOutlinedLevel);
  const auto active_constants = publish_mask_constants(kActiveLevel);
  auto composite = CompositeConstants {};
  composite.color = ToVec4(outline.color);
  composite.active_color = ToVec4(outline.active_color);
  composite.mask_srv = mask->srv.get();
  composite.radius = std::clamp(outline.radius, 0.5F, kMaxRadius);
  composite.occluded_alpha = std::clamp(outline.occluded_alpha, 0.0F, 1.0F);
  const auto composite_constants
    = composite_constants_->Publish(view_id, composite);
  if (!outlined_constants.IsValid() || !active_constants.IsValid()
    || !composite_constants.IsValid()) {
    return 0U;
  }

  graphics::GpuEventScope pass_scope(recorder,
    "Vortex.Stage20.SelectionOutline",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);
  const auto root_constants
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  const auto view_constants
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);

  // Mask: every outlined draw, MAX-blended.
  TrackTexture(recorder, *mask->texture);
  recorder.RequireResourceState(
    *mask->texture, graphics::ResourceStates::kRenderTarget);
  if (test_occlusion) {
    TrackTexture(recorder, *inputs.scene_depth);
    recorder.RequireResourceState(
      *inputs.scene_depth, graphics::ResourceStates::kShaderResource);
  }
  recorder.FlushBarriers();
  recorder.BindFrameBuffer(*mask->framebuffer);
  recorder.ClearFramebuffer(*mask->framebuffer,
    std::vector<std::optional<graphics::Color>> {
      graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F } });
  SetViewportAndScissor(recorder, ctx, width, height);
  auto current_alpha_test = std::optional<bool> {};
  for (const auto& draw : draws) {
    const auto raster
      = internal::ResolveMeshRasterState(metadata, draw.draw_index);
    if (current_alpha_test != raster.alpha_test) {
      recorder.SetPipelineState(BuildMaskPipelineDesc(raster.alpha_test));
      recorder.SetGraphicsRootConstantBufferView(
        view_constants, ctx.view_constants->GetGPUVirtualAddress());
      current_alpha_test = raster.alpha_test;
    }
    const auto& dm = metadata[draw.draw_index];
    recorder.SetGraphicsRoot32BitConstant(root_constants,
      (draw.level == kActiveLevel ? active_constants : outlined_constants)
        .get(),
      1U);
    recorder.Draw(dm.is_indexed != 0U ? dm.index_count : dm.vertex_count,
      (std::max)(dm.instance_count, 1U), 0U, draw.draw_index);
  }

  // Composite: the band around the mask over the view output.
  auto output
    = gfx->CreateFramebuffer(graphics::FramebufferDesc {}.AddColorAttachment(
      inputs.target->GetDescriptor().color_attachments.front()));
  if (!output) {
    return 0U;
  }
  TrackTexture(recorder, *target_color);
  recorder.RequireResourceState(
    *mask->texture, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *target_color, graphics::ResourceStates::kRenderTarget);
  recorder.FlushBarriers();
  recorder.BindFrameBuffer(*output);
  SetViewportAndScissor(recorder, ctx, width, height);
  recorder.SetPipelineState(BuildCompositePipelineDesc(*target_color));
  recorder.SetGraphicsRootConstantBufferView(
    view_constants, ctx.view_constants->GetGPUVirtualAddress());
  recorder.SetGraphicsRoot32BitConstant(root_constants, 0U, 0U);
  recorder.SetGraphicsRoot32BitConstant(
    root_constants, composite_constants.get(), 1U);
  recorder.Draw(3U, 1U, 0U, 0U);
  recorder.RequireResourceState(
    *target_color, graphics::ResourceStates::kRenderTarget);
  if (test_occlusion) {
    recorder.RequireResourceState(
      *inputs.scene_depth, graphics::ResourceStates::kDepthRead);
  }
  gfx->RegisterDeferredRelease(std::move(output));
  return static_cast<std::uint32_t>(draws.size());
}

auto SelectionOutlinePass::RemoveView(const ViewId view_id) -> void
{
  if (const auto it = masks_.find(view_id); it != masks_.end()) {
    RetireMask(it->second);
    masks_.erase(it);
  }
}

auto SelectionOutlinePass::EnsureMask(const ViewId view_id,
  const std::uint32_t width, const std::uint32_t height) -> ViewMask*
{
  auto& mask = masks_[view_id];
  if (mask.texture != nullptr && mask.texture->GetDescriptor().width == width
    && mask.texture->GetDescriptor().height == height) {
    return &mask;
  }
  RetireMask(mask);

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr || width == 0U || height == 0U) {
    return nullptr;
  }
  auto desc = graphics::TextureDesc {};
  desc.width = width;
  desc.height = height;
  desc.format = kMaskFormat;
  desc.texture_type = TextureType::kTexture2D;
  desc.is_render_target = true;
  desc.is_shader_resource = true;
  desc.use_clear_value = true;
  desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  desc.initial_state = graphics::ResourceStates::kCommon;
  desc.debug_name = "Vortex.SelectionOutlineMask";
  mask.texture = gfx->CreateTexture(desc);
  if (mask.texture == nullptr) {
    return nullptr;
  }

  auto& registry = gfx->GetResourceRegistry();
  registry.Register(mask.texture);
  auto& allocator = gfx->GetDescriptorAllocator();
  auto handle = allocator.AllocateBindless(bindless::generated::kTexturesDomain,
    graphics::ResourceViewType::kTexture_SRV);
  if (!handle.IsValid()) {
    LOG_F(ERROR, "SelectionOutlinePass: no descriptor for the outline mask");
    RetireMask(mask);
    return nullptr;
  }
  mask.srv = allocator.GetShaderVisibleIndex(handle);
  registry.RegisterView(*mask.texture, std::move(handle),
    graphics::TextureViewDescription {
      .view_type = graphics::ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = kMaskFormat,
      .dimension = TextureType::kTexture2D,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      .is_read_only_dsv = false,
    });
  mask.framebuffer = gfx->CreateFramebuffer(
    graphics::FramebufferDesc {}.AddColorAttachment(mask.texture));
  return &mask;
}

auto SelectionOutlinePass::RetireMask(ViewMask& mask) -> void
{
  auto gfx = renderer_.GetGraphics();
  if (gfx != nullptr) {
    if (mask.framebuffer) {
      gfx->RegisterDeferredRelease(std::move(mask.framebuffer));
    }
    if (mask.texture) {
      auto* registry = &gfx->GetResourceRegistry();
      gfx->GetDeferredReclaimer().RegisterDeferredAction(
        [registry, texture = std::move(mask.texture)] mutable -> void {
          if (registry->Contains(*texture)) {
            registry->UnRegisterResource(*texture);
          }
          texture.reset();
        });
    }
  }
  mask = {};
}

auto SelectionOutlinePass::BeginConstantsFrame(const RenderContext& ctx) -> void
{
  if (!mask_constants_) {
    auto gfx = renderer_.GetGraphics();
    CHECK_NOTNULL_F(gfx.get());
    mask_constants_
      = std::make_unique<internal::PerViewStructuredPublisher<MaskConstants>>(
        observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
        observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
        "Vortex.Stage20.SelectionOutline.MaskConstants");
    composite_constants_ = std::make_unique<
      internal::PerViewStructuredPublisher<CompositeConstants>>(
      observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
      observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
      "Vortex.Stage20.SelectionOutline.CompositeConstants");
  }
  if (constants_frame_ != ctx.frame_sequence) {
    mask_constants_->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    composite_constants_->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    constants_frame_ = ctx.frame_sequence;
  }
}

} // namespace oxygen::vortex
