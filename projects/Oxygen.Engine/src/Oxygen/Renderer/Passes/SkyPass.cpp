//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <utility>

#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Types/Format.h>
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
#include <Oxygen/Renderer/Passes/SkyPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>

using oxygen::engine::SkyPass;
using oxygen::engine::SkyPassConfig;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::DescriptorAllocator;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceRegistry;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;

SkyPass::SkyPass(std::shared_ptr<SkyPassConfig> config)
  : GraphicsRenderPass(config ? config->debug_name : "SkyPass")
  , config_(std::move(config))
{
}

auto SkyPass::ValidateConfig() -> void
{
  // Will throw if no valid color texture is found.
  [[maybe_unused]] const auto& _ = GetColorTexture();
}

auto SkyPass::DoPrepareResources(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  // The color target should remain in RENDER_TARGET state from ShaderPass.
  // Just ensure it's in the correct state.
  recorder.RequireResourceState(
    GetColorTexture(), graphics::ResourceStates::kRenderTarget);

  // Depth buffer should be in DEPTH_READ for sky depth test.
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

} // namespace

auto SkyPass::SetupRenderTargets(CommandRecorder& recorder) const -> void
{
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

  // Bind both RTV(s) and DSV if present.
  // Do NOT clear - we want to preserve the scene rendered by ShaderPass.
  if (dsv->IsValid()) {
    recorder.SetRenderTargets(std::span(rtvs), dsv);
  } else {
    recorder.SetRenderTargets(std::span(rtvs), std::nullopt);
  }

  DLOG_F(2, "[SkyPass] SetupRenderTargets: color_tex={}, depth_tex={}",
    static_cast<const void*>(&color_texture),
    dsv->IsValid() ? static_cast<const void*>(
                       fb->GetDescriptor().depth_attachment.texture.get())
                   : nullptr);
}

auto SkyPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  // Bind EnvironmentDynamicData for exposure and other dynamic data.
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

  // Draw fullscreen triangle for sky.
  // The vertex shader generates positions from SV_VertexID.
  // 3 vertices form a single triangle covering the entire screen.
  recorder.Draw(3, 1, 0, 0);

  Context().RegisterPass(this);

  co_return;
}

auto SkyPass::GetColorTexture() const -> const Texture&
{
  if (config_ && config_->color_texture) {
    return *config_->color_texture;
  }
  const auto* fb = GetFramebuffer();
  if (fb && !fb->GetDescriptor().color_attachments.empty()
    && fb->GetDescriptor().color_attachments[0].texture) {
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
    .top_left_x = 0.0f,
    .top_left_y = 0.0f,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0f,
    .max_depth = 1.0f,
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
  using graphics::CompareOp;
  using graphics::CullMode;
  using graphics::DepthStencilStateDesc;
  using graphics::FillMode;
  using graphics::FramebufferLayoutDesc;
  using graphics::GraphicsPipelineDesc;
  using graphics::PrimitiveType;
  using graphics::RasterizerStateDesc;
  using graphics::ShaderRequest;

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

  // Sky renders at z=1.0 (far plane) with LESS_EQUAL test.
  // Depth writes disabled to not affect subsequent transparent pass.
  DepthStencilStateDesc ds_desc {
    .depth_test_enable = has_depth,
    .depth_write_enable = false,
    .depth_func = CompareOp::kLessOrEqual,
    .stencil_enable = false,
    .stencil_read_mask = 0xFF,
    .stencil_write_mask = 0xFF,
  };

  // No culling for fullscreen triangle.
  RasterizerStateDesc raster_desc {
    .fill_mode = FillMode::kSolid,
    .cull_mode = CullMode::kNone,
    .front_counter_clockwise = true,
    .multisample_enable = false,
  };

  const auto& color_tex_desc = GetColorTexture().GetDescriptor();
  const FramebufferLayoutDesc fb_layout_desc {
    .color_target_formats = { color_tex_desc.format },
    .depth_stencil_format = depth_format,
    .sample_count = sample_count,
  };

  auto generated_bindings = BuildRootBindings();

  return GraphicsPipelineDesc::Builder()
    .SetVertexShader(ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "Passes/Sky/SkySphere_VS.hlsl",
      .entry_point = "VS",
      .defines = {},
    })
    .SetPixelShader(ShaderRequest {
      .stage = ShaderType::kPixel,
      .source_path = "Passes/Sky/SkySphere_PS.hlsl",
      .entry_point = "PS",
      .defines = {},
    })
    .SetPrimitiveTopology(PrimitiveType::kTriangleList)
    .SetRasterizerState(raster_desc)
    .SetDepthStencilState(ds_desc)
    .SetBlendState({})
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
  if (last_built->FramebufferLayout().color_target_formats.empty()
    || last_built->FramebufferLayout().color_target_formats[0]
      != color_tex_desc.format) {
    return true;
  }

  if (last_built->FramebufferLayout().sample_count
    != color_tex_desc.sample_count) {
    return true;
  }

  return false;
}
