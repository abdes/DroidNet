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
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Detail/RootParamToBindings.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShaderPass.h>

using oxygen::engine::ShaderPass;
using oxygen::engine::ShaderPassConfig;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::DescriptorAllocator;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::MakeShaderIdentifier;
using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceRegistry;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Scissors;
using oxygen::graphics::Texture;
using oxygen::graphics::ViewPort;

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
  DescriptorAllocator& allocator) -> NativeObject
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
    rtv.IsValid()) {
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
  if (!rtv.IsValid()) {
    throw std::runtime_error("Failed to register RTV with resource registry "
                             "even after successful allocation.");
  }
  return rtv;
}

// Helper to prepare a depth stencil view for the depth texture
auto PrepareDepthStencilView(Texture& depth_texture, ResourceRegistry& registry,
  DescriptorAllocator& allocator) -> NativeObject
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
    dsv.IsValid()) {
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
  if (!dsv.IsValid()) {
    throw std::runtime_error("Failed to register DSV with resource registry "
                             "even after successful allocation.");
  }
  return dsv;
}
} // namespace

auto ShaderPass::SetupRenderTargets(CommandRecorder& recorder) const -> void
{
  // Prepare render target view(s)
  auto& render_controller = Context().GetRenderController();
  auto& registry = render_controller.GetResourceRegistry();
  auto& allocator = render_controller.GetDescriptorAllocator();
  auto& color_texture = const_cast<Texture&>(GetColorTexture());
  const auto color_rtv
    = PrepareRenderTargetView(color_texture, registry, allocator);
  std::array rtvs { color_rtv };

  // Prepare DSV if depth attachment is present
  NativeObject dsv = {};
  const auto* fb = GetFramebuffer();
  if (fb && fb->GetDescriptor().depth_attachment.IsValid()
    && fb->GetDescriptor().depth_attachment.texture) {
    auto& depth_texture = *fb->GetDescriptor().depth_attachment.texture;
    dsv = PrepareDepthStencilView(depth_texture, registry, allocator);
  }

  // Bind both RTV(s) and DSV if present
  if (dsv.IsValid()) {
    recorder.SetRenderTargets(std::span(rtvs), dsv);
  } else {
    recorder.SetRenderTargets(std::span(rtvs), std::nullopt);
  }

  recorder.ClearFramebuffer(*Context().framebuffer,
    std::vector<std::optional<graphics::Color>> { GetClearColor() },
    std::nullopt, std::nullopt);
}

auto ShaderPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  SetupViewPortAndScissors(recorder);
  SetupRenderTargets(recorder);
  IssueDrawCalls(recorder);
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

auto ShaderPass::GetDrawList() const -> std::span<const RenderItem>
{
  // FIXME: For now, always use the opaque_draw_list from the context.
  return Context().opaque_draw_list;
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

auto ShaderPass::SetupViewPortAndScissors(
  CommandRecorder& command_recorder) const -> void
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
  command_recorder.SetViewport(viewport);

  const Scissors scissors { .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(width),
    .bottom = static_cast<int32_t>(height) };
  command_recorder.SetScissors(scissors);
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
  using graphics::PrimitiveType;
  using graphics::PushConstantsBinding;
  using graphics::RasterizerStateDesc;
  using graphics::RootBindingDesc;
  using graphics::RootBindingItem;
  using graphics::ShaderStageDesc;
  using graphics::ShaderStageFlags;
  using oxygen::graphics::GraphicsPipelineDesc;

  // Set up rasterizer and blend state for standard color rendering
  constexpr RasterizerStateDesc raster_desc {
    .fill_mode = FillMode::kSolid,
    .cull_mode = CullMode::kNone,
    .front_counter_clockwise = true,
    .multisample_enable = false,
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
    .depth_test_enable = has_depth,
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
  auto generated_bindings
    = oxygen::graphics::BuildRootBindingItemsFromGenerated();

  return GraphicsPipelineDesc::Builder()
    .SetVertexShader(ShaderStageDesc { .shader
      = MakeShaderIdentifier(ShaderType::kVertex, "FullScreenTriangle.hlsl") })
    .SetPixelShader(ShaderStageDesc { .shader
      = MakeShaderIdentifier(ShaderType::kPixel, "FullScreenTriangle.hlsl") })
    .SetPrimitiveTopology(PrimitiveType::kTriangleList)
    .SetRasterizerState(raster_desc)
    .SetDepthStencilState(ds_desc)
    .SetBlendState({})
    .SetFramebufferLayout(fb_layout_desc)
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .Build();
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

  return false;
}
