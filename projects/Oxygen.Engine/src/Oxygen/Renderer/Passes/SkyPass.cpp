//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Buffer.h>
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
#include <Oxygen/Renderer/Internal/EnvironmentDynamicDataManager.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/SkyPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>

using oxygen::engine::SkyPass;
using oxygen::engine::SkyPassConfig;
using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::DescriptorAllocator;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceRegistry;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;

namespace {
struct alignas(16) SkyPassConstants {
  float mouse_down_x { 0.0F };
  float mouse_down_y { 0.0F };
  float viewport_width { 0.0F };
  float viewport_height { 0.0F };
  uint32_t mouse_down_valid { 0U };
  uint32_t depth_srv_index { oxygen::kInvalidShaderVisibleIndex.get() };
  uint32_t pad1 { 0U };
  uint32_t pad2 { 0U };
  glm::mat4 inv_view_proj { 1.0F };
};

constexpr uint32_t kSkyPassConstantsSize = 96U;
static_assert(sizeof(SkyPassConstants) == kSkyPassConstantsSize,
  "SkyPassConstants must be 96 bytes");

constexpr uint32_t kConstantsBufferMinSize = 256U;

auto PrepareRenderTargetView(Texture& color_texture, ResourceRegistry& registry,
  DescriptorAllocator& allocator) -> oxygen::graphics::NativeView
{
  using oxygen::TextureType;

  const auto& tex_desc = color_texture.GetDescriptor();
  oxygen::graphics::TextureViewDescription rtv_view_desc {
    .view_type = ResourceViewType::kTexture_RTV,
    .visibility = DescriptorVisibility::kCpuOnly,
    .format = tex_desc.format,
    .dimension = tex_desc.texture_type,
    .sub_resources = { .base_mip_level = 0,
      .num_mip_levels = tex_desc.mip_levels,
      .base_array_slice = 0,
      .num_array_slices = (tex_desc.texture_type == TextureType::kTexture3D
          ? tex_desc.depth
          : tex_desc.array_size) },
    .is_read_only_dsv = false,
  };

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
    throw std::runtime_error("Failed to register RTV with resource registry.");
  }
  return rtv;
}

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
    throw std::runtime_error("Failed to register DSV with resource registry.");
  }
  return dsv;
}

auto PrepareDepthShaderResourceView(Texture& depth_texture,
  ResourceRegistry& registry, DescriptorAllocator& allocator)
  -> std::pair<oxygen::graphics::NativeView, uint32_t>
{
  using oxygen::TextureType;

  const auto& tex_desc = depth_texture.GetDescriptor();

  // Choose SRV format based on depth format
  oxygen::Format srv_format = tex_desc.format;
  if (tex_desc.format == oxygen::Format::kDepth32) {
    srv_format = oxygen::Format::kR32Float;
  }

  oxygen::graphics::TextureViewDescription srv_view_desc {
    .view_type = ResourceViewType::kTexture_SRV,
    .visibility = DescriptorVisibility::kShaderVisible,
    .format = srv_format,
    .dimension = tex_desc.texture_type,
    .sub_resources = { .base_mip_level = 0,
      .num_mip_levels = 1,
      .base_array_slice = 0,
      .num_array_slices = (tex_desc.texture_type == TextureType::kTexture3D
          ? tex_desc.depth
          : tex_desc.array_size) },
    .is_read_only_dsv = false,
  };

  if (const auto srv = registry.Find(depth_texture, srv_view_desc);
    srv->IsValid()) {
    // If found, we still need to fish out the shader-visible index.
    // For now, let's allow finding but return invalid index to trigger
    // re-lookup if needed, or better, assume allocator can give it back. Since
    // we don't store it in the view, we return kInvalid for now which will
    // trigger the re-allocation attempt. Actually, allocator might have it. But
    // safer to re-allocate if srv index is not known.
    return { srv, oxygen::kInvalidShaderVisibleIndex.get() };
  }
  auto srv_desc_handle = allocator.Allocate(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  if (!srv_desc_handle.IsValid()) {
    throw std::runtime_error(
      "Failed to allocate SRV descriptor handle for depth texture");
  }
  const uint32_t srv_index
    = allocator.GetShaderVisibleIndex(srv_desc_handle).get();
  const auto srv = registry.RegisterView(
    depth_texture, std::move(srv_desc_handle), srv_view_desc);
  if (!srv->IsValid()) {
    throw std::runtime_error(
      "Failed to register depth SRV with resource registry.");
  }
  return { srv, srv_index };
}

} // namespace

SkyPass::SkyPass(std::shared_ptr<SkyPassConfig> config)
  : GraphicsRenderPass(config ? config->debug_name : "SkyPass")
  , config_(std::move(config))
{
}

SkyPass::~SkyPass() { ReleasePassConstantsBuffer(); }

auto SkyPass::ValidateConfig() -> void
{
  // Will throw if no valid color texture is found.
  [[maybe_unused]] const auto& _ = GetColorTexture();
}

auto SkyPass::SetupRenderTargets(CommandRecorder& recorder) const -> void
{
  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  auto& color_texture = const_cast<Texture&>(GetColorTexture());
  const auto color_rtv
    = PrepareRenderTargetView(color_texture, registry, allocator);
  std::array rtvs { color_rtv };

  // Prepare DSV if a depth buffer is available (prefer DepthPrePass output).
  graphics::NativeView dsv = {};
  const Texture* depth_texture = GetDepthTexture();
  if (depth_texture != nullptr) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    dsv = PrepareDepthStencilView(
      const_cast<Texture&>(*depth_texture), registry, allocator);
  }

  // Bind DSV if present (read-only) to satisfy PSO matching if depth test is
  // used with kAlways.
  if (dsv->IsValid()) {
    recorder.SetRenderTargets(std::span(rtvs), dsv);
  } else {
    recorder.SetRenderTargets(std::span(rtvs), std::nullopt);
  }

  DLOG_F(2, "[SkyPass] SetupRenderTargets: color_tex={}, depth_tex={}",
    static_cast<const void*>(&color_texture),
    depth_texture != nullptr ? static_cast<const void*>(depth_texture)
                             : nullptr);
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
auto SkyPass::DoPrepareResources(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  // Ensure the color target is writable.
  recorder.RequireResourceState(
    GetColorTexture(), graphics::ResourceStates::kRenderTarget);

  // Depth buffer should be in DEPTH_READ for both DSV binding and SRV sampling.
  if (const Texture* depth_texture = GetDepthTexture();
    depth_texture != nullptr) {
    recorder.RequireResourceState(
      *depth_texture, graphics::ResourceStates::kDepthRead);
  }

  recorder.FlushBarriers();

  EnsurePassConstantsBuffer();
  UpdatePassConstants();

  co_return;
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
auto SkyPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

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

  SetupViewPortAndScissors(recorder);
  SetupRenderTargets(recorder);

  const uint32_t pass_constants_index = pass_constants_index_.IsValid()
    ? pass_constants_index_.get()
    : oxygen::kInvalidShaderVisibleIndex.get();
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants), 0U, 0);
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants),
    pass_constants_index, 1);

  recorder.Draw(3, 1, 0, 0);

  Context().RegisterPass(this);

  co_return;
}

auto SkyPass::EnsurePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ && pass_constants_index_.IsValid()) {
    return;
  }

  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  const graphics::BufferDesc desc {
    .size_bytes = kConstantsBufferMinSize,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "SkyPass_Constants",
  };

  pass_constants_buffer_ = graphics.CreateBuffer(desc);
  if (pass_constants_buffer_ == nullptr) {
    throw std::runtime_error("SkyPass: Failed to create pass constants buffer");
  }
  pass_constants_buffer_->SetName(desc.debug_name);

  pass_constants_mapped_ptr_
    = static_cast<std::byte*>(pass_constants_buffer_->Map(0, desc.size_bytes));
  if (pass_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error("SkyPass: Failed to map pass constants buffer");
  }

  graphics::BufferViewDescription cbv_view_desc;
  cbv_view_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
  cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  cbv_view_desc.range = { 0U, desc.size_bytes };

  auto cbv_handle
    = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!cbv_handle.IsValid()) {
    throw std::runtime_error("SkyPass: Failed to allocate CBV handle");
  }
  pass_constants_index_ = allocator.GetShaderVisibleIndex(cbv_handle);

  registry.Register(pass_constants_buffer_);
  registry.RegisterView(
    *pass_constants_buffer_, std::move(cbv_handle), cbv_view_desc);
}

auto SkyPass::UpdatePassConstants() -> void
{
  if (pass_constants_mapped_ptr_ == nullptr) {
    return;
  }

  SkyPassConstants constants {};
  if (config_ != nullptr) {
    const bool valid_mouse = config_->debug_mouse_down_position.has_value()
      && config_->debug_viewport_extent.width > 0.0F
      && config_->debug_viewport_extent.height > 0.0F;
    if (valid_mouse) {
      const auto mouse = *config_->debug_mouse_down_position;
      constants.mouse_down_x = mouse.x;
      constants.mouse_down_y = mouse.y;
      constants.viewport_width = config_->debug_viewport_extent.width;
      constants.viewport_height = config_->debug_viewport_extent.height;
      constants.mouse_down_valid = 1U;
    }
  }

  if (Context().current_view.resolved_view != nullptr) {
    constants.inv_view_proj
      = Context().current_view.resolved_view->InverseViewProjection();
  }

  if (const Texture* depth_texture = GetDepthTexture();
    depth_texture != nullptr) {
    auto& graphics = Context().GetGraphics();
    auto& registry = graphics.GetResourceRegistry();
    auto& allocator = graphics.GetDescriptorAllocator();

    if (depth_texture != last_depth_texture_) {
      last_depth_texture_ = depth_texture;
      depth_srv_index_ = oxygen::kInvalidShaderVisibleIndex;
    }

    if (!depth_srv_index_.IsValid()) {
      const auto [srv_view, srv_index] = PrepareDepthShaderResourceView(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        const_cast<Texture&>(*depth_texture), registry, allocator);
      if (srv_index != oxygen::kInvalidShaderVisibleIndex.get()) {
        depth_srv_index_ = oxygen::ShaderVisibleIndex { srv_index };
      }
    }
  } else {
    depth_srv_index_ = oxygen::kInvalidShaderVisibleIndex;
    last_depth_texture_ = nullptr;
  }
  constants.depth_srv_index = depth_srv_index_.get();

  std::memcpy(pass_constants_mapped_ptr_, &constants, sizeof(constants));
}

auto SkyPass::ReleasePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ == nullptr) {
    pass_constants_mapped_ptr_ = nullptr;
    return;
  }

  if (pass_constants_buffer_->IsMapped()) {
    pass_constants_buffer_->UnMap();
  }

  pass_constants_mapped_ptr_ = nullptr;
  pass_constants_buffer_.reset();
  pass_constants_index_ = oxygen::kInvalidShaderVisibleIndex;
  depth_srv_index_ = oxygen::kInvalidShaderVisibleIndex;
  last_depth_texture_ = nullptr;
}

auto SkyPass::GetColorTexture() const -> const Texture&
{
  if ((config_ != nullptr) && (config_->color_texture != nullptr)) {
    return *config_->color_texture;
  }
  const auto* fb = GetFramebuffer();
  if (fb != nullptr && !fb->GetDescriptor().color_attachments.empty()
    && (fb->GetDescriptor().color_attachments[0].texture != nullptr)) {
    return *fb->GetDescriptor().color_attachments[0].texture;
  }
  throw std::runtime_error("SkyPass: No valid color texture found.");
}

auto SkyPass::GetFramebuffer() const -> const Framebuffer*
{
  return Context().framebuffer.get();
}

auto SkyPass::SetupViewPortAndScissors(CommandRecorder& recorder) const -> void
{
  const auto& tex_desc = GetColorTexture().GetDescriptor();
  const auto width = tex_desc.width;
  const auto height = tex_desc.height;

  const ViewPort viewport {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  recorder.SetViewport(viewport);

  const Scissors scissors {
    .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(width),
    .bottom = static_cast<int32_t>(height),
  };
  recorder.SetScissors(scissors);
}

auto SkyPass::CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc
{
  using graphics::BlendFactor;
  using graphics::BlendOp;
  using graphics::BlendTargetDesc;
  using graphics::ColorWriteMask;
  using graphics::CompareOp;
  using graphics::CullMode;
  using graphics::DepthStencilStateDesc;
  using graphics::FillMode;
  using graphics::FramebufferLayoutDesc;
  using graphics::GraphicsPipelineDesc;
  using graphics::PrimitiveType;
  using graphics::RasterizerStateDesc;
  using graphics::ShaderRequest;

  bool has_depth = false;
  auto depth_format = oxygen::Format::kUnknown;
  uint32_t sample_count = 1U;
  if (const Texture* depth_texture = GetDepthTexture();
    depth_texture != nullptr) {
    has_depth = true;
    depth_format = depth_texture->GetDescriptor().format;
    sample_count = depth_texture->GetDescriptor().sample_count;
  } else {
    const auto& color_tex_desc = GetColorTexture().GetDescriptor();
    sample_count = color_tex_desc.sample_count;
  }

  // Restore hardware depth-stencil state for sky pass.
  // Use CompareOp::kLessOrEqual to ensure sky is only drawn at the far plane
  // (background), preventing it from drawing over opaque geometry.
  DepthStencilStateDesc ds_desc {
    .depth_test_enable = has_depth,
    .depth_write_enable = false,
    .depth_func = CompareOp::kLessOrEqual,
    .stencil_enable = false,
    .stencil_read_mask = 0xFF,
    .stencil_write_mask = 0xFF,
  };

  RasterizerStateDesc raster_desc {
    .fill_mode = FillMode::kSolid,
    .cull_mode = CullMode::kNone,
    .front_counter_clockwise = true,
    .multisample_enable = false,
  };

  const BlendTargetDesc blend_desc {
    .blend_enable = true,
    .src_blend = BlendFactor::kOne,
    .dest_blend = BlendFactor::kZero,
    .blend_op = BlendOp::kAdd,
    .src_blend_alpha = BlendFactor::kZero,
    .dest_blend_alpha = BlendFactor::kOne,
    .blend_op_alpha = BlendOp::kAdd,
    .write_mask = ColorWriteMask::kAll,
  };

  const auto& color_tex_desc = GetColorTexture().GetDescriptor();
  const FramebufferLayoutDesc fb_layout_desc {
    .color_target_formats = { color_tex_desc.format },
    .depth_stencil_format = depth_format,
    .sample_count = sample_count,
  };

  auto generated_bindings = BuildRootBindings();
  std::vector<graphics::ShaderDefine> ps_defines;
  if (graphics::detail::IsHdr(color_tex_desc.format)) {
    ps_defines.push_back(graphics::ShaderDefine {
      .name = "OXYGEN_HDR_OUTPUT",
      .value = "1",
    });
  }

  return GraphicsPipelineDesc::Builder()
    .SetVertexShader(ShaderRequest {
      .stage = oxygen::ShaderType::kVertex,
      .source_path = "Atmosphere/SkySphere_VS.hlsl",
      .entry_point = "VS",
      .defines = {},
    })
    .SetPixelShader(ShaderRequest {
      .stage = oxygen::ShaderType::kPixel,
      .source_path = "Atmosphere/SkySphere_PS.hlsl",
      .entry_point = "PS",
      .defines = ps_defines,
    })
    .SetPrimitiveTopology(PrimitiveType::kTriangleList)
    .SetRasterizerState(raster_desc)
    .SetDepthStencilState(ds_desc)
    .AddBlendTarget(blend_desc)
    .SetFramebufferLayout(fb_layout_desc)
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .Build();
}

auto SkyPass::NeedRebuildPipelineState() const -> bool
{
  const auto& last_built = LastBuiltPsoDesc();
  if (!last_built) {
    return true;
  }

  const auto& color_tex_desc = GetColorTexture().GetDescriptor();
  const auto& layout = last_built->FramebufferLayout();

  // Color format check
  if (layout.color_target_formats.empty()
    || (layout.color_target_formats[0] != color_tex_desc.format)) {
    return true;
  }

  // Depth format check
  auto current_depth_format = oxygen::Format::kUnknown;
  if (const Texture* depth_texture = GetDepthTexture();
    depth_texture != nullptr) {
    current_depth_format = depth_texture->GetDescriptor().format;
  }

  if (layout.depth_stencil_format != current_depth_format) {
    return true;
  }

  // Sample count check
  if (layout.sample_count != color_tex_desc.sample_count) {
    return true;
  }

  return false;
}

auto SkyPass::GetDepthTexture() const -> const Texture*
{
  // Prefer the depth texture produced by the DepthPrePass.
  if (const auto* depth_pass = Context().GetPass<engine::DepthPrePass>();
    depth_pass != nullptr) {
    return &depth_pass->GetDepthTexture();
  }

  // Fallback to the framebuffer depth attachment when the pre-pass is not
  // available (e.g. wireframe-only modes).
  const auto* fb = GetFramebuffer();
  if (fb != nullptr && fb->GetDescriptor().depth_attachment.IsValid()
    && (fb->GetDescriptor().depth_attachment.texture != nullptr)) {
    return fb->GetDescriptor().depth_attachment.texture.get();
  }

  return nullptr;
}
