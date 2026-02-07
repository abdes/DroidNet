//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Internal/GpuDebugManager.h>
#include <Oxygen/Renderer/Passes/GpuDebugDrawPass.h>
#include <Oxygen/Renderer/RenderContext.h>

namespace oxygen::engine {

namespace {
  struct GpuDebugDrawPassConstants {
    float mouse_down_x;
    float mouse_down_y;
    uint32_t mouse_down_valid;
    uint32_t pad0;
  };

  static_assert(sizeof(GpuDebugDrawPassConstants) == 16,
    "GpuDebugDrawPassConstants must be 16 bytes");

  // Helper to prepare a render target view for the color texture
  auto PrepareRenderTargetView(const graphics::Texture& color_texture,
    graphics::ResourceRegistry& registry,
    graphics::DescriptorAllocator& allocator) -> oxygen::graphics::NativeView
  {
    using oxygen::TextureType;
    using oxygen::graphics::DescriptorVisibility;
    using oxygen::graphics::ResourceViewType;

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
    return rtv;
  }
} // namespace

GpuDebugDrawPass::GpuDebugDrawPass(observer_ptr<Graphics> /*gfx*/)
  : GraphicsRenderPass("GpuDebugDrawPass", true)
{
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
}

GpuDebugDrawPass::~GpuDebugDrawPass() { ReleasePassConstantsBuffer(); }

auto GpuDebugDrawPass::ValidateConfig() -> void
{
  // No specific configuration required for this pass, but do sanity checks on
  // when it used.
  auto debug_manager = Context().gpu_debug_manager;
  if (!debug_manager) {
    return;
    DCHECK_NOTNULL_F(debug_manager->GetCounterBuffer());
    DCHECK_NOTNULL_F(debug_manager->GetLineBuffer());
  }
}

auto GpuDebugDrawPass::DoPrepareResources(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  auto debug_manager = Context().gpu_debug_manager;
  if (!debug_manager) {
    co_return; // We need a debug manager or this pass is a no-op.
  }
  DCHECK_NOTNULL_F(debug_manager->GetCounterBuffer());
  DCHECK_NOTNULL_F(debug_manager->GetLineBuffer());

  if (!recorder.IsResourceTracked(*debug_manager->GetCounterBuffer())) {
    recorder.BeginTrackingResourceState(
      *debug_manager->GetCounterBuffer(), graphics::ResourceStates::kCommon);
  }

  // Ensure line buffer is in ShaderResource state and counter buffer is in
  // IndirectArgument state for the draw call.
  recorder.RequireResourceState(
    *debug_manager->GetLineBuffer(), graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(*debug_manager->GetCounterBuffer(),
    graphics::ResourceStates::kIndirectArgument);
  recorder.FlushBarriers();

  EnsurePassConstantsBuffer();
  UpdatePassConstants();

  if (color_texture_) {
    recorder.RequireResourceState(
      *color_texture_, graphics::ResourceStates::kRenderTarget);
  }

  co_return;
}

auto GpuDebugDrawPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  auto debug_manager = Context().gpu_debug_manager;
  if (!debug_manager) {
    co_return; // We need a debug manager or this pass is a no-op.
  }

  if (color_texture_) {
    auto& graphics = Context().GetGraphics();
    auto& registry = graphics.GetResourceRegistry();
    auto& allocator = graphics.GetDescriptorAllocator();
    const auto color_rtv
      = PrepareRenderTargetView(*color_texture_, registry, allocator);
    std::array rtvs { color_rtv };
    recorder.SetRenderTargets(std::span(rtvs), std::nullopt);

    const auto& desc = color_texture_->GetDescriptor();
    recorder.SetViewport(ViewPort { .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(desc.width),
      .height = static_cast<float>(desc.height),
      .min_depth = 0.0F,
      .max_depth = 1.0F });
    recorder.SetScissors(Scissors { .left = 0,
      .top = 0,
      .right = static_cast<int32_t>(desc.width),
      .bottom = static_cast<int32_t>(desc.height) });
  }

  static bool logged_execute = false;
  if (!logged_execute) {
    LOG_F(WARNING, "GpuDebugDrawPass: executing indirect draw for debug lines");
    logged_execute = true;
  }

  // Issue the indirect draw call. The counter buffer contains the
  // D3D12_DRAW_ARGUMENTS at offset 0.
  recorder.ExecuteIndirect(*debug_manager->GetCounterBuffer(), 0);

  co_return;
}

auto GpuDebugDrawPass::EnsurePassConstantsBuffer() -> void
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
      "GpuDebugDrawPass: Failed to create pass constants buffer");
  }
  pass_constants_buffer_->SetName(desc.debug_name);

  pass_constants_mapped_ptr_
    = static_cast<std::byte*>(pass_constants_buffer_->Map(0, desc.size_bytes));
  if (!pass_constants_mapped_ptr_) {
    throw std::runtime_error(
      "GpuDebugDrawPass: Failed to map pass constants buffer");
  }

  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
  registry.Register(pass_constants_buffer_);
  for (std::size_t slot = 0; slot < kPassConstantsSlots; ++slot) {
    const uint32_t offset = static_cast<uint32_t>(slot * kPassConstantsStride);

    graphics::BufferViewDescription cbv_view_desc;
    cbv_view_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
    cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_view_desc.range = { offset, kPassConstantsStride };

    auto cbv_handle
      = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!cbv_handle.IsValid()) {
      throw std::runtime_error(
        "GpuDebugDrawPass: Failed to allocate CBV descriptor handle");
    }
    pass_constants_indices_[slot] = allocator.GetShaderVisibleIndex(cbv_handle);

    auto cbv_view = registry.RegisterView(
      *pass_constants_buffer_, std::move(cbv_handle), cbv_view_desc);
    if (!cbv_view->IsValid()) {
      throw std::runtime_error(
        "GpuDebugDrawPass: Failed to register pass constants CBV");
    }
  }
}

auto GpuDebugDrawPass::ReleasePassConstantsBuffer() -> void
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

auto GpuDebugDrawPass::UpdatePassConstants() -> void
{
  CHECK_NOTNULL_F(pass_constants_mapped_ptr_);

  const bool has_mouse_down = mouse_down_position_.has_value();
  const SubPixelPosition mouse_pos = has_mouse_down ? *mouse_down_position_
                                                    : SubPixelPosition {
                                                        .x = 0.0F,
                                                        .y = 0.0F,
                                                      };
  const GpuDebugDrawPassConstants constants {
    .mouse_down_x = mouse_pos.x,
    .mouse_down_y = mouse_pos.y,
    .mouse_down_valid = has_mouse_down ? 1u : 0u,
    .pad0 = 0u,
  };

  static bool logged_mouse_down = false;
  if (has_mouse_down && !logged_mouse_down) {
    LOG_F(WARNING, "GpuDebugDrawPass constants: mouse_down_valid={} x={} y={}",
      constants.mouse_down_valid, constants.mouse_down_x,
      constants.mouse_down_y);
    logged_mouse_down = true;
  }

  const auto slot = pass_constants_slot_ % kPassConstantsSlots;
  pass_constants_slot_++;
  auto* slot_ptr = pass_constants_mapped_ptr_
    + static_cast<std::ptrdiff_t>(slot * kPassConstantsStride);
  std::memcpy(slot_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(pass_constants_indices_[slot]);
}

auto GpuDebugDrawPass::CreatePipelineStateDesc()
  -> graphics::GraphicsPipelineDesc
{
  DCHECK_NOTNULL_F(Context().gpu_debug_manager);

  namespace g = oxygen::graphics;

  auto color_format = Format::kRGBA16Float; // Default to HDR
  auto depth_format = Format::kUnknown;
  uint32_t sample_count = 1;

  if (color_texture_) {
    const auto& desc = color_texture_->GetDescriptor();
    color_format = desc.format;
    sample_count = desc.sample_count;
  } else if (Context().framebuffer) {
    const auto& fb_desc = Context().framebuffer->GetDescriptor();
    if (!fb_desc.color_attachments.empty()
      && fb_desc.color_attachments[0].texture) {
      const auto& desc = fb_desc.color_attachments[0].texture->GetDescriptor();
      color_format = desc.format;
      sample_count = desc.sample_count;
    }
    if (fb_desc.depth_attachment.IsValid()
      && fb_desc.depth_attachment.texture) {
      depth_format = fb_desc.depth_attachment.texture->GetDescriptor().format;
    }
  }

  return graphics::GraphicsPipelineDesc::Builder()
    .SetVertexShader(g::ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "Renderer/GpuDebugDraw.hlsl",
      .entry_point = "VS",
    })
    .SetPixelShader(g::ShaderRequest {
      .stage = ShaderType::kPixel,
      .source_path = "Renderer/GpuDebugDraw.hlsl",
      .entry_point = "PS",
    })
    .SetPrimitiveTopology(g::PrimitiveType::kLineList)
    .SetDepthStencilState(g::DepthStencilStateDesc {
      .depth_test_enable = false,
      .depth_write_enable = false,
    })
    .SetFramebufferLayout(g::FramebufferLayoutDesc {
      .color_target_formats = { color_format },
      .depth_stencil_format = depth_format,
      .sample_count = sample_count,
    })
    .SetRootBindings(BuildRootBindings())
    .Build();
}

auto GpuDebugDrawPass::NeedRebuildPipelineState() const -> bool
{
  const auto& last_built = LastBuiltPsoDesc();
  if (!last_built) {
    return true;
  }

  auto color_format = Format::kRGBA16Float;
  auto depth_format = Format::kUnknown;
  uint32_t sample_count = 1;

  if (color_texture_) {
    const auto& desc = color_texture_->GetDescriptor();
    color_format = desc.format;
    sample_count = desc.sample_count;
  } else if (Context().framebuffer) {
    const auto& fb_desc = Context().framebuffer->GetDescriptor();
    if (!fb_desc.color_attachments.empty()
      && fb_desc.color_attachments[0].texture) {
      const auto& desc = fb_desc.color_attachments[0].texture->GetDescriptor();
      color_format = desc.format;
      sample_count = desc.sample_count;
    }
    if (fb_desc.depth_attachment.IsValid()
      && fb_desc.depth_attachment.texture) {
      depth_format = fb_desc.depth_attachment.texture->GetDescriptor().format;
    }
  }

  const auto& last_layout = last_built->FramebufferLayout();
  return last_layout.color_target_formats.empty()
    || last_layout.color_target_formats[0] != color_format
    || last_layout.depth_stencil_format != depth_format
    || last_layout.sample_count != sample_count;
}

} // namespace oxygen::engine
