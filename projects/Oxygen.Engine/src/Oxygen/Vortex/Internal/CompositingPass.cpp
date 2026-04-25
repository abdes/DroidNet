//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

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
#include <Oxygen/Vortex/Internal/CompositingAlphaSanitizer.h>
#include <Oxygen/Vortex/Internal/CompositingPass.h>
#include <Oxygen/Vortex/RenderContext.h>

namespace oxygen::vortex::internal {

namespace {

  auto SanitizeAlphaForConfig(const std::string_view debug_name,
    const float alpha) -> oxygen::vortex::internal::detail::SanitizedAlphaResult
  {
    static oxygen::vortex::internal::detail::AlphaWarningLimiter limiter {};
    return oxygen::vortex::internal::detail::SanitizeCompositingAlpha(
      debug_name, alpha, limiter);
  }

  auto CheckTrackedTexture(const graphics::CommandRecorder& recorder,
    const graphics::Texture& texture, const char* role) -> void
  {
    CHECK_F(recorder.IsResourceTracked(texture),
      "{} texture '{}' must already have "
      "resource-state tracking before execution",
      role, texture.GetDescriptor().debug_name);
  }

  auto HasPositiveArea(const ViewPort& viewport) -> bool
  {
    return viewport.width > 0.0F && viewport.height > 0.0F;
  }

  auto ResolveAttachmentFormat(
    const graphics::FramebufferAttachment& attachment) -> Format
  {
    CHECK_NOTNULL_F(attachment.texture.get());
    if (attachment.format != Format::kUnknown) {
      return attachment.format;
    }
    return attachment.texture->GetDescriptor().format;
  }

  auto ResolveAttachmentExtent(
    const graphics::FramebufferAttachment& attachment)
    -> std::pair<uint32_t, uint32_t>
  {
    CHECK_NOTNULL_F(attachment.texture.get());
    const auto& texture_desc = attachment.texture->GetDescriptor();
    const auto sub_resources
      = attachment.sub_resources.Resolve(texture_desc, true);
    const auto mip_level = sub_resources.base_mip_level;

    return {
      (std::max)(1U, texture_desc.width >> mip_level),
      (std::max)(1U, texture_desc.height >> mip_level),
    };
  }

  struct CompositingPassConstants {
    uint32_t source_texture_index;
    uint32_t sampler_index;
    float alpha;
    float pad0;
  };

  static_assert(sizeof(CompositingPassConstants) == 16,
    "CompositingPassConstants must be 16 bytes");

  auto ClampViewport(const ViewPort& viewport, const uint32_t target_width,
    const uint32_t target_height) -> ViewPort
  {
    ViewPort clamped = viewport;
    clamped.top_left_x
      = std::clamp(clamped.top_left_x, 0.0F, static_cast<float>(target_width));
    clamped.top_left_y
      = std::clamp(clamped.top_left_y, 0.0F, static_cast<float>(target_height));

    const auto max_width
      = static_cast<float>(target_width) - clamped.top_left_x;
    const auto max_height
      = static_cast<float>(target_height) - clamped.top_left_y;

    clamped.width = std::clamp(clamped.width, 0.0F, max_width);
    clamped.height = std::clamp(clamped.height, 0.0F, max_height);

    return clamped;
  }

} // namespace

CompositingPass::CompositingPass(std::shared_ptr<Config> config)
  : GraphicsRenderPass(config ? config->debug_name : "CompositingPass", false)
  , config_(std::move(config))
{
}

CompositingPass::~CompositingPass() { ReleasePassConstantsBuffer(); }

auto CompositingPass::ValidateConfig() -> void
{
  has_drawable_region_ = false;
  clamped_viewport_ = {};

  if (!config_) {
    throw std::runtime_error("CompositingPass: missing configuration");
  }
  if (!config_->source_texture) {
    throw std::runtime_error("CompositingPass: source texture is required");
  }
  if (!config_->viewport.IsValid()) {
    throw std::runtime_error("CompositingPass: viewport is invalid");
  }

  const auto& output = GetOutputTexture();
  if (&GetSourceTexture() == &output) {
    throw std::runtime_error(
      "CompositingPass: source texture and output texture must be distinct");
  }

  const auto sanitized_alpha
    = SanitizeAlphaForConfig(config_->debug_name, config_->alpha);
  if (sanitized_alpha.log_invalid_alpha) {
    LOG_F(WARNING, "invalid alpha={}, resetting to default {}", config_->alpha,
      sanitized_alpha.alpha);
  } else if (sanitized_alpha.log_clamped_alpha) {
    LOG_F(WARNING, "clamping alpha={} to {}", config_->alpha,
      sanitized_alpha.alpha);
  }
  config_->alpha = sanitized_alpha.alpha;

  clamped_viewport_ = GetClampedViewport();
  has_drawable_region_ = HasPositiveArea(clamped_viewport_);
}

auto CompositingPass::DoPrepareResources(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  const auto& source = GetSourceTexture();
  const auto& output = GetOutputTexture();

  if (!has_drawable_region_) {
    SetPassConstantsIndex(kInvalidShaderVisibleIndex);
    co_return;
  }

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
    throw std::runtime_error("CompositingPass: invalid source SRV index");
  }
  UpdatePassConstants(source_srv);

  co_return;
}

auto CompositingPass::DoExecute(graphics::CommandRecorder& recorder) -> co::Co<>
{
  if (!has_drawable_region_) {
    co_return;
  }

  SetupViewPortAndScissors(recorder);
  SetupRenderTargets(recorder);
  recorder.Draw(3, 1, 0, 0);

  co_return;
}

auto CompositingPass::SetupRenderTargets(
  graphics::CommandRecorder& recorder) const -> void
{
  const auto* fb = GetFramebuffer();
  CHECK_NOTNULL_F(fb);
  const auto rtvs = fb->GetRenderTargetViews();
  CHECK_F(!rtvs.empty() && rtvs.front()->IsValid(),
    "framebuffer missing a valid color RTV");
  std::array<graphics::NativeView, 1> color_rtvs { rtvs.front() };
  recorder.SetRenderTargets(std::span(color_rtvs), std::nullopt);
}

auto CompositingPass::SetupViewPortAndScissors(
  graphics::CommandRecorder& recorder) const -> void
{
  const ViewPort viewport {
    .top_left_x = clamped_viewport_.top_left_x,
    .top_left_y = clamped_viewport_.top_left_y,
    .width = clamped_viewport_.width,
    .height = clamped_viewport_.height,
    .min_depth = clamped_viewport_.min_depth,
    .max_depth = clamped_viewport_.max_depth,
  };
  recorder.SetViewport(viewport);

  const Scissors scissors {
    .left = static_cast<int32_t>(clamped_viewport_.top_left_x),
    .top = static_cast<int32_t>(clamped_viewport_.top_left_y),
    .right = static_cast<int32_t>(
      clamped_viewport_.top_left_x + clamped_viewport_.width),
    .bottom = static_cast<int32_t>(
      clamped_viewport_.top_left_y + clamped_viewport_.height),
  };
  recorder.SetScissors(scissors);
}

auto CompositingPass::GetFramebuffer() const -> const graphics::Framebuffer*
{
  return Context().pass_target.get();
}

auto CompositingPass::GetOutputAttachment() const
  -> const graphics::FramebufferAttachment&
{
  const auto* fb = GetFramebuffer();
  if (fb == nullptr) {
    throw std::runtime_error("CompositingPass: framebuffer is null");
  }
  const auto& fb_desc = fb->GetDescriptor();
  if (fb_desc.color_attachments.empty()
    || !fb_desc.color_attachments.front().texture) {
    throw std::runtime_error("CompositingPass: missing color attachment");
  }
  return fb_desc.color_attachments.front();
}

auto CompositingPass::GetOutputTexture() const -> const graphics::Texture&
{
  return *GetOutputAttachment().texture;
}

auto CompositingPass::GetSourceTexture() const -> const graphics::Texture&
{
  CHECK_NOTNULL_F(config_);
  CHECK_F(static_cast<bool>(config_->source_texture),
    "CompositingPass requires a source texture");
  return *config_->source_texture;
}

auto CompositingPass::GetClampedViewport() const -> ViewPort
{
  const auto& attachment = GetOutputAttachment();
  const auto [width, height] = ResolveAttachmentExtent(attachment);
  return ClampViewport(config_->viewport, width, height);
}

auto CompositingPass::EnsurePassConstantsBuffer() -> void
{
  auto& state = GetCurrentFramePassConstantsState();
  if (state.chunks.empty()) {
    CreatePassConstantsChunk(state);
  }
}

auto CompositingPass::CreatePassConstantsChunk(FramePassConstantsState& state)
  -> void
{
  auto& graphics = Context().GetGraphics();
  graphics_ = observer_ptr { &graphics };
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  const size_t chunk_index = state.chunks.size();
  const graphics::BufferDesc desc {
    .size_bytes = kPassConstantsStride * kPassConstantsChunkSlots,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = fmt::format("{}_PassConstants_Frame{}_Chunk{}", GetName(),
      Context().frame_slot.get(), chunk_index),
  };

  PassConstantsChunk chunk {};
  chunk.buffer = graphics.CreateBuffer(desc);
  if (!chunk.buffer) {
    throw std::runtime_error(
      "CompositingPass: Failed to create pass constants buffer");
  }
  chunk.buffer->SetName(desc.debug_name);

  chunk.mapped_ptr
    = static_cast<std::byte*>(chunk.buffer->Map(0, desc.size_bytes));
  if (chunk.mapped_ptr == nullptr) {
    throw std::runtime_error(
      "CompositingPass: Failed to map pass constants buffer");
  }

  chunk.indices.fill(kInvalidShaderVisibleIndex);
  registry.Register(chunk.buffer);
  for (uint32_t slot = 0; slot < kPassConstantsChunkSlots; ++slot) {
    const uint32_t offset = slot * kPassConstantsStride;

    graphics::BufferViewDescription cbv_view_desc;
    cbv_view_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
    cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_view_desc.range = { offset, kPassConstantsStride };

    auto cbv_handle
      = allocator.AllocateRaw(graphics::ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!cbv_handle.IsValid()) {
      throw std::runtime_error(
        "CompositingPass: Failed to allocate CBV descriptor handle");
    }
    chunk.indices.at(slot) = allocator.GetShaderVisibleIndex(cbv_handle);

    auto cbv_view = registry.RegisterView(
      *chunk.buffer, std::move(cbv_handle), cbv_view_desc);
    if (!cbv_view->IsValid()) {
      throw std::runtime_error(
        "CompositingPass: Failed to register pass constants CBV");
    }
  }

  state.chunks.push_back(std::move(chunk));
}

auto CompositingPass::GetCurrentFramePassConstantsState()
  -> FramePassConstantsState&
{
  CHECK_F(Context().frame_slot != frame::kInvalidSlot,
    "frame_slot must be valid before allocating pass constants");
  const auto slot_index = static_cast<size_t>(Context().frame_slot.get());
  CHECK_LT_F(slot_index, pass_constants_frames_.size(),
    "frame_slot {} out of bounds for pass constants",
    Context().frame_slot.get());

  auto& state = pass_constants_frames_.at(slot_index);
  if (state.frame_sequence != Context().frame_sequence) {
    ReleasePassConstantsFrameState(state);
    state.frame_sequence = Context().frame_sequence;
  }
  return state;
}

auto CompositingPass::ReleasePassConstantsBuffer() -> void
{
  for (auto& state : pass_constants_frames_) {
    ReleasePassConstantsFrameState(state);
  }
  SetPassConstantsIndex(kInvalidShaderVisibleIndex);
}

auto CompositingPass::ReleasePassConstantsFrameState(
  FramePassConstantsState& state) -> void
{
  for (auto& chunk : state.chunks) {
    if (!chunk.buffer) {
      continue;
    }
    if (chunk.buffer->IsMapped()) {
      chunk.buffer->UnMap();
    }
    if (graphics_ != nullptr) {
      auto& registry = graphics_->GetResourceRegistry();
      if (registry.Contains(*chunk.buffer)) {
        registry.UnRegisterResource(*chunk.buffer);
      }
    }
    chunk.buffer.reset();
    chunk.mapped_ptr = nullptr;
    chunk.indices.fill(kInvalidShaderVisibleIndex);
    chunk.used_slots = 0U;
  }
  state.chunks.clear();
  state.frame_sequence = frame::kInvalidSequenceNumber;
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

  if (const auto existing_index
    = registry.FindShaderVisibleIndex(texture, srv_desc);
    existing_index.has_value()) {
    return *existing_index;
  }

  CHECK_F(registry.Contains(texture),
    "source texture '{}' must be registered in ResourceRegistry before SRV "
    "creation",
    texture.GetDescriptor().debug_name);
  if (registry.Contains(texture, srv_desc)) {
    throw std::runtime_error(
      "CompositingPass: source SRV exists in registry without a "
      "shader-visible index");
  }

  auto srv_handle
    = allocator.AllocateRaw(graphics::ResourceViewType::kTexture_SRV,
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

  return srv_index;
}

auto CompositingPass::UpdatePassConstants(
  const ShaderVisibleIndex source_texture_index) -> void
{
  const float alpha = detail::SanitizeCompositingAlphaValue(config_->alpha);
  const CompositingPassConstants constants {
    .source_texture_index = source_texture_index.get(),
    .sampler_index = 0U,
    .alpha = alpha,
    .pad0 = 0.0F,
  };

  auto& state = GetCurrentFramePassConstantsState();
  if (state.chunks.empty()
    || state.chunks.back().used_slots >= kPassConstantsChunkSlots) {
    CreatePassConstantsChunk(state);
  }

  auto& chunk = state.chunks.back();
  CHECK_NOTNULL_F(chunk.mapped_ptr);
  CHECK_LT_F(chunk.used_slots, kPassConstantsChunkSlots);
  const auto slot = chunk.used_slots++;
  auto* slot_ptr = chunk.mapped_ptr
    + static_cast<std::ptrdiff_t>(slot * kPassConstantsStride);
  std::memcpy(slot_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(chunk.indices.at(slot));
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

  const auto& attachment = GetOutputAttachment();
  const auto& color_desc = attachment.texture->GetDescriptor();
  const FramebufferLayoutDesc fb_layout_desc {
    .color_target_formats = { ResolveAttachmentFormat(attachment) },
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
      .source_path = "Vortex/RendererCore/Compositing/Compositing_VS.hlsl",
      .entry_point = "VS",
      .defines = {},
    })
    .SetPixelShader(ShaderRequest {
      .stage = oxygen::ShaderType::kPixel,
      .source_path = "Vortex/RendererCore/Compositing/Compositing_PS.hlsl",
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

  const auto& attachment = GetOutputAttachment();
  const auto& color_desc = attachment.texture->GetDescriptor();
  const auto output_format = ResolveAttachmentFormat(attachment);
  if (last_built->FramebufferLayout().color_target_formats.empty()
    || last_built->FramebufferLayout().color_target_formats.front()
      != output_format) {
    return true;
  }

  if (last_built->FramebufferLayout().sample_count != color_desc.sample_count) {
    return true;
  }

  return false;
}

} // namespace oxygen::vortex::internal
