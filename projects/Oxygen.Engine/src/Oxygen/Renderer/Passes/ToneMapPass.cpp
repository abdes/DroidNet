//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ToneMapPass.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

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
#include <Oxygen/Renderer/Passes/AutoExposurePass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>

namespace oxygen::engine {

namespace {
  struct alignas(16) ToneMapPassConstants {
    uint32_t source_texture_index;
    uint32_t sampler_index;
    uint32_t exposure_buffer_index;
    uint32_t tone_mapper;
    float exposure;
    float gamma;
    uint32_t debug_flags;
    float _pad0;
  };

  static_assert(sizeof(ToneMapPassConstants) == 32,
    "ToneMapPassConstants must be 32 bytes");

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
        "ToneMapPass: Failed to allocate RTV descriptor handle");
    }

    const auto rtv = registry.RegisterView(
      color_texture, std::move(rtv_desc_handle), rtv_view_desc);
    if (!rtv->IsValid()) {
      throw std::runtime_error(
        "ToneMapPass: Failed to register RTV with resource registry");
    }
    return rtv;
  }
} // namespace

auto to_string(ExposureMode mode) -> std::string
{
  switch (mode) {
  case ExposureMode::kManual:
    return "manual";
  case ExposureMode::kAuto:
    return "auto";
  case ExposureMode::kManualCamera:
    return "manual_camera";
  }
  return "unknown";
}

auto to_string(ToneMapper mapper) -> std::string
{
  switch (mapper) {
  case ToneMapper::kAcesFitted:
    return "aces";
  case ToneMapper::kReinhard:
    return "reinhard";
  case ToneMapper::kFilmic:
    return "filmic";
  case ToneMapper::kNone:
    return "none";
  }
  return "unknown";
}

ToneMapPass::ToneMapPass(std::shared_ptr<ToneMapPassConfig> config)
  : GraphicsRenderPass(config ? config->debug_name : "ToneMapPass", true)
  , config_(std::move(config))
{
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
}

ToneMapPass::~ToneMapPass() { ReleasePassConstantsBuffer(); }

auto ToneMapPass::ValidateConfig() -> void
{
  if (!config_) {
    throw std::runtime_error("ToneMapPass: missing configuration");
  }
  if (!config_->source_texture) {
    throw std::runtime_error("ToneMapPass: source texture is required");
  }
  // Output texture can be null - we'll use framebuffer in that case
}

auto ToneMapPass::DoPrepareResources(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  const auto& source = GetSourceTexture();
  const auto& output = GetOutputTexture();
  const auto& src_desc = source.GetDescriptor();
  const auto& out_desc = output.GetDescriptor();

  DLOG_F(2, "source ptr={} size={}x{} fmt={} name={}", fmt::ptr(&source),
    src_desc.width, src_desc.height, src_desc.format, src_desc.debug_name);
  DLOG_F(2, "output ptr={} size={}x{} fmt={} name={}", fmt::ptr(&output),
    out_desc.width, out_desc.height, out_desc.format, out_desc.debug_name);
  DLOG_F(2, "exposure={} tonemapper={}", config_->manual_exposure,
    config_->tone_mapper);

  recorder.RequireResourceState(
    source, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    output, graphics::ResourceStates::kRenderTarget);
  recorder.FlushBarriers();

  EnsurePassConstantsBuffer();

  const auto source_srv = EnsureSourceTextureSrv(source);
  if (!source_srv.IsValid()) {
    throw std::runtime_error("ToneMapPass: invalid source SRV index");
  }
  UpdatePassConstants(source_srv);

  co_return;
}

auto ToneMapPass::DoExecute(graphics::CommandRecorder& recorder) -> co::Co<>
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

auto ToneMapPass::SetupRenderTargets(graphics::CommandRecorder& recorder) const
  -> void
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

auto ToneMapPass::SetupViewPortAndScissors(
  graphics::CommandRecorder& recorder) const -> void
{
  const auto& output_desc = GetOutputTexture().GetDescriptor();

  const ViewPort viewport {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(output_desc.width),
    .height = static_cast<float>(output_desc.height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  recorder.SetViewport(viewport);

  const Scissors scissors {
    .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(output_desc.width),
    .bottom = static_cast<int32_t>(output_desc.height),
  };
  recorder.SetScissors(scissors);
}

auto ToneMapPass::GetOutputTexture() const -> const graphics::Texture&
{
  // If output_texture is set in config, use it
  if (config_ && config_->output_texture) {
    return *config_->output_texture;
  }

  // Otherwise, use framebuffer color attachment
  const auto* fb = Context().framebuffer.get();
  if (!fb) {
    throw std::runtime_error("ToneMapPass: framebuffer is null");
  }
  const auto& fb_desc = fb->GetDescriptor();
  if (fb_desc.color_attachments.empty()
    || !fb_desc.color_attachments[0].texture) {
    throw std::runtime_error("ToneMapPass: missing color attachment");
  }
  return *fb_desc.color_attachments[0].texture;
}

auto ToneMapPass::GetSourceTexture() const -> const graphics::Texture&
{
  CHECK_NOTNULL_F(config_);
  CHECK_F(static_cast<bool>(config_->source_texture),
    "ToneMapPass requires a source texture");
  return *config_->source_texture;
}

auto ToneMapPass::EnsurePassConstantsBuffer() -> void
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
      "ToneMapPass: Failed to create pass constants buffer");
  }
  pass_constants_buffer_->SetName(desc.debug_name);

  pass_constants_mapped_ptr_
    = static_cast<std::byte*>(pass_constants_buffer_->Map(0, desc.size_bytes));
  if (!pass_constants_mapped_ptr_) {
    throw std::runtime_error(
      "ToneMapPass: Failed to map pass constants buffer");
  }

  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
  registry.Register(pass_constants_buffer_);
  for (size_t slot = 0; slot < kPassConstantsSlots; ++slot) {
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
        "ToneMapPass: Failed to allocate CBV descriptor handle");
    }
    pass_constants_indices_[slot] = allocator.GetShaderVisibleIndex(cbv_handle);

    auto cbv_view = registry.RegisterView(
      *pass_constants_buffer_, std::move(cbv_handle), cbv_view_desc);
    if (!cbv_view->IsValid()) {
      throw std::runtime_error(
        "ToneMapPass: Failed to register pass constants CBV");
    }
  }
}

auto ToneMapPass::ReleasePassConstantsBuffer() -> void
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

auto ToneMapPass::EnsureSourceTextureSrv(const graphics::Texture& texture)
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

  // The resource registry aborts if we try to register a duplicate view (same
  // resource + same description). This can occur if this pass is re-created
  // (or its local cache is cleared) while the global registry still holds the
  // prior view. In that case, explicitly unregister the stale view first.
  const bool registry_has_view = registry.Contains(texture, srv_desc);

  if (auto it = source_texture_srvs_.find(&texture);
    it != source_texture_srvs_.end()) {
    if (registry_has_view) {
      return it->second;
    }
    source_texture_srvs_.erase(it);
  } else if (registry_has_view) {
    // If the registry already has this view, use its index.
    const auto existing_index
      = registry.FindShaderVisibleIndex(texture, srv_desc);
    if (existing_index.has_value()) {
      source_texture_srvs_[&texture] = *existing_index;
      return *existing_index;
    }
  }

  auto srv_handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!srv_handle.IsValid()) {
    throw std::runtime_error(
      "ToneMapPass: Failed to allocate source SRV handle");
  }

  const auto srv_index = allocator.GetShaderVisibleIndex(srv_handle);
  auto srv_view = registry.RegisterView(
    const_cast<graphics::Texture&>(texture), std::move(srv_handle), srv_desc);
  if (!srv_view->IsValid()) {
    throw std::runtime_error("ToneMapPass: Failed to register source SRV view");
  }

  source_texture_srvs_[&texture] = srv_index;
  return srv_index;
}

auto ToneMapPass::UpdatePassConstants(ShaderVisibleIndex source_texture_index)
  -> void
{
  CHECK_NOTNULL_F(pass_constants_mapped_ptr_);

  float exposure = std::max(config_->manual_exposure, 0.0F);
  ShaderVisibleIndex exposure_buffer_index = kInvalidShaderVisibleIndex;
  uint32_t debug_flags = 0u;
  if (config_->exposure_mode == ExposureMode::kAuto) {
    debug_flags |= 1u; // want auto exposure
    const auto view_id = Context().current_view.view_id;
    const auto* ae = Context().GetPass<AutoExposurePass>();

    if (ae != nullptr) {
      debug_flags |= 2u; // AutoExposurePass found
      exposure_buffer_index
        = ae->GetExposureOutput(view_id).exposure_state_srv_index;
    }

    if (ae == nullptr) {
      LOG_F(ERROR,
        "ToneMapPass: Auto exposure requested, but AutoExposurePass is not "
        "registered (view_id={})",
        view_id.get());
    }

    if (exposure_buffer_index.IsValid()) {
      debug_flags |= 4u; // exposure buffer valid
    } else {
      LOG_F(ERROR,
        "ToneMapPass: Auto exposure requested, but exposure buffer SRV index "
        "is invalid (view_id={}, ae_registered={})",
        view_id.get(), ae != nullptr);
    }

    // Fallback: if auto exposure pass did not run, use the resolved view
    // exposure captured during scene prep.
    if (!exposure_buffer_index.IsValid()) {
      if (const auto* prepared = Context().current_view.prepared_frame.get()) {
        debug_flags |= 8u; // used prepared-frame fallback exposure
        exposure = std::max(prepared->exposure, 0.0F);
      }
    }
  }
  const ToneMapPassConstants constants {
    .source_texture_index = source_texture_index.get(),
    .sampler_index = 0u,
    .exposure_buffer_index = exposure_buffer_index.get(),
    .tone_mapper = static_cast<uint32_t>(config_->tone_mapper),
    .exposure = exposure,
    .gamma = config_->gamma,
    .debug_flags = debug_flags,
    ._pad0 = 0.0F,
  };

  const auto slot = pass_constants_slot_ % kPassConstantsSlots;
  pass_constants_slot_++;
  auto* slot_ptr = pass_constants_mapped_ptr_
    + static_cast<std::ptrdiff_t>(slot * kPassConstantsStride);
  std::memcpy(slot_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(pass_constants_indices_[slot]);
}

auto ToneMapPass::CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc
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

  // No blending for tonemapping - we're writing the final result
  const BlendTargetDesc blend_desc {
    .blend_enable = false,
    .src_blend = BlendFactor::kOne,
    .dest_blend = BlendFactor::kZero,
    .blend_op = BlendOp::kAdd,
    .src_blend_alpha = BlendFactor::kOne,
    .dest_blend_alpha = BlendFactor::kZero,
    .blend_op_alpha = BlendOp::kAdd,
    .write_mask = ColorWriteMask::kAll,
  };

  auto root_bindings = BuildRootBindings();

  return GraphicsPipelineDesc::Builder()
    .SetVertexShader(ShaderRequest {
      .stage = oxygen::ShaderType::kVertex,
      .source_path = "Compositing/ToneMap_VS.hlsl",
      .entry_point = "VS",
      .defines = {},
    })
    .SetPixelShader(ShaderRequest {
      .stage = oxygen::ShaderType::kPixel,
      .source_path = "Compositing/ToneMap_PS.hlsl",
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

auto ToneMapPass::NeedRebuildPipelineState() const -> bool
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
