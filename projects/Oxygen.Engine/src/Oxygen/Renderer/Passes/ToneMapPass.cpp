//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ToneMapPass.h"

#include <algorithm>
#include <array>
#include <cmath>
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
  constexpr float kDefaultManualExposure = 1.0F;
  constexpr float kDefaultGamma = 2.2F;

  auto SanitizeManualExposure(const float manual_exposure) -> float
  {
    if (!std::isfinite(manual_exposure)) {
      return kDefaultManualExposure;
    }
    return std::max(manual_exposure, 0.0F);
  }

  auto SanitizeGamma(const float gamma) -> float
  {
    if (!std::isfinite(gamma) || gamma <= 0.0F) {
      return kDefaultGamma;
    }
    return gamma;
  }

  auto CheckTrackedTexture(const graphics::CommandRecorder& recorder,
    const graphics::Texture& texture, const char* role) -> void
  {
    CHECK_F(recorder.IsResourceTracked(texture),
      "{} texture '{}' must already have resource-state tracking "
      "before execution",
      role, texture.GetDescriptor().debug_name);
  }

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
      = allocator.AllocateRaw(graphics::ResourceViewType::kTexture_RTV,
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

ToneMapPass::ToneMapPass(std::shared_ptr<ToneMapPassConfig> config)
  : GraphicsRenderPass(config ? config->debug_name : "ToneMapPass", false)
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

  const auto& output = GetOutputTexture();
  if (&GetSourceTexture() == &output) {
    throw std::runtime_error(
      "ToneMapPass: source texture and output texture must be distinct");
  }

  if (!std::isfinite(config_->manual_exposure)) {
    LOG_F(WARNING, "invalid manual_exposure={}, resetting to default {}",
      config_->manual_exposure, kDefaultManualExposure);
    config_->manual_exposure = kDefaultManualExposure;
  } else if (config_->manual_exposure < 0.0F) {
    LOG_F(WARNING, "negative manual_exposure={}, clamping to 0",
      config_->manual_exposure);
    config_->manual_exposure = 0.0F;
  }

  if (!std::isfinite(config_->gamma) || config_->gamma <= 0.0F) {
    LOG_F(WARNING, "invalid gamma={}, resetting to default {}", config_->gamma,
      kDefaultGamma);
    config_->gamma = kDefaultGamma;
  }
}

auto ToneMapPass::DoPrepareResources(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  const auto& source = GetSourceTexture();
  const auto& output = GetOutputTexture();
  DLOG_F(2, "source ptr={} size={}x{} fmt={} name={}", fmt::ptr(&source),
    source.GetDescriptor().width, source.GetDescriptor().height,
    source.GetDescriptor().format, source.GetDescriptor().debug_name);
  DLOG_F(2, "output ptr={} size={}x{} fmt={} name={}", fmt::ptr(&output),
    output.GetDescriptor().width, output.GetDescriptor().height,
    output.GetDescriptor().format, output.GetDescriptor().debug_name);
  DLOG_F(2, "exposure={} tonemapper={}", config_->manual_exposure,
    config_->tone_mapper);

  // ToneMapPass does not own the scene HDR/SDR textures, so it must not guess
  // or overwrite their initial tracker state. Callers must seed tracking.
  CheckTrackedTexture(recorder, source, "source");
  CheckTrackedTexture(recorder, output, "output");

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
  const auto* fb = Context().pass_target.get();
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
  graphics_ = observer_ptr { &graphics };
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
      = allocator.AllocateRaw(graphics::ResourceViewType::kConstantBuffer,
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
    pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
    pass_constants_slot_ = 0u;
    SetPassConstantsIndex(kInvalidShaderVisibleIndex);
    return;
  }

  if (pass_constants_buffer_->IsMapped()) {
    pass_constants_buffer_->UnMap();
  }

  if (graphics_ != nullptr) {
    auto& registry = graphics_->GetResourceRegistry();
    if (registry.Contains(*pass_constants_buffer_)) {
      registry.UnRegisterResource(*pass_constants_buffer_);
    }
  }

  pass_constants_mapped_ptr_ = nullptr;
  pass_constants_buffer_.reset();
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
  pass_constants_slot_ = 0u;
  SetPassConstantsIndex(kInvalidShaderVisibleIndex);
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

  // Reuse the canonical shader-visible SRV already registered for this
  // texture/description pair when available.
  if (const auto existing_index
    = registry.FindShaderVisibleIndex(texture, srv_desc);
    existing_index.has_value()) {
    return *existing_index;
  }

  if (registry.Contains(texture, srv_desc)) {
    throw std::runtime_error(
      "ToneMapPass: source SRV exists in registry without a shader-visible "
      "index");
  }

  auto srv_handle
    = allocator.AllocateRaw(graphics::ResourceViewType::kTexture_SRV,
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

  return srv_index;
}

auto ToneMapPass::UpdatePassConstants(ShaderVisibleIndex source_texture_index)
  -> void
{
  CHECK_NOTNULL_F(pass_constants_mapped_ptr_);

  const float manual_exposure
    = SanitizeManualExposure(config_->manual_exposure);
  const float gamma = SanitizeGamma(config_->gamma);
  float cpu_exposure_constant = manual_exposure;
  ShaderVisibleIndex exposure_buffer_index = kInvalidShaderVisibleIndex;
  uint32_t debug_flags = 0u;
  bool fallback_used = false;
  const char* shader_exposure_source = "manual";
  const auto current_view_id = Context().current_view.view_id;
  const auto exposure_view_id
    = Context().current_view.exposure_view_id != kInvalidViewId
    ? Context().current_view.exposure_view_id
    : current_view_id;
  if (config_->exposure_mode == ExposureMode::kAuto) {
    debug_flags |= 1u; // want auto exposure
    const auto* ae = Context().GetPass<AutoExposurePass>();

    if (ae != nullptr) {
      debug_flags |= 2u; // AutoExposurePass found
      exposure_buffer_index
        = ae->GetExposureOutput(exposure_view_id).exposure_state_srv_index;
    }

    if (exposure_buffer_index.IsValid()) {
      debug_flags |= 4u; // exposure buffer valid
      shader_exposure_source = "buffer";
    } else {
      shader_exposure_source = "prepared_fallback";
    }

    // Fallback: if auto exposure pass did not run, use the resolved view
    // exposure captured during scene prep.
    if (!exposure_buffer_index.IsValid()) {
      if (const auto* prepared = Context().current_view.prepared_frame.get()) {
        debug_flags |= 8u; // used prepared-frame fallback exposure
        fallback_used = true;
        cpu_exposure_constant = std::max(prepared->exposure, 0.0F);
      } else {
        shader_exposure_source = "manual_fallback";
      }
    }
  }
  DLOG_F(2,
    "view={} exposure_view={} exposure_mode={} "
    "shader_exposure_source={} "
    "manual_exposure={:.6f} cpu_exposure_constant={:.6f} "
    "exposure_buffer_index={} fallback_used={} debug_flags=0x{:x} "
    "prepared_exposure={:.6f}",
    current_view_id.get(), exposure_view_id.get(),
    static_cast<std::uint32_t>(config_->exposure_mode), shader_exposure_source,
    config_->manual_exposure, cpu_exposure_constant,
    exposure_buffer_index.get(), fallback_used, debug_flags,
    Context().current_view.prepared_frame != nullptr
      ? Context().current_view.prepared_frame->exposure
      : -1.0F);
  const ToneMapPassConstants constants {
    .source_texture_index = source_texture_index.get(),
    .sampler_index = 0u,
    .exposure_buffer_index = exposure_buffer_index.get(),
    .tone_mapper = static_cast<uint32_t>(config_->tone_mapper),
    .exposure = cpu_exposure_constant,
    .gamma = gamma,
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
