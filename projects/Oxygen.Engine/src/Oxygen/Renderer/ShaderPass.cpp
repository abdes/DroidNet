//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <utility>

#include <Oxygen/Data/MeshAsset.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/Format.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
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
  recorder.FlushBarriers();

  co_return;
}

namespace {
// Helper to prepare a render target view for the color texture, mirroring
// DepthPrePass::PrepareDepthStencilView
auto PrepareRenderTargetView(Texture& color_texture, ResourceRegistry& registry,
  DescriptorAllocator& allocator) -> NativeObject
{
  const auto& tex_desc = color_texture.GetDescriptor();
  oxygen::graphics::TextureViewDescription rtv_view_desc { .view_type
    = ResourceViewType::kTexture_RTV,
    .visibility = DescriptorVisibility::kCpuOnly,
    .format = tex_desc.format,
    .dimension = tex_desc.dimension,
    .sub_resources = { .base_mip_level = 0,
      .num_mip_levels = tex_desc.mip_levels,
      .base_array_slice = 0,
      .num_array_slices
      = (tex_desc.dimension == oxygen::graphics::TextureDimension::kTexture3D
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
  recorder.SetRenderTargets(std::span(rtvs), std::nullopt);

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
  if (fb && fb->GetDescriptor().color_attachments.size() > 0
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
  // Set up rasterizer and blend state for standard color rendering
  constexpr graphics::RasterizerStateDesc raster_desc {
    .fill_mode = graphics::FillMode::kSolid,
    .cull_mode = graphics::CullMode::kBack,
    .front_counter_clockwise = true,
    .multisample_enable = false,
  };

  constexpr graphics::DepthStencilStateDesc ds_desc {
    .depth_test_enable = false, // No depth for this simple pass
    .depth_write_enable = false,
    .depth_func = graphics::CompareOp::kAlways,
    .stencil_enable = false,
    .stencil_read_mask = 0xFF,
    .stencil_write_mask = 0xFF,
  };

  // Get color target format and sample count from the color texture
  const auto& color_tex_desc = GetColorTexture().GetDescriptor();
  const graphics::FramebufferLayoutDesc fb_layout_desc {
    .color_target_formats = { color_tex_desc.format },
    .depth_stencil_format = graphics::Format::kUnknown,
    .sample_count = color_tex_desc.sample_count,
  };

  // Root binding for SRV descriptor table (bindless vertex buffer)
  constexpr graphics::RootBindingDesc srv_table_desc { .binding_slot_desc
    = graphics::BindingSlotDesc { .register_index = 0, .register_space = 0 },
    .visibility = graphics::ShaderStageFlags::kAll,
    .data = graphics::DescriptorTableBinding {
      .view_type = ResourceViewType::kStructuredBuffer_SRV,
      .base_index = 0,
    } };
  // Root binding for per-draw constants (CBV)
  constexpr graphics::RootBindingDesc per_draw_cbv_desc { .binding_slot_desc
    = graphics::BindingSlotDesc { .register_index = 0, .register_space = 0 },
    .visibility = graphics::ShaderStageFlags::kAll,
    .data = graphics::DirectBufferBinding {} };

  return graphics::GraphicsPipelineDesc::Builder()
    .SetVertexShader(graphics::ShaderStageDesc {
      .shader = MakeShaderIdentifier(
        graphics::ShaderType::kVertex, "FullScreenTriangle.hlsl") })
    .SetPixelShader(graphics::ShaderStageDesc {
      .shader = MakeShaderIdentifier(
        graphics::ShaderType::kPixel, "FullScreenTriangle.hlsl") })
    .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
    .SetRasterizerState(raster_desc)
    .SetDepthStencilState(ds_desc)
    .SetBlendState({})
    .SetFramebufferLayout(fb_layout_desc)
    .AddRootBinding(graphics::RootBindingItem(srv_table_desc))
    .AddRootBinding(graphics::RootBindingItem(per_draw_cbv_desc))
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
