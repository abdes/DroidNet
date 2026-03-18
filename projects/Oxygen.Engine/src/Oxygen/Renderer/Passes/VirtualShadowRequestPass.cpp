//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Internal/ShadowBackendCommon.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/VirtualShadowRequestPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::engine {

namespace {

  struct alignas(packing::kShaderDataFieldAlignment)
    VirtualShadowRequestPassConstants {
    ShaderVisibleIndex depth_texture_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex virtual_directional_shadow_metadata_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex request_words_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_mark_flags_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t request_word_count { 0U };
    std::uint32_t total_page_count { 0U };
    std::uint32_t border_dilation_texels { 0U };
    std::uint32_t _pad0 { 0U };
    glm::uvec2 pixel_stride { 1U, 1U };
    std::uint32_t _pad_stride0 { 0U };
    std::uint32_t _pad_stride1 { 0U };
    glm::uvec2 screen_dimensions { 0U, 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };

    glm::mat4 inv_view_projection_matrix { 1.0F };
  };
  static_assert(sizeof(VirtualShadowRequestPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);
  static_assert(
    offsetof(VirtualShadowRequestPassConstants, depth_texture_index) == 0U);
  static_assert(
    offsetof(VirtualShadowRequestPassConstants, request_word_count) == 16U);
  static_assert(
    offsetof(VirtualShadowRequestPassConstants, pixel_stride) == 32U);
  static_assert(
    offsetof(VirtualShadowRequestPassConstants, screen_dimensions) == 48U);
  static_assert(
    offsetof(VirtualShadowRequestPassConstants, inv_view_projection_matrix)
    == 64U);

} // namespace

VirtualShadowRequestPass::VirtualShadowRequestPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config->debug_name)
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowRequestPass::~VirtualShadowRequestPass()
{
  if (clear_upload_buffer_ && clear_upload_mapped_ptr_ != nullptr) {
    clear_upload_buffer_->UnMap();
    clear_upload_mapped_ptr_ = nullptr;
  }
  if (page_mark_flags_clear_upload_buffer_
    && page_mark_flags_clear_upload_mapped_ptr_ != nullptr) {
    page_mark_flags_clear_upload_buffer_->UnMap();
    page_mark_flags_clear_upload_mapped_ptr_ = nullptr;
  }
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
}

auto VirtualShadowRequestPass::DoPrepareResources(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  active_dispatch_ = false;
  active_request_word_count_ = 0U;
  active_pixel_stride_ = std::max(1U, config_->pixel_stride);
  active_border_dilation_texels_ = config_->border_dilation_texels;
  active_view_id_ = {};

  if (Context().frame_slot == frame::kInvalidSlot) {
    co_return;
  }

  const auto* prepared_frame = Context().current_view.prepared_frame.get();
  if (prepared_frame == nullptr || !prepared_frame->IsValid()
    || prepared_frame->draw_metadata_bytes.empty()
    || prepared_frame->partitions.empty()) {
    co_return;
  }

  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (depth_pass == nullptr || shadow_manager == nullptr
    || Context().current_view.resolved_view == nullptr) {
    co_return;
  }

  const auto* metadata = shadow_manager->TryGetVirtualDirectionalMetadata(
    Context().current_view.view_id);
  const auto* publication
    = shadow_manager->TryGetFramePublication(Context().current_view.view_id);
  if (metadata == nullptr) {
    co_return;
  }

  if (metadata->clip_level_count == 0U || metadata->pages_per_axis == 0U) {
    co_return;
  }
  if (publication == nullptr
    || !publication->virtual_directional_shadow_metadata_srv.IsValid()) {
    LOG_F(ERROR,
      "VirtualShadowRequestPass: missing current virtual directional metadata "
      "publication for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto total_pages = metadata->clip_level_count * metadata->pages_per_axis
    * metadata->pages_per_axis;
  const auto request_word_count = (std::max(1U, total_pages) + 31U) / 32U;
  if (request_word_count > kMaxRequestWordCount) {
    LOG_F(WARNING,
      "VirtualShadowRequestPass: skipping view {} because request buffer "
      "capacity {} words is smaller than required {} words",
      Context().current_view.view_id.get(), kMaxRequestWordCount,
      request_word_count);
    co_return;
  }

  const auto& depth_texture = depth_pass->GetDepthTexture();
  EnsureRequestBuffers();
  EnsurePassConstantsBuffer();

  const auto depth_texture_srv = EnsureDepthTextureSrv(depth_texture);
  if (depth_texture_srv == kInvalidShaderVisibleIndex) {
    LOG_F(ERROR,
      "VirtualShadowRequestPass: failed to prepare depth SRV for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const VirtualShadowRequestPassConstants pass_constants {
    .depth_texture_index = depth_texture_srv,
    .virtual_directional_shadow_metadata_index
    = publication->virtual_directional_shadow_metadata_srv,
    .request_words_uav_index = request_words_uav_,
    .page_mark_flags_uav_index = page_mark_flags_uav_,
    .request_word_count = request_word_count,
    .total_page_count = total_pages,
    .border_dilation_texels = active_border_dilation_texels_,
    .pixel_stride = glm::uvec2(active_pixel_stride_, active_pixel_stride_),
    .screen_dimensions = glm::uvec2(depth_texture.GetDescriptor().width,
      depth_texture.GetDescriptor().height),
    .inv_view_projection_matrix
    = Context().current_view.resolved_view->InverseViewProjection(),
  };
  std::memcpy(
    pass_constants_mapped_ptr_, &pass_constants, sizeof(pass_constants));
  SetPassConstantsIndex(pass_constants_index_);

  if (!recorder.IsResourceTracked(*clear_upload_buffer_)) {
    recorder.BeginTrackingResourceState(
      *clear_upload_buffer_, graphics::ResourceStates::kCopySource, false);
  }
  if (!recorder.IsResourceTracked(*request_words_buffer_)) {
    recorder.BeginTrackingResourceState(
      *request_words_buffer_, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*page_mark_flags_buffer_)) {
    recorder.BeginTrackingResourceState(
      *page_mark_flags_buffer_, graphics::ResourceStates::kCommon, true);
  }

  recorder.RequireResourceState(
    depth_texture, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *request_words_buffer_, graphics::ResourceStates::kCopyDest);
  recorder.RequireResourceState(
    *page_mark_flags_buffer_, graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*request_words_buffer_, 0U, *clear_upload_buffer_, 0U,
    static_cast<std::size_t>(kMaxRequestWordCount * sizeof(std::uint32_t)));
  recorder.CopyBuffer(*page_mark_flags_buffer_, 0U,
    *page_mark_flags_clear_upload_buffer_, 0U,
    static_cast<std::size_t>(kMaxSupportedPageCount * sizeof(std::uint32_t)));

  recorder.RequireResourceState(
    *request_words_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *page_mark_flags_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  active_dispatch_ = true;
  active_view_id_ = Context().current_view.view_id;
  active_request_word_count_ = request_word_count;

  co_return;
}

auto VirtualShadowRequestPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  if (!active_dispatch_) {
    co_return;
  }

  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  if (depth_pass == nullptr) {
    co_return;
  }

  const auto& depth_texture = depth_pass->GetDepthTexture();
  const auto width = std::max(1U, depth_texture.GetDescriptor().width);
  const auto height = std::max(1U, depth_texture.GetDescriptor().height);
  const auto pixel_stride = std::max(1U, active_pixel_stride_);
  const auto group_count_x
    = (width + pixel_stride * kDispatchGroupSize - 1U)
    / (pixel_stride * kDispatchGroupSize);
  const auto group_count_y
    = (height + pixel_stride * kDispatchGroupSize - 1U)
    / (pixel_stride * kDispatchGroupSize);
  recorder.Dispatch(group_count_x, group_count_y, 1U);

  co_return;
}

auto VirtualShadowRequestPass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowRequestPass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowRequestPass: config is null");
  }
}

auto VirtualShadowRequestPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  graphics::ShaderRequest shader_request {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Lighting/VirtualShadowRequest.hlsl",
    .entry_point = "CS",
  };

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader(std::move(shader_request))
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowRequest_PSO")
    .Build();
}

auto VirtualShadowRequestPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

auto VirtualShadowRequestPass::EnsureRequestBuffers() -> void
{
  if (request_words_buffer_ && clear_upload_buffer_ && page_mark_flags_buffer_
    && page_mark_flags_clear_upload_buffer_
    && clear_upload_mapped_ptr_ != nullptr && request_words_uav_.IsValid()
    && page_mark_flags_clear_upload_mapped_ptr_ != nullptr
    && request_words_srv_.IsValid() && page_mark_flags_uav_.IsValid()
    && page_mark_flags_srv_.IsValid()) {
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();

  constexpr std::uint64_t kBufferSize
    = kMaxRequestWordCount * sizeof(std::uint32_t);
  constexpr std::uint64_t kPageMarkFlagsSize
    = kMaxSupportedPageCount * sizeof(std::uint32_t);

  if (!request_words_buffer_) {
    const graphics::BufferDesc desc {
      .size_bytes = kBufferSize,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowRequestPass.RequestWords",
    };
    request_words_buffer_ = gfx_->CreateBuffer(desc);
    if (!request_words_buffer_) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to create request buffer");
    }
    registry.Register(request_words_buffer_);

    auto uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to allocate request UAV");
    }
    request_words_uav_ = allocator.GetShaderVisibleIndex(uav_handle);

    graphics::BufferViewDescription uav_desc;
    uav_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_UAV;
    uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    uav_desc.range = { 0U, kBufferSize };
    uav_desc.stride = sizeof(std::uint32_t);
    registry.RegisterView(
      *request_words_buffer_, std::move(uav_handle), uav_desc);

    auto srv_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!srv_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to allocate request SRV");
    }
    request_words_srv_ = allocator.GetShaderVisibleIndex(srv_handle);

    graphics::BufferViewDescription srv_desc;
    srv_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
    srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    srv_desc.range = { 0U, kBufferSize };
    srv_desc.stride = sizeof(std::uint32_t);
    registry.RegisterView(
      *request_words_buffer_, std::move(srv_handle), srv_desc);
  }

  if (!clear_upload_buffer_) {
    const graphics::BufferDesc desc {
      .size_bytes = kBufferSize,
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = "VirtualShadowRequestPass.ClearUpload",
    };
    clear_upload_buffer_ = gfx_->CreateBuffer(desc);
    if (!clear_upload_buffer_) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to create clear upload buffer");
    }
    clear_upload_mapped_ptr_ = clear_upload_buffer_->Map(0U, desc.size_bytes);
    if (clear_upload_mapped_ptr_ == nullptr) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to map clear upload buffer");
    }
    std::memset(
      clear_upload_mapped_ptr_, 0, static_cast<std::size_t>(desc.size_bytes));
  }

  if (!page_mark_flags_buffer_) {
    const graphics::BufferDesc desc {
      .size_bytes = kPageMarkFlagsSize,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowRequestPass.PageMarkFlags",
    };
    page_mark_flags_buffer_ = gfx_->CreateBuffer(desc);
    if (!page_mark_flags_buffer_) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to create page-mark flags buffer");
    }
    registry.Register(page_mark_flags_buffer_);

    auto uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to allocate page-mark flags UAV");
    }
    page_mark_flags_uav_ = allocator.GetShaderVisibleIndex(uav_handle);

    graphics::BufferViewDescription uav_desc;
    uav_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_UAV;
    uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    uav_desc.range = { 0U, kPageMarkFlagsSize };
    uav_desc.stride = sizeof(std::uint32_t);
    registry.RegisterView(
      *page_mark_flags_buffer_, std::move(uav_handle), uav_desc);

    auto srv_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!srv_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to allocate page-mark flags SRV");
    }
    page_mark_flags_srv_ = allocator.GetShaderVisibleIndex(srv_handle);

    graphics::BufferViewDescription srv_desc;
    srv_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
    srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    srv_desc.range = { 0U, kPageMarkFlagsSize };
    srv_desc.stride = sizeof(std::uint32_t);
    registry.RegisterView(
      *page_mark_flags_buffer_, std::move(srv_handle), srv_desc);
  }

  if (!page_mark_flags_clear_upload_buffer_) {
    const graphics::BufferDesc desc {
      .size_bytes = kPageMarkFlagsSize,
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = "VirtualShadowRequestPass.PageMarkFlagsClearUpload",
    };
    page_mark_flags_clear_upload_buffer_ = gfx_->CreateBuffer(desc);
    if (!page_mark_flags_clear_upload_buffer_) {
      throw std::runtime_error("VirtualShadowRequestPass: failed to create "
                               "page-mark flags clear upload buffer");
    }
    page_mark_flags_clear_upload_mapped_ptr_
      = page_mark_flags_clear_upload_buffer_->Map(0U, desc.size_bytes);
    if (page_mark_flags_clear_upload_mapped_ptr_ == nullptr) {
      throw std::runtime_error("VirtualShadowRequestPass: failed to map "
                               "page-mark flags clear upload buffer");
    }
    std::memset(page_mark_flags_clear_upload_mapped_ptr_, 0,
      static_cast<std::size_t>(desc.size_bytes));
  }
}

auto VirtualShadowRequestPass::EnsurePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ && pass_constants_index_.IsValid()
    && pass_constants_mapped_ptr_ != nullptr) {
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();

  const graphics::BufferDesc desc {
    .size_bytes = 256U,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowRequestPass.Constants",
  };
  pass_constants_buffer_ = gfx_->CreateBuffer(desc);
  if (!pass_constants_buffer_) {
    throw std::runtime_error(
      "VirtualShadowRequestPass: failed to create pass constants buffer");
  }
  registry.Register(pass_constants_buffer_);

  pass_constants_mapped_ptr_ = pass_constants_buffer_->Map(0U, desc.size_bytes);
  if (pass_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "VirtualShadowRequestPass: failed to map pass constants buffer");
  }

  graphics::BufferViewDescription cbv_desc;
  cbv_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
  cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  cbv_desc.range = { 0U, desc.size_bytes };

  auto cbv_handle
    = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!cbv_handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowRequestPass: failed to allocate constants CBV");
  }
  pass_constants_index_ = allocator.GetShaderVisibleIndex(cbv_handle);
  pass_constants_cbv_ = registry.RegisterView(
    *pass_constants_buffer_, std::move(cbv_handle), cbv_desc);
}

auto VirtualShadowRequestPass::EnsureDepthTextureSrv(
  const graphics::Texture& depth_tex) -> ShaderVisibleIndex
{
  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();

  const auto depth_format = depth_tex.GetDescriptor().format;
  Format srv_format = Format::kR32Float;
  switch (depth_format) {
  case Format::kDepth32:
  case Format::kDepth32Stencil8:
  case Format::kDepth24Stencil8:
    srv_format = Format::kR32Float;
    break;
  case Format::kDepth16:
    srv_format = Format::kR16UNorm;
    break;
  default:
    srv_format = depth_format;
    break;
  }

  graphics::TextureViewDescription srv_desc {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = srv_format,
    .dimension = depth_tex.GetDescriptor().texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .is_read_only_dsv = false,
  };

  if (const auto existing_index
    = registry.FindShaderVisibleIndex(depth_tex, srv_desc);
    existing_index.has_value()) {
    depth_texture_srv_ = *existing_index;
    depth_texture_owner_ = &depth_tex;
    owns_depth_texture_srv_ = false;
    return depth_texture_srv_;
  }

  if (depth_texture_srv_.IsValid() && depth_texture_owner_ == &depth_tex
    && owns_depth_texture_srv_ && registry.Contains(depth_tex, srv_desc)) {
    return depth_texture_srv_;
  }

  auto register_new_srv = [&]() -> ShaderVisibleIndex {
    auto srv_handle
      = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!srv_handle.IsValid()) {
      return kInvalidShaderVisibleIndex;
    }
    depth_texture_srv_ = allocator.GetShaderVisibleIndex(srv_handle);
    auto native_view
      = registry.RegisterView(const_cast<graphics::Texture&>(depth_tex),
        std::move(srv_handle), srv_desc);
    if (!native_view->IsValid()) {
      depth_texture_srv_ = kInvalidShaderVisibleIndex;
      owns_depth_texture_srv_ = false;
      return kInvalidShaderVisibleIndex;
    }
    depth_texture_owner_ = &depth_tex;
    owns_depth_texture_srv_ = true;
    return depth_texture_srv_;
  };

  if (!depth_texture_srv_.IsValid() || !owns_depth_texture_srv_) {
    return register_new_srv();
  }

  const auto updated
    = registry.UpdateView(const_cast<graphics::Texture&>(depth_tex),
      bindless::HeapIndex { depth_texture_srv_.get() }, srv_desc);
  if (!updated) {
    depth_texture_srv_ = kInvalidShaderVisibleIndex;
    depth_texture_owner_ = nullptr;
    owns_depth_texture_srv_ = false;
    return register_new_srv();
  }
  depth_texture_owner_ = &depth_tex;
  owns_depth_texture_srv_ = true;
  return depth_texture_srv_;
}

} // namespace oxygen::engine
