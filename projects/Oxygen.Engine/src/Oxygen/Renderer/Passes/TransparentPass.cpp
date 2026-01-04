//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Passes/TransparentPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/PassMask.h>

using oxygen::engine::TransparentPass;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::GraphicsPipelineDesc;
using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;

TransparentPass::TransparentPass(std::shared_ptr<Config> config)
  : RenderPass(config ? config->debug_name : "TransparentPass")
  , config_(std::move(config))
{
}

auto TransparentPass::ValidateConfig() -> void
{
  if (!config_ || !config_->color_texture) {
    throw std::runtime_error("TransparentPass: color_texture required");
  }
  // depth_texture optional (could render after opaque depth already present)
}

auto TransparentPass::DoPrepareResources(CommandRecorder& recorder) -> co::Co<>
{
  // Transition targets (color RT for render, depth read if provided)
  recorder.RequireResourceState(
    *config_->color_texture, graphics::ResourceStates::kRenderTarget);
  if (config_->depth_texture) {
    recorder.RequireResourceState(
      *config_->depth_texture, graphics::ResourceStates::kDepthRead);
  }
  recorder.FlushBarriers();
  co_return;
}

auto TransparentPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  // Minimal RT binding path identical to ShaderPass helper logic (inline to
  // avoid duplication until a shared helper is extracted).
  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  const auto& color_tex = *config_->color_texture;
  const auto& color_desc = color_tex.GetDescriptor();
  graphics::TextureViewDescription rtv_desc {
    .view_type = ResourceViewType::kTexture_RTV,
    .visibility = DescriptorVisibility::kCpuOnly,
    .format = color_desc.format,
    .dimension = color_desc.texture_type,
    .sub_resources = { .base_mip_level = 0,
      .num_mip_levels = color_desc.mip_levels,
      .base_array_slice = 0,
      .num_array_slices = (color_desc.texture_type == TextureType::kTexture3D
          ? color_desc.depth
          : color_desc.array_size) },
    .is_read_only_dsv = false,
  };
  graphics::NativeView rtv;
  if (auto found = registry.Find(color_tex, rtv_desc); found->IsValid()) {
    rtv = found;
  } else {
    auto handle = allocator.Allocate(
      ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly);
    if (!handle.IsValid()) {
      throw std::runtime_error(
        "TransparentPass: failed to allocate RTV descriptor");
    }
    rtv = registry.RegisterView(
      const_cast<Texture&>(color_tex), std::move(handle), rtv_desc);
  }

  graphics::NativeView dsv;
  if (config_->depth_texture) {
    const auto& depth_tex = *config_->depth_texture;
    const auto& depth_desc = depth_tex.GetDescriptor();
    graphics::TextureViewDescription dsv_desc { .view_type
      = ResourceViewType::kTexture_DSV,
      .visibility = DescriptorVisibility::kCpuOnly,
      .format = depth_desc.format,
      .dimension = depth_desc.texture_type,
      .sub_resources = { .base_mip_level = 0,
        .num_mip_levels = depth_desc.mip_levels,
        .base_array_slice = 0,
        .num_array_slices = (depth_desc.texture_type == TextureType::kTexture3D
            ? depth_desc.depth
            : depth_desc.array_size) },
      .is_read_only_dsv = true };
    if (auto found = registry.Find(depth_tex, dsv_desc); found->IsValid()) {
      dsv = found;
    } else {
      auto handle = allocator.Allocate(
        ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);
      if (!handle.IsValid()) {
        throw std::runtime_error(
          "TransparentPass: failed to allocate DSV descriptor");
      }
      dsv = registry.RegisterView(
        const_cast<Texture&>(depth_tex), std::move(handle), dsv_desc);
    }
  }

  if (dsv->IsValid()) {
    recorder.SetRenderTargets(std::span(&rtv, 1), dsv);
  } else {
    recorder.SetRenderTargets(std::span(&rtv, 1), std::nullopt);
  }

  // Issue only transparent draws; RenderPass logs emitted count.
  // TODO(engine): Implement proper back-to-front ordering (or OIT) inside the
  // transparent partition; current order is deterministic but not depth-sorted
  // which can cause incorrect blending for overlapping transparent geometry.
  IssueDrawCallsOverPass(recorder, oxygen::engine::PassMaskBit::kTransparent);
  Context().RegisterPass(this);
  co_return;
}

auto TransparentPass::GetColorTexture() const -> const Texture&
{
  return *config_->color_texture;
}

auto TransparentPass::GetDepthTexture() const -> const Texture*
{
  return config_->depth_texture.get();
}

// Removed IssueTransparentDraws (predicate-based IssueDrawCalls used instead).

auto TransparentPass::CreatePipelineStateDesc() -> GraphicsPipelineDesc
{
  using graphics::CompareOp;
  using graphics::CullMode;
  using graphics::DepthStencilStateDesc;
  using graphics::FillMode;
  using graphics::GraphicsPipelineDesc;
  using graphics::PrimitiveType;
  using graphics::RasterizerStateDesc;

  const auto requested_fill
    = config_ ? config_->fill_mode : oxygen::graphics::FillMode::kSolid;
  const RasterizerStateDesc raster_desc { .fill_mode = requested_fill,
    .cull_mode = CullMode::kNone,
    .front_counter_clockwise = true,
    .multisample_enable = false };

  DepthStencilStateDesc ds_desc { .depth_test_enable
    = (GetDepthTexture() != nullptr),
    .depth_write_enable = false, // transparent: no depth writes
    .depth_func = CompareOp::kLessOrEqual,
    .stencil_enable = false,
    .stencil_read_mask = 0xFF,
    .stencil_write_mask = 0xFF };

  const auto& color_desc = GetColorTexture().GetDescriptor();
  auto sample_count = color_desc.sample_count;
  auto depth_format = Format::kUnknown;
  if (auto* depth = GetDepthTexture()) {
    depth_format = depth->GetDescriptor().format;
    sample_count = depth->GetDescriptor().sample_count;
  }
  const graphics::FramebufferLayoutDesc fb_layout_desc { .color_target_formats
    = { color_desc.format },
    .depth_stencil_format = depth_format,
    .sample_count = sample_count };

  // Generated root binding items (indices + descriptor tables)
  auto generated_bindings = BuildRootBindings();

  // NOTE: Reuse existing bindless mesh shader (see ShaderPass rationale).
  return GraphicsPipelineDesc::Builder()
    .SetVertexShader(graphics::ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "Passes/Forward/ForwardMesh.hlsl",
      .entry_point = "VS",
    })
    .SetPixelShader(graphics::ShaderRequest {
      .stage = ShaderType::kPixel,
      .source_path = "Passes/Forward/ForwardMesh.hlsl",
      .entry_point = "PS",
    })
    .SetPrimitiveTopology(PrimitiveType::kTriangleList)
    .SetRasterizerState(raster_desc)
    .SetDepthStencilState(ds_desc)
    // Enable standard alpha blending for transparent surfaces.
    // We already have BlendTargetDesc (PipelineState.h); previous TODO was
    // stale. Using straight (non-premultiplied) alpha convention:
    //   Color:   SrcColor * SrcAlpha + DestColor * (1 - SrcAlpha)
    //   Alpha:   SrcAlpha * 1 + DestAlpha * (1 - SrcAlpha)
    // If/when premultiplied alpha is adopted switch src_blend to kOne.
    .SetBlendState({ graphics::BlendTargetDesc {
      .blend_enable = true,
      .src_blend = graphics::BlendFactor::kSrcAlpha,
      .dest_blend = graphics::BlendFactor::kInvSrcAlpha,
      .blend_op = graphics::BlendOp::kAdd,
      .src_blend_alpha = graphics::BlendFactor::kOne,
      .dest_blend_alpha = graphics::BlendFactor::kInvSrcAlpha,
      .blend_op_alpha = graphics::BlendOp::kAdd,
      .write_mask = graphics::ColorWriteMask::kAll,
    } })
    .SetFramebufferLayout(fb_layout_desc)
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .Build();
}

auto TransparentPass::NeedRebuildPipelineState() const -> bool
{
  const auto& last = LastBuiltPsoDesc();
  if (!last) {
    return true;
  }
  const auto& color_desc = GetColorTexture().GetDescriptor();
  if (last->FramebufferLayout().color_target_formats.empty()
    || last->FramebufferLayout().color_target_formats[0] != color_desc.format) {
    return true;
  }
  if (auto* depth = GetDepthTexture()) {
    if (last->FramebufferLayout().depth_stencil_format
      != depth->GetDescriptor().format) {
      return true;
    }
  }
  // Rebuild pipeline state if rasterizer fill mode changed
  const auto requested_fill
    = config_ ? config_->fill_mode : oxygen::graphics::FillMode::kSolid;
  if (last->RasterizerState().fill_mode != requested_fill) {
    return true;
  }
  return false;
}
