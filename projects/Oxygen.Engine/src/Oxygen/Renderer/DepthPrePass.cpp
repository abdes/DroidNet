//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/Scissors.h>
#include <Oxygen/Graphics/Common/Types/ViewPort.h>
#include <Oxygen/Renderer/DepthPrepass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Renderer/Renderer.h>

using oxygen::engine::DepthPrePass;
using oxygen::graphics::Buffer;
using oxygen::graphics::Color;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::GraphicsPipelineDesc;
using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Scissors;
using oxygen::graphics::Texture;
using oxygen::graphics::ViewPort;

DepthPrePass::DepthPrePass(std::shared_ptr<Config> config)
  : RenderPass(config->debug_name)
  , config_(std::move(config))
{
}

//! Sets the viewport for the depth pre-pass.
auto DepthPrePass::SetViewport(const ViewPort& viewport) -> void
{
  if (!viewport.IsValid()) {
    throw std::invalid_argument(
      fmt::format("viewport {} is invalid", nostd::to_string(viewport)));
  }
  DCHECK_NOTNULL_F(
    config_->depth_texture, "expecting a non-null depth texture");

  const auto& tex_desc = config_->depth_texture->GetDescriptor();

  auto viewport_width = viewport.top_left_x + viewport.width;
  auto viewport_height = viewport.top_left_y + viewport.height;
  if (viewport_width > static_cast<float>(tex_desc.width)
    || viewport_height > static_cast<float>(tex_desc.height)) {
    throw std::out_of_range(fmt::format(
      "viewport dimensions ({}, {}) exceed depth_texture bounds: ({}, {})",
      viewport_width, viewport_height, tex_desc.width, tex_desc.height));
  }
  viewport_.emplace(viewport);
}

auto DepthPrePass::SetScissors(const Scissors& scissors) -> void
{
  if (!scissors.IsValid()) {
    throw std::invalid_argument(
      fmt::format("scissors {} are invalid.", nostd::to_string(scissors)));
  }
  DCHECK_NOTNULL_F(config_->depth_texture,
    "expecting depth texture to be valid when setting scissors");

  const auto& tex_desc = config_->depth_texture->GetDescriptor();

  // Assuming scissors coordinates are relative to the texture origin (0,0)
  if (scissors.left < 0 || scissors.top < 0) {
    throw std::out_of_range("scissors left and top must be non-negative.");
  }
  if (std::cmp_greater(scissors.right, tex_desc.width)
    || std::cmp_greater(scissors.bottom, tex_desc.height)) {
    throw std::out_of_range(fmt::format(
      "scissors dimensions ({}, {}) exceed depth_texture bounds ({}, {})",
      scissors.right, scissors.bottom, tex_desc.width, tex_desc.height));
  }

  scissors_.emplace(scissors);
}

auto DepthPrePass::SetClearColor(const Color& color) -> void
{
  clear_color_.emplace(color);
}

auto DepthPrePass::GetDrawList() const -> std::span<const RenderItem>
{
  // FIXME: For now, always use the opaque_draw_list from the context.
  return Context().opaque_draw_list;
}
auto DepthPrePass::GetFramebuffer() const -> const Framebuffer*
{
  return Context().framebuffer.get();
}

/*!
 The base implementation of this method ensures that the `depth_texture`
 (specified in `Config`) is transitioned to a state suitable for depth-stencil
 attachment (e.g., `ResourceStates::kDepthWrite`) using the provided
 `CommandRecorder`. It then flushes any pending resource barriers.

 Flushing barriers here is crucial to ensure the `depth_texture` is definitively
 in the `kDepthWrite` state before any subsequent operations by derived classes
 (e.g., clearing the texture) or later render stages.

 Backend-specific derived classes should call this base method and can then
 perform additional preparations, such as:
 - Interpreting `clear_color_` to derive depth and/or stencil clear values and
   applying them to the `depth_texture`.
 - Preparing the optional `framebuffer` if it's provided in `Config` and is
   relevant to the backend operation (e.g., for binding or coordinated
   transitions).
*/
auto DepthPrePass::DoPrepareResources(CommandRecorder& recorder) -> co::Co<>
{
  // Ensure the depth_texture is in kDepthWrite state before derived classes
  // might perform operations like clears. Note that the depth_texture should
  // be already in a valid state when this method is called, but we are
  // explicitly transitioning it for safety. The transition will be optimized
  // out if the state is already correct.
  if (config_->depth_texture) {
    recorder.RequireResourceState(
      *config_->depth_texture, graphics::ResourceStates::kDepthWrite);
    recorder.FlushBarriers();
  }

  co_return;
}

auto DepthPrePass::ValidateConfig() -> void
{
  // Will throw if no valid color texture is found.
  [[maybe_unused]] const auto& _ = GetDepthTexture();
}

auto DepthPrePass::NeedRebuildPipelineState() const -> bool
{
  const auto& last_built = LastBuiltPsoDesc();
  if (!last_built) {
    return true;
  }

  // If pipeline state exists, check if depth texture properties have changed
  if (last_built->FramebufferLayout().depth_stencil_format
    != GetDepthTexture().GetDescriptor().format) {
    return true;
  }
  if (last_built->FramebufferLayout().sample_count
    != GetDepthTexture().GetDescriptor().sample_count) {
    return true;
  }
  return false; // No need to rebuild
}
auto DepthPrePass::GetDepthTexture() const -> const Texture&
{
  const Texture* depth_texture { nullptr };
  if (config_ && config_->depth_texture) {
    depth_texture = config_->depth_texture.get();
  }
  const auto* fb = GetFramebuffer();
  if (fb && fb->GetDescriptor().depth_attachment.IsValid()) {
    const auto* fb_depth_texture
      = fb->GetDescriptor().depth_attachment.texture.get();
    // Ensure if both are present, then both are the same
    if (depth_texture && depth_texture != fb_depth_texture) {
      throw std::runtime_error(
        "DepthPrePass: Config depth_texture and framebuffer depth attachment "
        "texture must match when both are provided.");
    }
    return *fb_depth_texture;
  }

  if (depth_texture) {
    return *depth_texture;
  }

  throw std::runtime_error("ShaderPass: No valid depth texture found.");
}

/*!
 For a DepthPrePass, this involves rendering the geometry from the `draw_list`
 (specified in `Config`) to populate the `depth_texture`. Key responsibilities
 include:
 - Setting up a pipeline state configured for depth-only rendering (no color
   writes).
 - Applying the `viewport_` and `scissors_` if they have been set.
 - Issuing draw calls for the specified geometry.
*/

auto DepthPrePass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  const auto dsv = PrepareDepthStencilView(GetDepthTexture());
  DCHECK_F(dsv.IsValid(), "DepthStencilView must be valid after preparation");

  SetupViewPortAndScissors(recorder);
  ClearDepthStencilView(recorder, dsv);
  SetupRenderTargets(recorder, dsv);
  IssueDrawCalls(recorder);
  Context().RegisterPass(this);

  co_return;
}

// --- Private helper implementations for Execute() ---

auto DepthPrePass::PrepareDepthStencilView(const Texture& depth_texture_ref)
  -> NativeObject
{
  using graphics::DescriptorHandle;
  using graphics::DescriptorVisibility;
  using graphics::TextureDimension;
  using graphics::TextureViewDescription;

  auto& render_controller = Context().GetRenderController();
  auto& registry = render_controller.GetResourceRegistry();
  auto& allocator = render_controller.GetDescriptorAllocator();

  // 1. Prepare TextureViewDescription
  const auto& depth_tex_desc = depth_texture_ref.GetDescriptor();
  const TextureViewDescription dsv_view_desc {
    .view_type = ResourceViewType::kTexture_DSV,
    .visibility = DescriptorVisibility::kCpuOnly,
    .format = depth_tex_desc.format,
    .dimension = depth_tex_desc.dimension,
    .sub_resources = {
        .base_mip_level = 0,
        .num_mip_levels = depth_tex_desc.mip_levels,
        .base_array_slice = 0,
        .num_array_slices = (depth_tex_desc.dimension == TextureDimension::kTexture3D
                ? depth_tex_desc.depth
                : depth_tex_desc.array_size),
    },
    .is_read_only_dsv = false, // Default for a writable DSV
  };

  // 2. Check with ResourceRegistry::FindView
  if (const auto dsv = registry.Find(depth_texture_ref, dsv_view_desc);
    dsv.IsValid()) {
    return dsv;
  }
  // View not found (cache miss), create and register it
  DescriptorHandle dsv_desc_handle = allocator.Allocate(
    ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);

  if (!dsv_desc_handle.IsValid()) {
    throw std::runtime_error(
      "Failed to allocate DSV descriptor handle for depth texture");
  }
  // Register the newly created view
  const auto dsv = registry.RegisterView(
    const_cast<Texture&>(depth_texture_ref), // Added const_cast
    std::move(dsv_desc_handle), dsv_view_desc);

  if (!dsv.IsValid()) {
    throw std::runtime_error("Failed to register DSV with resource registry "
                             "even after successful allocation.");
  }

  return dsv;
}

auto DepthPrePass::ClearDepthStencilView(CommandRecorder& command_recorder,
  const NativeObject& dsv_handle) const -> void
{
  // only depth, as the depth pre-pass does not use the stencil buffer
  command_recorder.ClearDepthStencilView(
    GetDepthTexture(), dsv_handle, graphics::ClearFlags::kDepth, 1.0f, 0);
}

auto DepthPrePass::SetupRenderTargets(
  CommandRecorder& command_recorder, const NativeObject& dsv) const -> void
{
  DCHECK_F(dsv.IsValid(),
    "DepthStencilView must be valid before setting render targets");

  command_recorder.SetRenderTargets({}, dsv);
}

auto DepthPrePass::SetupViewPortAndScissors(
  CommandRecorder& command_recorder) const -> void
{
  // Use the depth texture. It is already validated consistent with the
  // framebuffer if provided.
  const auto& common_tex_desc = GetDepthTexture().GetDescriptor();
  const auto width = common_tex_desc.width;
  const auto height = common_tex_desc.height;

  const ViewPort viewport {
    .top_left_x = 0.0f,
    .top_left_y = 0.0f,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0f,
    .max_depth = 1.0f,
  };
  command_recorder.SetViewport(viewport);

  const Scissors scissors {
    .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(width),
    .bottom = static_cast<int32_t>(height),
  };
  command_recorder.SetScissors(scissors);
}

auto DepthPrePass::CreatePipelineStateDesc() -> GraphicsPipelineDesc
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
  using graphics::RasterizerStateDesc;
  using graphics::RootBindingDesc;
  using graphics::RootBindingItem;
  using graphics::ShaderStageDesc;
  using graphics::ShaderStageFlags;
  using graphics::ShaderType;

  constexpr RasterizerStateDesc raster_desc {
    .fill_mode = FillMode::kSolid,
    .cull_mode = CullMode::kBack,
    .front_counter_clockwise = true, // Default winding order for front faces

    // D3D12_RASTERIZER_DESC::MultisampleEnable is for controlling antialiasing
    // behavior on lines and edges, not strictly for enabling/disabling MSAA
    // sample processing for a texture. The sample_count in
    // FramebufferLayoutDesc and the texture itself dictate MSAA. It's often
    // left false unless specific line/edge AA is needed.
    //
    // Or `depth_texture.GetDesc().sample_count > 1` if specifically needed for
    // rasterizer stage
    .multisample_enable = false,
  };

  constexpr DepthStencilStateDesc ds_desc {
    .depth_test_enable = true, // Enable depth testing
    .depth_write_enable = true, // Enable writing to depth buffer
    .depth_func = CompareOp::kLessOrEqual, // Typical depth comparison function
    .stencil_enable = false, // Stencil testing usually disabled unless required
    .stencil_read_mask = 0xFF, // full-mask for reading stencil buffer
    .stencil_write_mask = 0xFF, // full-mask for writing to stencil buffer
  };

  auto& depth_texture_desc = GetDepthTexture().GetDescriptor();
  const FramebufferLayoutDesc fb_layout_desc { .color_target_formats = {},
    .depth_stencil_format = depth_texture_desc.format,
    .sample_count = depth_texture_desc.sample_count };

  constexpr RootBindingDesc srv_table_desc { // t0, space0
        .binding_slot_desc = BindingSlotDesc {
          .register_index = 0,
          .register_space = 0,
        },
        .visibility = ShaderStageFlags::kAll,
        .data = DescriptorTableBinding {
          .view_type = ResourceViewType::kStructuredBuffer_SRV,
          .base_index = 0 // unbounded
        }
    };

  constexpr RootBindingDesc resource_indices_cbv_desc { // b0, space0
    .binding_slot_desc = BindingSlotDesc {
      .register_index = 0,
      .register_space = 0,
    },
    .visibility = ShaderStageFlags::kAll,
    .data = DirectBufferBinding {}
  };

  constexpr RootBindingDesc scene_constants_cbv_desc { // b1, space0
    .binding_slot_desc = BindingSlotDesc {
      .register_index = 1,
      .register_space = 0,
    },
    .visibility = ShaderStageFlags::kAll,
    .data = DirectBufferBinding {}
  };

  return GraphicsPipelineDesc::Builder()
    .SetVertexShader(ShaderStageDesc {
      .shader = MakeShaderIdentifier(ShaderType::kVertex, "DepthPrePass.hlsl"),
    })
    .SetPixelShader(ShaderStageDesc {
      .shader = MakeShaderIdentifier(ShaderType::kPixel, "DepthPrePass.hlsl"),
    })
    .SetPrimitiveTopology(PrimitiveType::kTriangleList)
    .SetRasterizerState(raster_desc)
    .SetDepthStencilState(ds_desc)
    .SetBlendState({})
    .SetFramebufferLayout(fb_layout_desc)
    // binding 0: SRV table
    .AddRootBinding(RootBindingItem(srv_table_desc))
    // binding 1: ResourceIndices CBV (b0)
    .AddRootBinding(RootBindingItem(resource_indices_cbv_desc))
    // binding 2: SceneConstants CBV (b1)
    .AddRootBinding(RootBindingItem(scene_constants_cbv_desc))
    .Build();
}
