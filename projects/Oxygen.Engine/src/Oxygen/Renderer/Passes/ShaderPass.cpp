//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <utility>

#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/MaterialPermutations.h>
#include <Oxygen/Renderer/Types/PassMask.h>

using oxygen::engine::ShaderPass;
using oxygen::engine::ShaderPassConfig;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::DescriptorAllocator;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceRegistry;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;

ShaderPass::ShaderPass(std::shared_ptr<ShaderPassConfig> config)
  : RenderPass(config ? config->debug_name : "ShaderPass")
  , config_(std::move(config))
{
}

/*!
 This method is called by the constructor to ensure that the provided
 configuration together with the current RenderContext allow to create a healthy
 ShaderPass.

 ## Checks
 - Must have a valid color texture, either from the configuration or the
   framebuffer.
 - RenderContext must have a valid pointer to DepthPrePass (dependency).

 @throws std::runtime_error if validation fails.
*/
auto ShaderPass::ValidateConfig() -> void
{
  // Will throw if no valid color texture is found.
  [[maybe_unused]] const auto& _ = GetColorTexture();

  // TODO: validate DepthPrePass dependency.
}

auto ShaderPass::DoPrepareResources(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);
  // Transition the color target to RENDER_TARGET state
  recorder.RequireResourceState(
    GetColorTexture(), graphics::ResourceStates::kRenderTarget);

  // Transition the depth target to DEPTH_READ or DEPTH_WRITE as needed
  const auto* fb = GetFramebuffer();
  if (fb && fb->GetDescriptor().depth_attachment.IsValid()
    && fb->GetDescriptor().depth_attachment.texture) {
    recorder.RequireResourceState(*fb->GetDescriptor().depth_attachment.texture,
      graphics::ResourceStates::kDepthRead);
  }

  recorder.FlushBarriers();

  co_return;
}

namespace {
// Helper to prepare a render target view for the color texture, mirroring
// DepthPrePass::PrepareDepthStencilView
auto PrepareRenderTargetView(Texture& color_texture, ResourceRegistry& registry,
  DescriptorAllocator& allocator) -> oxygen::graphics::NativeView
{
  using oxygen::TextureType;

  const auto& tex_desc = color_texture.GetDescriptor();
  oxygen::graphics::TextureViewDescription rtv_view_desc { .view_type
    = ResourceViewType::kTexture_RTV,
    .visibility = DescriptorVisibility::kCpuOnly,
    .format = tex_desc.format,
    .dimension = tex_desc.texture_type,
    .sub_resources = { .base_mip_level = 0,
      .num_mip_levels = tex_desc.mip_levels,
      .base_array_slice = 0,
      .num_array_slices = (tex_desc.texture_type == TextureType::kTexture3D
          ? tex_desc.depth
          : tex_desc.array_size) },
    .is_read_only_dsv = false };

  if (const auto rtv = registry.Find(color_texture, rtv_view_desc);
    rtv->IsValid()) {
    return rtv;
  }
  auto rtv_desc_handle = allocator.Allocate(
    ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly);
  if (!rtv_desc_handle.IsValid()) {
    throw std::runtime_error(
      "Failed to allocate RTV descriptor handle for color texture");
  }
  const auto rtv = registry.RegisterView(
    color_texture, std::move(rtv_desc_handle), rtv_view_desc);
  if (!rtv->IsValid()) {
    throw std::runtime_error("Failed to register RTV with resource registry "
                             "even after successful allocation.");
  }
  return rtv;
}

// Helper to prepare a depth stencil view for the depth texture
auto PrepareDepthStencilView(Texture& depth_texture, ResourceRegistry& registry,
  DescriptorAllocator& allocator) -> oxygen::graphics::NativeView
{
  using oxygen::TextureType;

  const auto& tex_desc = depth_texture.GetDescriptor();
  oxygen::graphics::TextureViewDescription dsv_view_desc {
    .view_type = ResourceViewType::kTexture_DSV,
    .visibility = DescriptorVisibility::kCpuOnly,
    .format = tex_desc.format,
    .dimension = tex_desc.texture_type,
    .sub_resources = { .base_mip_level = 0,
      .num_mip_levels = tex_desc.mip_levels,
      .base_array_slice = 0,
      .num_array_slices = (tex_desc.texture_type == TextureType::kTexture3D
          ? tex_desc.depth
          : tex_desc.array_size) },
    .is_read_only_dsv = true,
  };

  if (const auto dsv = registry.Find(depth_texture, dsv_view_desc);
    dsv->IsValid()) {
    return dsv;
  }
  auto dsv_desc_handle = allocator.Allocate(
    ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);
  if (!dsv_desc_handle.IsValid()) {
    throw std::runtime_error(
      "Failed to allocate DSV descriptor handle for depth texture");
  }
  const auto dsv = registry.RegisterView(
    depth_texture, std::move(dsv_desc_handle), dsv_view_desc);
  if (!dsv->IsValid()) {
    throw std::runtime_error("Failed to register DSV with resource registry "
                             "even after successful allocation.");
  }
  return dsv;
}
} // namespace

auto ShaderPass::SetupRenderTargets(CommandRecorder& recorder) const -> void
{
  // Prepare render target view(s)
  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();
  auto& color_texture = const_cast<Texture&>(GetColorTexture());
  const auto color_rtv
    = PrepareRenderTargetView(color_texture, registry, allocator);
  std::array rtvs { color_rtv };

  // Prepare DSV if depth attachment is present
  graphics::NativeView dsv = {};
  const auto* fb = GetFramebuffer();
  if (fb && fb->GetDescriptor().depth_attachment.IsValid()
    && fb->GetDescriptor().depth_attachment.texture) {
    auto& depth_texture = *fb->GetDescriptor().depth_attachment.texture;
    dsv = PrepareDepthStencilView(depth_texture, registry, allocator);
  }

  // Bind both RTV(s) and DSV if present
  if (dsv->IsValid()) {
    recorder.SetRenderTargets(std::span(rtvs), dsv);
  } else {
    recorder.SetRenderTargets(std::span(rtvs), std::nullopt);
  }

  // Keep render target setup logs at debug level to avoid noisy INFO output
  DLOG_F(2,
    "[ShaderPass] SetupRenderTargets: color_tex={}, depth_tex={}, "
    "clear_color=({}, {}, {}, {})",
    static_cast<const void*>(&color_texture),
    dsv->IsValid() ? static_cast<const void*>(
                       fb->GetDescriptor().depth_attachment.texture.get())
                   : nullptr,
    GetClearColor().r, GetClearColor().g, GetClearColor().b, GetClearColor().a);

  recorder.ClearFramebuffer(*Context().framebuffer,
    std::vector<std::optional<graphics::Color>> { GetClearColor() },
    std::nullopt, std::nullopt);
}

auto ShaderPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  SetupViewPortAndScissors(recorder);
  SetupRenderTargets(recorder);

  // Emit opaque and masked partitions; transparent handled by TransparentPass.
  const auto psf = Context().current_view.prepared_frame;
  if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()) {
    Context().RegisterPass(this);
    co_return;
  }
  if (psf->partitions.empty()) {
    LOG_F(ERROR, "ShaderPass: partitions are missing; nothing will be drawn");
    Context().RegisterPass(this);
    co_return;
  }

  DCHECK_F(pso_opaque_single_.has_value());
  DCHECK_F(pso_opaque_double_.has_value());
  DCHECK_F(pso_masked_single_.has_value());
  DCHECK_F(pso_masked_double_.has_value());

  const auto* records = reinterpret_cast<const engine::DrawMetadata*>(
    psf->draw_metadata_bytes.data());

  uint32_t emitted_count = 0;
  uint32_t skipped_invalid = 0;
  uint32_t draw_errors = 0;

  for (const auto& pr : psf->partitions) {
    if (!pr.pass_mask.IsSet(oxygen::engine::PassMaskBit::kOpaque)
      && !pr.pass_mask.IsSet(oxygen::engine::PassMaskBit::kMasked)) {
      continue;
    }

    const bool is_masked
      = pr.pass_mask.IsSet(oxygen::engine::PassMaskBit::kMasked);
    const bool is_double_sided
      = pr.pass_mask.IsSet(oxygen::engine::PassMaskBit::kDoubleSided);

    const auto& pso_desc = [&]() -> const graphics::GraphicsPipelineDesc& {
      if (is_masked) {
        return is_double_sided ? *pso_masked_double_ : *pso_masked_single_;
      }
      return is_double_sided ? *pso_opaque_double_ : *pso_opaque_single_;
    }();
    recorder.SetPipelineState(pso_desc);

    EmitDrawRange(recorder, records, pr.begin, pr.end, emitted_count,
      skipped_invalid, draw_errors);
  }

  if (emitted_count > 0 || skipped_invalid > 0 || draw_errors > 0) {
    DLOG_F(2, "ShaderPass: emitted={}, skipped_invalid={}, errors={}",
      emitted_count, skipped_invalid, draw_errors);
  }

  Context().RegisterPass(this);

  co_return;
}

auto ShaderPass::GetColorTexture() const -> const Texture&
{
  if (config_ && config_->color_texture) {
    return *config_->color_texture;
  }
  const auto* fb = GetFramebuffer();
  if (fb && !fb->GetDescriptor().color_attachments.empty()
    && fb->GetDescriptor().color_attachments[0].texture) {
    return *fb->GetDescriptor().color_attachments[0].texture;
  }
  throw std::runtime_error("ShaderPass: No valid color texture found.");
}

auto ShaderPass::GetFramebuffer() const -> const Framebuffer*
{
  return Context().framebuffer.get();
}

auto ShaderPass::GetClearColor() const -> const graphics::Color&
{
  if (config_ && config_->clear_color.has_value()) {
    return *config_->clear_color;
  }
  const auto& color_tex_desc = GetColorTexture().GetDescriptor();
  return color_tex_desc.clear_value;
}
auto ShaderPass::HasDepth() const -> bool
{
  const auto* fb = GetFramebuffer();
  return fb && fb->GetDescriptor().depth_attachment.IsValid();
}

auto ShaderPass::SetupViewPortAndScissors(CommandRecorder& recorder) const
  -> void
{
  const auto& common_tex_desc = GetColorTexture().GetDescriptor();
  const auto width = common_tex_desc.width;
  const auto height = common_tex_desc.height;

  const ViewPort viewport { .top_left_x = 0.0f,
    .top_left_y = 0.0f,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0f,
    .max_depth = 1.0f };
  recorder.SetViewport(viewport);

  const Scissors scissors { .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(width),
    .bottom = static_cast<int32_t>(height) };
  recorder.SetScissors(scissors);
}

// --- Pipeline setup for ShaderPass ---

/*!
 Creates the pipeline state description for the ShaderPass. This should
 configure the pipeline for color rendering (with color writes enabled),
 suitable for a simple forward or Forward+ pass. The configuration should match
 the color target's format and sample count, and set up the root signature for
 per-draw constants if needed.

 @return GraphicsPipelineDesc for the ShaderPass.
*/
auto ShaderPass::CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc
{
  using graphics::BindingSlotDesc;
  using graphics::CompareOp;
  using graphics::CullMode;
  using graphics::DepthStencilStateDesc;
  using graphics::DescriptorTableBinding;
  using graphics::DirectBufferBinding;
  using graphics::FillMode;
  using graphics::FramebufferLayoutDesc;
  using graphics::GraphicsPipelineDesc;
  using graphics::PrimitiveType;
  using graphics::PushConstantsBinding;
  using graphics::RasterizerStateDesc;
  using graphics::RootBindingDesc;
  using graphics::RootBindingItem;
  using graphics::ShaderRequest;
  using graphics::ShaderStageFlags;

  // Determine requested rasterizer fill mode from configuration (default solid)
  const auto requested_fill
    = config_ ? config_->fill_mode : oxygen::graphics::FillMode::kSolid;

  const auto MakeRasterDesc = [&](CullMode cull_mode) -> RasterizerStateDesc {
    // When wireframe is requested disable culling so edges for all faces are
    // visible.
    const auto effective_cull
      = (requested_fill == FillMode::kWireFrame) ? CullMode::kNone : cull_mode;
    return RasterizerStateDesc {
      .fill_mode = requested_fill,
      .cull_mode = effective_cull,
      .front_counter_clockwise = true,
      .multisample_enable = false,
    };
  };

  // Determine if a depth attachment is present
  const auto* fb = GetFramebuffer();
  bool has_depth = false;
  auto depth_format = Format::kUnknown;
  uint32_t sample_count = 1;
  if (fb) {
    const auto& fb_desc = fb->GetDescriptor();
    if (fb_desc.depth_attachment.IsValid()
      && fb_desc.depth_attachment.texture) {
      has_depth = true;
      depth_format = fb_desc.depth_attachment.texture->GetDescriptor().format;
      sample_count
        = fb_desc.depth_attachment.texture->GetDescriptor().sample_count;
    } else if (!fb_desc.color_attachments.empty()
      && fb_desc.color_attachments[0].IsValid()
      && fb_desc.color_attachments[0].texture) {
      sample_count
        = fb_desc.color_attachments[0].texture->GetDescriptor().sample_count;
    }
  }

  DepthStencilStateDesc ds_desc {
    .depth_test_enable = has_depth && (requested_fill != FillMode::kWireFrame),
    .depth_write_enable = false,
    .depth_func = CompareOp::kLessOrEqual,
    .stencil_enable = false,
    .stencil_read_mask = 0xFF,
    .stencil_write_mask = 0xFF,
  };

  // Get color target format from the color texture
  const auto& color_tex_desc = GetColorTexture().GetDescriptor();
  const FramebufferLayoutDesc fb_layout_desc {
    .color_target_formats = { color_tex_desc.format },
    .depth_stencil_format = depth_format,
    .sample_count = sample_count,
  };

  // Build root bindings from generated table
  auto generated_bindings = BuildRootBindings();

  // NOTE: The engine uses Passes/Forward/ForwardMesh.hlsl for forward shading.
  // Material permutations are driven by shader defines (e.g., ALPHA_TEST)
  // rather than separate entry points. This allows the same PS entry point
  // to compile into different variants based on active defines.
  using graphics::ShaderDefine;

  const auto BuildDesc
    = [&](CullMode cull_mode,
        std::vector<ShaderDefine> defines) -> GraphicsPipelineDesc {
    return GraphicsPipelineDesc::Builder()
      .SetVertexShader(ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Passes/Forward/ForwardMesh.hlsl",
        .entry_point = "VS",
        .defines = defines,
      })
      .SetPixelShader(ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Passes/Forward/ForwardMesh.hlsl",
        .entry_point = "PS",
        .defines = defines,
      })
      .SetPrimitiveTopology(PrimitiveType::kTriangleList)
      .SetRasterizerState(MakeRasterDesc(cull_mode))
      .SetDepthStencilState(ds_desc)
      .SetBlendState({})
      .SetFramebufferLayout(fb_layout_desc)
      .SetRootBindings(std::span<const RootBindingItem>(
        generated_bindings.data(), generated_bindings.size()))
      .Build();
  };

  // Partition-aware variants using shader defines.
  // ALPHA_TEST define enables alpha-tested (masked) path in pixel shader.
  pso_opaque_single_
    = BuildDesc(CullMode::kBack, ToDefines(permutation::kOpaqueDefines));
  pso_opaque_double_
    = BuildDesc(CullMode::kNone, ToDefines(permutation::kOpaqueDefines));
  pso_masked_single_
    = BuildDesc(CullMode::kBack, ToDefines(permutation::kMaskedDefines));
  pso_masked_double_
    = BuildDesc(CullMode::kNone, ToDefines(permutation::kMaskedDefines));

  // Emit diagnostic log for rasterizer settings used to build the PSO.
  DLOG_F(2, "[ShaderPass] CreatePipelineStateDesc: fill_mode={}",
    static_cast<int>(requested_fill));

  return *pso_opaque_single_;
}

/*!
 Determines if the pipeline state needs to be rebuilt, e.g., if the color
 texture's format or sample count has changed.
*/
auto ShaderPass::NeedRebuildPipelineState() const -> bool
{
  const auto& last_built = LastBuiltPsoDesc();
  if (!last_built) {
    return true;
  }

  const auto& color_tex_desc = GetColorTexture().GetDescriptor();
  if (last_built->FramebufferLayout().color_target_formats.empty()
    || last_built->FramebufferLayout().color_target_formats[0]
      != color_tex_desc.format) {
    return true;
  }

  if (last_built->FramebufferLayout().sample_count
    != color_tex_desc.sample_count) {
    return true;
  }
  // Rebuild if rasterizer fill mode changed
  const auto requested_fill
    = config_ ? config_->fill_mode : oxygen::graphics::FillMode::kSolid;
  if (last_built->RasterizerState().fill_mode != requested_fill) {
    return true;
  }

  return false;
}

// Removed IssueOpaqueDraws (superseded by partition-based IssueDrawCalls).
