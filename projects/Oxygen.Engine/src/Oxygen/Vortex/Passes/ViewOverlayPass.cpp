//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <glm/vec2.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/Passes/RenderPass.h>
#include <Oxygen/Vortex/Passes/ViewOverlayPass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/ViewOverlay.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr std::uint32_t kVerticesPerLine = 6U;

  struct alignas(16) ViewOverlayConstants {
    std::uint32_t triangles_srv { kInvalidShaderVisibleIndex.get() };
    std::uint32_t lines_srv { kInvalidShaderVisibleIndex.get() };
    std::uint32_t scene_depth_srv { kInvalidShaderVisibleIndex.get() };
    std::uint32_t test_occlusion { 0U };
    glm::vec2 viewport_size { 1.0F };
    float occluded_alpha { 1.0F };
    std::uint32_t pad0 { 0U };
  };
  static_assert(sizeof(ViewOverlayConstants) == 32U);

  enum class Primitive : std::uint8_t { kTriangles, kLines };

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

  auto BuildPipelineDesc(const graphics::Texture& target,
    const Primitive primitive) -> graphics::GraphicsPipelineDesc
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
    const bool lines = primitive == Primitive::kLines;
    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Vortex/Services/Editor/ViewOverlay.hlsl",
        .entry_point
        = lines ? "VortexViewOverlayLineVS" : "VortexViewOverlayTriangleVS",
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Vortex/Services/Editor/ViewOverlay.hlsl",
        .entry_point = "VortexViewOverlayPS",
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
      .SetDebugName(lines ? "Vortex.Stage20.ViewOverlay.Lines"
                          : "Vortex.Stage20.ViewOverlay.Triangles")
      .Build();
  }

  template <typename Element>
  auto Upload(upload::TransientStructuredBuffer& buffer,
    const std::vector<Element>& elements) -> ShaderVisibleIndex
  {
    if (elements.empty()) {
      return kInvalidShaderVisibleIndex;
    }
    auto allocation
      = buffer.Allocate(static_cast<std::uint32_t>(elements.size()));
    if (!allocation || !allocation->srv.IsValid()
      || !allocation->TryWriteRange(std::span { elements })) {
      LOG_F(WARNING, "ViewOverlayPass: no upload space for {} elements",
        elements.size());
      return kInvalidShaderVisibleIndex;
    }
    return allocation->srv;
  }

} // namespace

struct ViewOverlayPass::Constants : ViewOverlayConstants { };

ViewOverlayPass::ViewOverlayPass(Renderer& renderer)
  : renderer_(renderer)
{
}

ViewOverlayPass::~ViewOverlayPass() = default;

auto ViewOverlayPass::Record(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const ViewOverlay& overlay,
  const Inputs& inputs) -> std::uint32_t
{
  const auto view_id = ctx.current_view.view_id;
  if (overlay.IsEmpty() || inputs.target == nullptr || view_id == kInvalidViewId
    || ctx.current_view.resolved_view == nullptr
    || ctx.view_constants == nullptr
    || inputs.target->GetDescriptor().color_attachments.empty()) {
    return 0U;
  }
  auto gfx = renderer_.GetGraphics();
  const auto target_color
    = inputs.target->GetDescriptor().color_attachments.front().texture;
  if (gfx == nullptr || target_color == nullptr) {
    return 0U;
  }

  BeginFrame(ctx);
  const auto& view = *ctx.current_view.resolved_view;
  const auto test_occlusion
    = inputs.scene_depth != nullptr && inputs.scene_depth_srv.IsValid();

  struct LayerDraw {
    ShaderVisibleIndex constants { kInvalidShaderVisibleIndex };
    std::uint32_t triangle_vertices { 0U };
    std::uint32_t line_vertices { 0U };
  };
  const auto prepare
    = [&](const ViewOverlayLayer& layer, const bool occludable) -> LayerDraw {
    if (layer.IsEmpty()) {
      return {};
    }
    auto constants = Constants {};
    constants.triangles_srv = Upload(*triangles_, layer.triangles).get();
    constants.lines_srv = Upload(*lines_, layer.lines).get();
    constants.scene_depth_srv = inputs.scene_depth_srv.get();
    constants.test_occlusion = (occludable && test_occlusion) ? 1U : 0U;
    constants.viewport_size
      = glm::vec2 { view.Viewport().width, view.Viewport().height };
    constants.occluded_alpha = overlay.occluded_alpha;
    auto draw = LayerDraw {};
    draw.constants = constants_->Publish(view_id, constants);
    if (constants.triangles_srv != kInvalidShaderVisibleIndex.get()) {
      draw.triangle_vertices
        = static_cast<std::uint32_t>(layer.triangles.size());
    }
    if (constants.lines_srv != kInvalidShaderVisibleIndex.get()) {
      draw.line_vertices
        = static_cast<std::uint32_t>(layer.lines.size()) * kVerticesPerLine;
    }
    return draw;
  };
  const auto layers = std::array {
    prepare(overlay.scene, true),
    prepare(overlay.top, false),
  };

  auto output
    = gfx->CreateFramebuffer(graphics::FramebufferDesc {}.AddColorAttachment(
      inputs.target->GetDescriptor().color_attachments.front()));
  if (!output) {
    return 0U;
  }

  graphics::GpuEventScope pass_scope(recorder, "Vortex.Stage20.ViewOverlay",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);
  const auto root_constants
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  const auto view_constants
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);

  TrackTexture(recorder, *target_color);
  recorder.RequireResourceState(
    *target_color, graphics::ResourceStates::kRenderTarget);
  if (test_occlusion) {
    TrackTexture(recorder, *inputs.scene_depth);
    recorder.RequireResourceState(
      *inputs.scene_depth, graphics::ResourceStates::kShaderResource);
  }
  recorder.FlushBarriers();
  recorder.BindFrameBuffer(*output);
  const auto& target_desc = target_color->GetDescriptor();
  const auto clamped = internal::ResolveClampedViewportState(
    view.Viewport(), view.Scissor(), target_desc.width, target_desc.height);
  recorder.SetViewport(clamped.viewport);
  recorder.SetScissors(clamped.scissors);

  auto draws = std::uint32_t { 0U };
  const auto record = [&](const Primitive primitive, const LayerDraw& layer,
                        const std::uint32_t vertex_count) {
    if (vertex_count == 0U || !layer.constants.IsValid()) {
      return;
    }
    recorder.SetPipelineState(BuildPipelineDesc(*target_color, primitive));
    recorder.SetGraphicsRootConstantBufferView(
      view_constants, ctx.view_constants->GetGPUVirtualAddress());
    recorder.SetGraphicsRoot32BitConstant(root_constants, 0U, 0U);
    recorder.SetGraphicsRoot32BitConstant(
      root_constants, layer.constants.get(), 1U);
    recorder.Draw(vertex_count, 1U, 0U, 0U);
    ++draws;
  };
  for (const auto& layer : layers) {
    record(Primitive::kTriangles, layer, layer.triangle_vertices);
    record(Primitive::kLines, layer, layer.line_vertices);
  }

  if (test_occlusion) {
    recorder.RequireResourceState(
      *inputs.scene_depth, graphics::ResourceStates::kDepthRead);
  }
  gfx->RegisterDeferredRelease(std::move(output));
  return draws;
}

auto ViewOverlayPass::BeginFrame(const RenderContext& ctx) -> void
{
  if (!constants_) {
    auto gfx = renderer_.GetGraphics();
    CHECK_NOTNULL_F(gfx.get());
    const auto gfx_ptr = observer_ptr { gfx.get() };
    const auto transfers
      = observer_ptr { &renderer_.GetInlineTransfersCoordinator() };
    constants_
      = std::make_unique<internal::PerViewStructuredPublisher<Constants>>(
        gfx_ptr, renderer_.GetStagingProvider(), transfers,
        "Vortex.Stage20.ViewOverlay.Constants");
    triangles_ = std::make_unique<upload::TransientStructuredBuffer>(gfx_ptr,
      renderer_.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(ViewOverlayVertex)), transfers,
      "Vortex.Stage20.ViewOverlay.Triangles");
    lines_ = std::make_unique<upload::TransientStructuredBuffer>(gfx_ptr,
      renderer_.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(ViewOverlayLine)), transfers,
      "Vortex.Stage20.ViewOverlay.Lines");
  }
  if (frame_ != ctx.frame_sequence) {
    constants_->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    triangles_->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    lines_->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
    frame_ = ctx.frame_sequence;
  }
}

} // namespace oxygen::vortex
