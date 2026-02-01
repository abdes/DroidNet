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
#include <Oxygen/Renderer/Internal/EnvironmentDynamicDataManager.h>
#include <Oxygen/Renderer/Internal/EnvironmentStaticDataManager.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/Passes/TransparentPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
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
  : GraphicsRenderPass(config ? config->debug_name : "TransparentPass")
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

  // Ensure environment static resources (e.g. BRDF LUT) are in correct state
  if (auto* env_static
    = Context().GetRenderer().GetEnvironmentStaticDataManager().get()) {
    env_static->EnforceBarriers(recorder);
  }

  recorder.FlushBarriers();
  co_return;
}

auto TransparentPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  // Bind EnvironmentDynamicData for Forward+ lighting and exposure
  if (const auto manager = Context().env_dynamic_manager) {
    const auto view_id = Context().current_view.view_id;
    manager->UpdateIfNeeded(view_id);
    if (const auto env_addr = manager->GetGpuVirtualAddress(view_id);
      env_addr != 0) {
      recorder.SetGraphicsRootConstantBufferView(
        static_cast<uint32_t>(binding::RootParam::kEnvironmentDynamicData),
        env_addr);
    }
  }

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

  DCHECK_F(pso_single_sided_.has_value());
  DCHECK_F(pso_double_sided_.has_value());

  // Transparent draws require strict back-to-front ordering across all
  // transparent materials. We therefore ignore partitions and render the
  // already-sorted draw list in order, selecting cull mode per draw.
  const auto* psf = Context().current_view.prepared_frame.get();
  if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()) {
    Context().RegisterPass(this);
    co_return;
  }

  const auto* records = reinterpret_cast<const engine::DrawMetadata*>(
    psf->draw_metadata_bytes.data());
  const auto record_count = static_cast<uint32_t>(
    psf->draw_metadata_bytes.size() / sizeof(engine::DrawMetadata));

  const graphics::GraphicsPipelineDesc* current_pso = nullptr;
  uint32_t emitted_count = 0;
  uint32_t skipped_invalid = 0;
  uint32_t draw_errors = 0;

  for (uint32_t draw_index = 0; draw_index < record_count; ++draw_index) {
    const auto& md = records[draw_index];
    if (!md.flags.IsSet(oxygen::engine::PassMaskBit::kTransparent)) {
      continue;
    }

    const bool is_double_sided
      = md.flags.IsSet(oxygen::engine::PassMaskBit::kDoubleSided);
    const auto& pso_desc
      = is_double_sided ? *pso_double_sided_ : *pso_single_sided_;
    if (current_pso != &pso_desc) {
      recorder.SetPipelineState(pso_desc);
      current_pso = &pso_desc;
    }

    EmitDrawRange(recorder, records, draw_index, draw_index + 1, emitted_count,
      skipped_invalid, draw_errors);
  }

  if (emitted_count > 0 || skipped_invalid > 0 || draw_errors > 0) {
    DLOG_F(2, "TransparentPass: emitted={}, skipped_invalid={}, errors={}",
      emitted_count, skipped_invalid, draw_errors);
  }
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

  const auto MakeRasterDesc = [&](CullMode cull_mode) -> RasterizerStateDesc {
    const auto effective_cull
      = (requested_fill == FillMode::kWireframe) ? CullMode::kNone : cull_mode;
    return RasterizerStateDesc {
      .fill_mode = requested_fill,
      .cull_mode = effective_cull,
      .front_counter_clockwise = true,
      .multisample_enable = false,
    };
  };

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

  const auto BuildDesc = [&](CullMode cull_mode) -> GraphicsPipelineDesc {
    return GraphicsPipelineDesc::Builder()
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Passes/Forward/ForwardMesh_VS.hlsl",
        .entry_point = "VS",
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Passes/Forward/ForwardMesh_PS.hlsl",
        .entry_point = "PS",
      })
      .SetPrimitiveTopology(PrimitiveType::kTriangleList)
      .SetRasterizerState(MakeRasterDesc(cull_mode))
      .SetDepthStencilState(ds_desc)
      // Enable standard alpha blending for transparent surfaces.
      // Straight (non-premultiplied) alpha convention:
      //   Color:   SrcColor * SrcAlpha + DestColor * (1 - SrcAlpha)
      //   Alpha:   SrcAlpha * 1 + DestAlpha * (1 - SrcAlpha)
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
  };

  pso_single_sided_ = BuildDesc(CullMode::kBack);
  pso_double_sided_ = BuildDesc(CullMode::kNone);
  return *pso_double_sided_;
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
