//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Passes/WireframePass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/MaterialPermutations.h>
#include <Oxygen/Renderer/Types/PassMask.h>


using oxygen::Scissors;
using oxygen::TextureType;
using oxygen::ViewPort;
using oxygen::engine::WireframePass;
using oxygen::engine::WireframePassConfig;
using oxygen::graphics::Buffer;
using oxygen::graphics::Color;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::DescriptorAllocator;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceRegistry;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;

namespace {
struct alignas(16) WireframePassConstants {
  Color wire_color;
  float apply_exposure_compensation;
  float padding[3];
};

static_assert(sizeof(WireframePassConstants) == 32,
  "WireframePassConstants must be 32 bytes");
} // namespace

WireframePass::WireframePass(std::shared_ptr<WireframePassConfig> config)
  : GraphicsRenderPass(config ? config->debug_name : "WireframePass")
  , config_(std::move(config))
{
}

WireframePass::~WireframePass()
{
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
  pass_constants_cbv_ = {};
  pass_constants_index_ = kInvalidShaderVisibleIndex;
  pass_constants_buffer_.reset();
}

auto WireframePass::SetWireColor(const Color& color) -> void
{
  if (!config_) {
    return;
  }
  if (config_->wire_color == color) {
    return;
  }
  LOG_F(INFO, "WireframePass: SetWireColor ({}, {}, {}, {})", color.r, color.g,
    color.b, color.a);
  config_->wire_color = color;
  pass_constants_dirty_ = true;
}

auto WireframePass::ValidateConfig() -> void
{
  // Will throw if no valid color texture is found.
  [[maybe_unused]] const auto& _ = GetColorTexture();
}

auto WireframePass::DoPrepareResources(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  recorder.RequireResourceState(
    GetColorTexture(), graphics::ResourceStates::kRenderTarget);

  const auto* fb = GetFramebuffer();
  if (fb && fb->GetDescriptor().depth_attachment.IsValid()
    && fb->GetDescriptor().depth_attachment.texture) {
    const auto state = (config_ && config_->depth_write_enable)
      ? graphics::ResourceStates::kDepthWrite
      : graphics::ResourceStates::kDepthRead;
    recorder.RequireResourceState(
      *fb->GetDescriptor().depth_attachment.texture, state);
  }

  recorder.FlushBarriers();

  const bool need_constants_init = !pass_constants_buffer_
    || pass_constants_index_ == kInvalidShaderVisibleIndex;
  if (need_constants_init) {
    auto& graphics = Context().GetGraphics();
    auto& registry = graphics.GetResourceRegistry();
    auto& allocator = graphics.GetDescriptorAllocator();

    const graphics::BufferDesc desc {
      .size_bytes = 256u,
      .usage = graphics::BufferUsage::kConstant,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = std::string { GetName() } + "_PassConstants",
    };

    pass_constants_buffer_ = graphics.CreateBuffer(desc);
    if (!pass_constants_buffer_) {
      throw std::runtime_error(
        "WireframePass: Failed to create pass constants buffer");
    }
    pass_constants_buffer_->SetName(desc.debug_name);

    pass_constants_mapped_ptr_
      = pass_constants_buffer_->Map(0, desc.size_bytes);
    if (!pass_constants_mapped_ptr_) {
      throw std::runtime_error(
        "WireframePass: Failed to map pass constants buffer");
    }

    graphics::BufferViewDescription cbv_view_desc;
    cbv_view_desc.view_type = ResourceViewType::kConstantBuffer;
    cbv_view_desc.visibility = DescriptorVisibility::kShaderVisible;
    cbv_view_desc.range = { 0u, desc.size_bytes };

    auto cbv_handle = allocator.Allocate(
      ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
    if (!cbv_handle.IsValid()) {
      throw std::runtime_error(
        "WireframePass: Failed to allocate CBV descriptor handle");
    }
    pass_constants_index_ = allocator.GetShaderVisibleIndex(cbv_handle);

    registry.Register(pass_constants_buffer_);
    pass_constants_cbv_ = registry.RegisterView(
      *pass_constants_buffer_, std::move(cbv_handle), cbv_view_desc);
    if (!pass_constants_cbv_->IsValid()) {
      throw std::runtime_error(
        "WireframePass: Failed to register pass constants CBV");
    }
    pass_constants_dirty_ = true;
  }

  if (pass_constants_dirty_) {
    const auto wire_color
      = (config_ ? config_->wire_color : Color { 1.0F, 1.0F, 1.0F, 1.0F });
    const float apply_exposure_compensation
      = (config_ && config_->apply_exposure_compensation) ? 1.0F : 0.0F;
    const WireframePassConstants snapshot {
      .wire_color = wire_color,
      .apply_exposure_compensation = apply_exposure_compensation,
      .padding = { 0.0F, 0.0F, 0.0F },
    };
    LOG_F(INFO,
      "WireframePass: Upload pass constants wire_color=({}, {}, {}, {})",
      snapshot.wire_color.r, snapshot.wire_color.g, snapshot.wire_color.b,
      snapshot.wire_color.a);
    std::memcpy(pass_constants_mapped_ptr_, &snapshot, sizeof(snapshot));
    pass_constants_dirty_ = false;
  }

  SetPassConstantsIndex(pass_constants_index_);

  co_return;
}

auto WireframePass::SetupRenderTargets(CommandRecorder& recorder) const -> void
{
  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();
  auto& color_texture = const_cast<Texture&>(GetColorTexture());

  const auto& tex_desc = color_texture.GetDescriptor();
  graphics::TextureViewDescription rtv_view_desc { .view_type
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

  auto rtv = registry.Find(color_texture, rtv_view_desc);
  if (!rtv->IsValid()) {
    auto rtv_desc_handle = allocator.Allocate(
      ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly);
    if (!rtv_desc_handle.IsValid()) {
      throw std::runtime_error(
        "WireframePass: Failed to allocate RTV descriptor handle");
    }
    rtv = registry.RegisterView(
      color_texture, std::move(rtv_desc_handle), rtv_view_desc);
    if (!rtv->IsValid()) {
      throw std::runtime_error(
        "WireframePass: Failed to register RTV with resource registry");
    }
  }
  std::array rtvs { rtv };

  graphics::NativeView dsv = {};
  const Texture* depth_texture_ptr = nullptr;
  if (const auto* fb = GetFramebuffer(); fb
    && fb->GetDescriptor().depth_attachment.IsValid()
    && fb->GetDescriptor().depth_attachment.texture) {
    auto& depth_texture = *fb->GetDescriptor().depth_attachment.texture;
    depth_texture_ptr = &depth_texture;
    graphics::TextureViewDescription dsv_view_desc { .view_type
      = ResourceViewType::kTexture_DSV,
      .visibility = DescriptorVisibility::kCpuOnly,
      .format = depth_texture.GetDescriptor().format,
      .dimension = depth_texture.GetDescriptor().texture_type,
      .sub_resources = { .base_mip_level = 0,
        .num_mip_levels = depth_texture.GetDescriptor().mip_levels,
        .base_array_slice = 0,
        .num_array_slices
        = (depth_texture.GetDescriptor().texture_type == TextureType::kTexture3D
            ? depth_texture.GetDescriptor().depth
            : depth_texture.GetDescriptor().array_size) },
      .is_read_only_dsv = !(config_ && config_->depth_write_enable) };

    if (auto found = registry.Find(depth_texture, dsv_view_desc);
      found->IsValid()) {
      dsv = found;
    } else {
      auto dsv_desc_handle = allocator.Allocate(
        ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);
      if (!dsv_desc_handle.IsValid()) {
        throw std::runtime_error(
          "WireframePass: Failed to allocate DSV descriptor handle");
      }
      dsv = registry.RegisterView(
        depth_texture, std::move(dsv_desc_handle), dsv_view_desc);
    }
  }

  if (dsv->IsValid()) {
    recorder.SetRenderTargets(std::span(rtvs), dsv);
  } else {
    recorder.SetRenderTargets(std::span(rtvs), std::nullopt);
  }

  const bool clear_depth = config_ && config_->clear_depth_target
    && config_->depth_write_enable && depth_texture_ptr;
  if (clear_depth) {
    recorder.ClearDepthStencilView(
      *depth_texture_ptr, dsv, graphics::ClearFlags::kDepth, 1.0F, 0);
  }

  const bool clear_enabled = (!config_) || config_->clear_color_target;
  if (clear_enabled) {
    if (const auto* fb_to_clear = GetFramebuffer()) {
      recorder.ClearFramebuffer(*fb_to_clear,
        std::vector<std::optional<graphics::Color>> { GetClearColor() },
        std::nullopt, std::nullopt);
    }
  }
}

auto WireframePass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  SetupViewPortAndScissors(recorder);
  SetupRenderTargets(recorder);

  const auto psf = Context().current_view.prepared_frame;
  if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()) {
    Context().RegisterPass(this);
    co_return;
  }
  DCHECK_F(pso_opaque_single_.has_value());
  DCHECK_F(pso_opaque_double_.has_value());
  DCHECK_F(pso_masked_single_.has_value());
  DCHECK_F(pso_masked_double_.has_value());

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
    if (!md.flags.IsSet(oxygen::engine::PassMaskBit::kOpaque)
      && !md.flags.IsSet(oxygen::engine::PassMaskBit::kMasked)
      && !md.flags.IsSet(oxygen::engine::PassMaskBit::kTransparent)) {
      continue;
    }

    const bool is_masked = md.flags.IsSet(oxygen::engine::PassMaskBit::kMasked);
    const bool is_double_sided
      = md.flags.IsSet(oxygen::engine::PassMaskBit::kDoubleSided);

    // Wireframe selects PSOs per partition (opaque/masked, single/double
    // sided). Unlike most passes, it switches PSOs inside the draw loop.
    const auto& pso_desc = [&]() -> const graphics::GraphicsPipelineDesc& {
      if (is_masked) {
        return is_double_sided ? *pso_masked_double_ : *pso_masked_single_;
      }
      return is_double_sided ? *pso_opaque_double_ : *pso_opaque_single_;
    }();

    if (current_pso != &pso_desc) {
      recorder.SetPipelineState(pso_desc);
      // PSO changes rebind the root signature and invalidate root constants.
      // Rebind pass constants so the wire color CBV index remains valid.
      RebindCommonRootParameters(recorder);
      current_pso = &pso_desc;
    }

    EmitDrawRange(recorder, records, draw_index, draw_index + 1, emitted_count,
      skipped_invalid, draw_errors);
  }

  if (emitted_count > 0 || skipped_invalid > 0 || draw_errors > 0) {
    DLOG_F(2, "WireframePass: emitted={}, skipped_invalid={}, errors={}",
      emitted_count, skipped_invalid, draw_errors);
  }

  Context().RegisterPass(this);
  co_return;
}

auto WireframePass::GetColorTexture() const -> const Texture&
{
  if (config_ && config_->color_texture) {
    return *config_->color_texture;
  }
  const auto* fb = GetFramebuffer();
  if (fb && !fb->GetDescriptor().color_attachments.empty()
    && fb->GetDescriptor().color_attachments[0].texture) {
    return *fb->GetDescriptor().color_attachments[0].texture;
  }
  throw std::runtime_error("WireframePass: No valid color texture found.");
}

auto WireframePass::GetFramebuffer() const -> const Framebuffer*
{
  return Context().framebuffer.get();
}

auto WireframePass::GetClearColor() const -> const Color&
{
  if (config_ && config_->clear_color.has_value()) {
    return *config_->clear_color;
  }
  const auto& color_tex_desc = GetColorTexture().GetDescriptor();
  return color_tex_desc.clear_value;
}

auto WireframePass::HasDepth() const -> bool
{
  const auto* fb = GetFramebuffer();
  return fb && fb->GetDescriptor().depth_attachment.IsValid();
}

auto WireframePass::SetupViewPortAndScissors(CommandRecorder& recorder) const
  -> void
{
  const auto& tex_desc = GetColorTexture().GetDescriptor();
  const auto width = tex_desc.width;
  const auto height = tex_desc.height;

  const ViewPort viewport { .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0F,
    .max_depth = 1.0F };
  recorder.SetViewport(viewport);

  const Scissors scissors { .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(width),
    .bottom = static_cast<int32_t>(height) };
  recorder.SetScissors(scissors);
}

auto WireframePass::CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc
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

  const auto depth_write_enable = config_ && config_->depth_write_enable;

  DepthStencilStateDesc ds_desc {
    .depth_test_enable = has_depth,
    .depth_write_enable = depth_write_enable,
    .depth_func = CompareOp::kLessOrEqual,
    .stencil_enable = false,
    .stencil_read_mask = 0xFF,
    .stencil_write_mask = 0xFF,
  };

  RasterizerStateDesc raster_desc { .fill_mode = FillMode::kWireframe,
    .cull_mode = CullMode::kBack,
    .front_counter_clockwise = true,
    .multisample_enable = false };

  const auto& color_tex_desc = GetColorTexture().GetDescriptor();
  const FramebufferLayoutDesc fb_layout_desc {
    .color_target_formats = { color_tex_desc.format },
    .depth_stencil_format = depth_format,
    .sample_count = sample_count,
  };

  auto generated_bindings = BuildRootBindings();

  const auto BuildDesc
    = [&](CullMode cull_mode, std::vector<graphics::ShaderDefine> defines) {
        RasterizerStateDesc raster = raster_desc;
        raster.cull_mode = cull_mode;

        return GraphicsPipelineDesc::Builder()
          .SetVertexShader(ShaderRequest {
            .stage = ShaderType::kVertex,
            .source_path = "Passes/Forward/ForwardMesh_VS.hlsl",
            .entry_point = "VS",
            .defines = {},
          })
          .SetPixelShader(ShaderRequest {
            .stage = ShaderType::kPixel,
            .source_path = "Passes/Forward/ForwardWireframe_PS.hlsl",
            .entry_point = "PS",
            .defines = defines,
          })
          .SetPrimitiveTopology(PrimitiveType::kTriangleList)
          .SetRasterizerState(raster)
          .SetDepthStencilState(ds_desc)
          .SetBlendState({})
          .SetFramebufferLayout(fb_layout_desc)
          .SetRootBindings(std::span<const graphics::RootBindingItem>(
            generated_bindings.data(), generated_bindings.size()))
          .Build();
      };

  pso_opaque_single_
    = BuildDesc(CullMode::kBack, ToDefines(permutation::kOpaqueDefines));
  pso_opaque_double_
    = BuildDesc(CullMode::kNone, ToDefines(permutation::kOpaqueDefines));
  pso_masked_single_
    = BuildDesc(CullMode::kBack, ToDefines(permutation::kMaskedDefines));
  pso_masked_double_
    = BuildDesc(CullMode::kNone, ToDefines(permutation::kMaskedDefines));

  return *pso_opaque_single_;
}

auto WireframePass::NeedRebuildPipelineState() const -> bool
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

  if (last_built->RasterizerState().fill_mode
    != graphics::FillMode::kWireframe) {
    return true;
  }

  const bool depth_write_enable = config_ && config_->depth_write_enable;
  if (last_built->DepthStencilState().depth_write_enable
    != depth_write_enable) {
    return true;
  }

  return false;
}
