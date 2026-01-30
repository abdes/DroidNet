//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "CompositingPass.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/RenderContext.h>

namespace oxygen::engine {

namespace {
  struct CompositingPassConstants {
    uint32_t source_texture_index;
    uint32_t sampler_index;
    float alpha;
    float pad0;
  };

  static_assert(sizeof(CompositingPassConstants) == 16,
    "CompositingPassConstants must be 16 bytes");

  auto PrepareRenderTargetView(graphics::Texture& color_texture,
    graphics::ResourceRegistry& registry,
    graphics::DescriptorAllocator& allocator) -> graphics::NativeView
  {
    const auto& tex_desc = color_texture.GetDescriptor();
    graphics::TextureViewDescription rtv_view_desc {
      .view_type = graphics::ResourceViewType::kTexture_RTV,
      .visibility = graphics::DescriptorVisibility::kCpuOnly,
      .format = tex_desc.format,
      .dimension = tex_desc.texture_type,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      .is_read_only_dsv = false,
    };

    if (const auto rtv = registry.Find(color_texture, rtv_view_desc);
      rtv->IsValid()) {
      return rtv;
    }

    auto rtv_desc_handle
      = allocator.Allocate(graphics::ResourceViewType::kTexture_RTV,
        graphics::DescriptorVisibility::kCpuOnly);
    if (!rtv_desc_handle.IsValid()) {
      throw std::runtime_error(
        "CompositingPass: Failed to allocate RTV descriptor handle");
    }

    const auto rtv = registry.RegisterView(
      color_texture, std::move(rtv_desc_handle), rtv_view_desc);
    if (!rtv->IsValid()) {
      throw std::runtime_error(
        "CompositingPass: Failed to register RTV with resource registry");
    }
    return rtv;
  }

  auto ClampViewport(const ViewPort& viewport,
    const graphics::TextureDesc& target_desc) -> ViewPort
  {
    ViewPort clamped = viewport;
    clamped.top_left_x = std::clamp(
      clamped.top_left_x, 0.0F, static_cast<float>(target_desc.width));
    clamped.top_left_y = std::clamp(
      clamped.top_left_y, 0.0F, static_cast<float>(target_desc.height));

    const auto max_width
      = static_cast<float>(target_desc.width) - clamped.top_left_x;
    const auto max_height
      = static_cast<float>(target_desc.height) - clamped.top_left_y;

    clamped.width = std::clamp(clamped.width, 0.0F, max_width);
    clamped.height = std::clamp(clamped.height, 0.0F, max_height);

    return clamped;
  }
} // namespace

CompositingPass::CompositingPass(std::shared_ptr<Config> config)
  : GraphicsRenderPass(config ? config->debug_name : "CompositingPass", false)
  , config_(std::move(config))
{
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
}

CompositingPass::~CompositingPass() { ReleasePassConstantsBuffer(); }

auto CompositingPass::ValidateConfig() -> void
{
  if (!config_) {
    throw std::runtime_error("CompositingPass: missing configuration");
  }
  if (!config_->source_texture) {
    throw std::runtime_error("CompositingPass: source texture is required");
  }
  if (!config_->viewport.IsValid()) {
    throw std::runtime_error("CompositingPass: viewport is invalid");
  }
  if (!GetFramebuffer()) {
    throw std::runtime_error("CompositingPass: framebuffer is required");
  }
}

auto CompositingPass::DoPrepareResources(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  const auto& source = GetSourceTexture();
  const auto& output = GetOutputTexture();
  const auto& src_desc = source.GetDescriptor();
  const auto& out_desc = output.GetDescriptor();

  LOG_F(INFO,
    "[CompositingPass] source={} size={}x{} fmt={} samples={} name={}",
    static_cast<const void*>(&source), src_desc.width, src_desc.height,
    static_cast<int>(src_desc.format), src_desc.sample_count,
    src_desc.debug_name);
  LOG_F(INFO,
    "[CompositingPass] output={} size={}x{} fmt={} samples={} name={}",
    static_cast<const void*>(&output), out_desc.width, out_desc.height,
    static_cast<int>(out_desc.format), out_desc.sample_count,
    out_desc.debug_name);
  LOG_F(INFO,
    "[CompositingPass] viewport=({}, {}) {}x{} alpha={}",
    config_->viewport.top_left_x, config_->viewport.top_left_y,
    config_->viewport.width, config_->viewport.height, config_->alpha);

  recorder.BeginTrackingResourceState(
    source, graphics::ResourceStates::kCommon);
  recorder.RequireResourceState(
    source, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    output, graphics::ResourceStates::kRenderTarget);
  recorder.FlushBarriers();

  EnsurePassConstantsBuffer();

  const auto source_srv = EnsureSourceTextureSrv(source);
  if (!source_srv.IsValid()) {
    throw std::runtime_error("CompositingPass: invalid source SRV index");
  }
  UpdatePassConstants(source_srv);

  co_return;
}

auto CompositingPass::DoExecute(graphics::CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  SetupViewPortAndScissors(recorder);
  SetupRenderTargets(recorder);

  recorder.Draw(3, 1, 0, 0);

  const auto& source = GetSourceTexture();
  recorder.RequireResourceState(source, graphics::ResourceStates::kCommon);
  recorder.FlushBarriers();

  co_return;
}

auto CompositingPass::SetupRenderTargets(
  graphics::CommandRecorder& recorder) const -> void
{
  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  auto& color_texture = const_cast<graphics::Texture&>(GetOutputTexture());
  const auto color_rtv
    = PrepareRenderTargetView(color_texture, registry, allocator);
  std::array rtvs { color_rtv };

  recorder.SetRenderTargets(std::span(rtvs), std::nullopt);
}

auto CompositingPass::SetupViewPortAndScissors(
  graphics::CommandRecorder& recorder) const -> void
{
  const auto& output_desc = GetOutputTexture().GetDescriptor();
  const auto clamped = ClampViewport(config_->viewport, output_desc);

  const ViewPort viewport {
    .top_left_x = clamped.top_left_x,
    .top_left_y = clamped.top_left_y,
    .width = clamped.width,
    .height = clamped.height,
    .min_depth = clamped.min_depth,
    .max_depth = clamped.max_depth,
  };
  recorder.SetViewport(viewport);

  const Scissors scissors {
    .left = static_cast<int32_t>(clamped.top_left_x),
    .top = static_cast<int32_t>(clamped.top_left_y),
    .right = static_cast<int32_t>(clamped.top_left_x + clamped.width),
    .bottom = static_cast<int32_t>(clamped.top_left_y + clamped.height),
  };
  recorder.SetScissors(scissors);
}

auto CompositingPass::GetFramebuffer() const -> const graphics::Framebuffer*
{
  return Context().framebuffer.get();
}

auto CompositingPass::GetOutputTexture() const -> const graphics::Texture&
{
  const auto* fb = GetFramebuffer();
  if (!fb) {
    throw std::runtime_error("CompositingPass: framebuffer is null");
  }
  const auto& fb_desc = fb->GetDescriptor();
  if (fb_desc.color_attachments.empty()
    || !fb_desc.color_attachments[0].texture) {
    throw std::runtime_error("CompositingPass: missing color attachment");
  }
  return *fb_desc.color_attachments[0].texture;
}

auto CompositingPass::GetSourceTexture() const -> const graphics::Texture&
{
  CHECK_NOTNULL_F(config_);
  CHECK_F(static_cast<bool>(config_->source_texture),
    "CompositingPass requires a source texture");
  return *config_->source_texture;
}

auto CompositingPass::EnsurePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ && pass_constants_indices_[0].IsValid()) {
    return;
  }

  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  const graphics::BufferDesc desc {
    .size_bytes = kPassConstantsStride * kPassConstantsSlots,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = std::string { GetName() } + "_PassConstants",
  };

  pass_constants_buffer_ = graphics.CreateBuffer(desc);
  if (!pass_constants_buffer_) {
    throw std::runtime_error(
      "CompositingPass: Failed to create pass constants buffer");
  }
  pass_constants_buffer_->SetName(desc.debug_name);

  pass_constants_mapped_ptr_
    = static_cast<std::byte*>(pass_constants_buffer_->Map(0, desc.size_bytes));
  if (!pass_constants_mapped_ptr_) {
    throw std::runtime_error(
      "CompositingPass: Failed to map pass constants buffer");
  }

  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
  registry.Register(pass_constants_buffer_);
  for (size_t slot = 0; slot < kPassConstantsSlots; ++slot) {
    const uint32_t offset
      = static_cast<uint32_t>(slot * kPassConstantsStride);

    graphics::BufferViewDescription cbv_view_desc;
    cbv_view_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
    cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_view_desc.range = { offset, kPassConstantsStride };

    auto cbv_handle
      = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!cbv_handle.IsValid()) {
      throw std::runtime_error(
        "CompositingPass: Failed to allocate CBV descriptor handle");
    }
    pass_constants_indices_[slot]
      = allocator.GetShaderVisibleIndex(cbv_handle);

    auto cbv_view = registry.RegisterView(
      *pass_constants_buffer_, std::move(cbv_handle), cbv_view_desc);
    if (!cbv_view->IsValid()) {
      throw std::runtime_error(
        "CompositingPass: Failed to register pass constants CBV");
    }
  }
}

auto CompositingPass::ReleasePassConstantsBuffer() -> void
{
  if (!pass_constants_buffer_) {
    pass_constants_mapped_ptr_ = nullptr;
    return;
  }

  if (pass_constants_buffer_->IsMapped()) {
    pass_constants_buffer_->UnMap();
  }

  pass_constants_mapped_ptr_ = nullptr;
  pass_constants_buffer_.reset();
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
  pass_constants_slot_ = 0u;
}

auto CompositingPass::EnsureSourceTextureSrv(const graphics::Texture& texture)
  -> ShaderVisibleIndex
{
  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  const auto& tex_desc = texture.GetDescriptor();
  graphics::TextureViewDescription srv_desc {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = tex_desc.format,
    .dimension = tex_desc.texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .is_read_only_dsv = false,
  };

  if (auto it = source_texture_srvs_.find(&texture);
    it != source_texture_srvs_.end()) {
    if (registry.Contains(texture, srv_desc)) {
      return it->second;
    }
    source_texture_srvs_.erase(it);
  }

  auto srv_handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!srv_handle.IsValid()) {
    throw std::runtime_error(
      "CompositingPass: Failed to allocate source SRV handle");
  }

  const auto srv_index = allocator.GetShaderVisibleIndex(srv_handle);
  auto srv_view = registry.RegisterView(
    const_cast<graphics::Texture&>(texture), std::move(srv_handle), srv_desc);
  if (!srv_view->IsValid()) {
    throw std::runtime_error(
      "CompositingPass: Failed to register source SRV view");
  }

  source_texture_srvs_[&texture] = srv_index;
  return srv_index;
}

auto CompositingPass::UpdatePassConstants(
  ShaderVisibleIndex source_texture_index) -> void
{
  CHECK_NOTNULL_F(pass_constants_mapped_ptr_);

  const float alpha = std::clamp(config_->alpha, 0.0F, 1.0F);
  const CompositingPassConstants constants {
    .source_texture_index = source_texture_index.get(),
    .sampler_index = 0u,
    .alpha = alpha,
    .pad0 = 0.0F,
  };

  const auto slot = pass_constants_slot_ % kPassConstantsSlots;
  pass_constants_slot_++;
  auto* slot_ptr = pass_constants_mapped_ptr_
    + static_cast<std::ptrdiff_t>(slot * kPassConstantsStride);
  std::memcpy(slot_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(pass_constants_indices_[slot]);
}

auto CompositingPass::CreatePipelineStateDesc()
  -> graphics::GraphicsPipelineDesc
{
  using graphics::BlendFactor;
  using graphics::BlendOp;
  using graphics::BlendTargetDesc;
  using graphics::ColorWriteMask;
  using graphics::DepthStencilStateDesc;
  using graphics::FramebufferLayoutDesc;
  using graphics::GraphicsPipelineDesc;
  using graphics::PrimitiveType;
  using graphics::RasterizerStateDesc;
  using graphics::ShaderRequest;

  const auto& color_desc = GetOutputTexture().GetDescriptor();
  const FramebufferLayoutDesc fb_layout_desc {
    .color_target_formats = { color_desc.format },
    .depth_stencil_format = Format::kUnknown,
    .sample_count = color_desc.sample_count,
  };

  const RasterizerStateDesc raster_desc {
    .fill_mode = graphics::FillMode::kSolid,
    .cull_mode = graphics::CullMode::kNone,
    .front_counter_clockwise = true,
    .multisample_enable = false,
  };

  const DepthStencilStateDesc ds_desc = DepthStencilStateDesc::Disabled();

  const BlendTargetDesc blend_desc {
    .blend_enable = true,
    .src_blend = BlendFactor::kSrcAlpha,
    .dest_blend = BlendFactor::kInvSrcAlpha,
    .blend_op = BlendOp::kAdd,
    .src_blend_alpha = BlendFactor::kOne,
    .dest_blend_alpha = BlendFactor::kInvSrcAlpha,
    .blend_op_alpha = BlendOp::kAdd,
    .write_mask = ColorWriteMask::kAll,
  };

  auto root_bindings = BuildRootBindings();

  return GraphicsPipelineDesc::Builder()
    .SetVertexShader(ShaderRequest {
      .stage = oxygen::ShaderType::kVertex,
      .source_path = "Passes/Compositing/Compositing_VS.hlsl",
      .entry_point = "VS",
      .defines = {},
    })
    .SetPixelShader(ShaderRequest {
      .stage = oxygen::ShaderType::kPixel,
      .source_path = "Passes/Compositing/Compositing_PS.hlsl",
      .entry_point = "PS",
      .defines = {},
    })
    .SetPrimitiveTopology(PrimitiveType::kTriangleList)
    .SetRasterizerState(raster_desc)
    .SetDepthStencilState(ds_desc)
    .SetBlendState({ blend_desc })
    .SetFramebufferLayout(fb_layout_desc)
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size()))
    .Build();
}

auto CompositingPass::NeedRebuildPipelineState() const -> bool
{
  const auto& last_built = LastBuiltPsoDesc();
  if (!last_built) {
    return true;
  }

  const auto& color_desc = GetOutputTexture().GetDescriptor();
  if (last_built->FramebufferLayout().color_target_formats.empty()
    || last_built->FramebufferLayout().color_target_formats[0]
      != color_desc.format) {
    return true;
  }

  if (last_built->FramebufferLayout().sample_count != color_desc.sample_count) {
    return true;
  }

  return false;
}

} // namespace oxygen::engine
