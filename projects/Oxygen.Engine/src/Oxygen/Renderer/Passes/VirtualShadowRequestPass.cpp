//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include <glm/integer.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
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
#include <Oxygen/Renderer/Types/VirtualShadowPageFlags.h>
#include <Oxygen/Renderer/Types/VirtualShadowRequestFeedback.h>

namespace oxygen::engine {

using namespace oxygen::renderer::internal::shadow_detail;

namespace {

  using SteadyClock = std::chrono::steady_clock;

  auto ElapsedMicroseconds(const SteadyClock::time_point start)
    -> std::uint64_t
  {
    return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
        SteadyClock::now() - start)
        .count());
  }

  struct alignas(packing::kShaderDataFieldAlignment)
    VirtualShadowRequestPassConstants {
    ShaderVisibleIndex depth_texture_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex virtual_directional_shadow_metadata_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex request_words_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_mark_flags_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex stats_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t request_word_count { 0U };
    std::uint32_t total_page_count { 0U };
    std::uint32_t _pad0 { 0U };
    glm::uvec2 screen_dimensions { 0U, 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };

    glm::mat4 inv_view_projection_matrix { 1.0F };
  };
  static_assert(sizeof(VirtualShadowRequestPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);
  static_assert(offsetof(VirtualShadowRequestPassConstants, depth_texture_index)
      == 0U);
  static_assert(offsetof(
                    VirtualShadowRequestPassConstants,
                    stats_uav_index)
      == 16U);
  static_assert(offsetof(
                    VirtualShadowRequestPassConstants,
                    screen_dimensions)
      == 32U);
  static_assert(offsetof(
                    VirtualShadowRequestPassConstants,
                    inv_view_projection_matrix)
      == 48U);

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
  if (stats_clear_upload_buffer_ && stats_clear_upload_mapped_ptr_ != nullptr) {
    stats_clear_upload_buffer_->UnMap();
    stats_clear_upload_mapped_ptr_ = nullptr;
  }
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
  for (auto& slot : slot_readbacks_) {
    if (slot.buffer && slot.mapped_words != nullptr) {
      slot.buffer->UnMap();
      slot.mapped_words = nullptr;
      slot.mapped_page_mark_flags = nullptr;
      slot.mapped_stats = nullptr;
    }
    slot.buffer.reset();
  }
}

auto VirtualShadowRequestPass::DoPrepareResources(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  const auto prepare_begin = SteadyClock::now();
  active_dispatch_ = false;
  active_request_word_count_ = 0U;
  active_pages_per_axis_ = 0U;
  active_clip_level_count_ = 0U;
  active_directional_address_space_hash = 0U;
  active_clip_grid_origin_x_.fill(0);
  active_clip_grid_origin_y_.fill(0);
  active_view_id_ = {};

  if (Context().frame_slot == frame::kInvalidSlot) {
    co_return;
  }

  ProcessCompletedFeedback(Context().frame_slot);

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
  const auto* publication = shadow_manager->TryGetFramePublication(
    Context().current_view.view_id);
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
  EnsureReadbackBuffer(Context().frame_slot);

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
    .stats_uav_index = stats_uav_,
    .request_word_count = request_word_count,
    .total_page_count = total_pages,
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
  if (!recorder.IsResourceTracked(*stats_buffer_)) {
    recorder.BeginTrackingResourceState(
      *stats_buffer_, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*stats_clear_upload_buffer_)) {
    recorder.BeginTrackingResourceState(
      *stats_clear_upload_buffer_, graphics::ResourceStates::kCopySource, false);
  }

  auto& readback = slot_readbacks_[Context().frame_slot.get()];
  if (!recorder.IsResourceTracked(*readback.buffer)) {
    recorder.BeginTrackingResourceState(
      *readback.buffer, graphics::ResourceStates::kCopyDest, false);
  }

  recorder.RequireResourceState(
    depth_texture, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *request_words_buffer_, graphics::ResourceStates::kCopyDest);
  recorder.RequireResourceState(
    *page_mark_flags_buffer_, graphics::ResourceStates::kCopyDest);
  recorder.RequireResourceState(
    *stats_buffer_, graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*request_words_buffer_, 0U, *clear_upload_buffer_, 0U,
    static_cast<std::size_t>(kMaxRequestWordCount * sizeof(std::uint32_t)));
  recorder.CopyBuffer(*page_mark_flags_buffer_, 0U,
    *page_mark_flags_clear_upload_buffer_, 0U,
    static_cast<std::size_t>(kMaxSupportedPageCount * sizeof(std::uint32_t)));
  recorder.CopyBuffer(*stats_buffer_, 0U, *stats_clear_upload_buffer_, 0U,
    static_cast<std::size_t>(kStatsWordCount * sizeof(std::uint32_t)));

  recorder.RequireResourceState(
    *request_words_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *page_mark_flags_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *stats_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  active_dispatch_ = true;
  active_view_id_ = Context().current_view.view_id;
  active_request_word_count_ = request_word_count;
  active_pages_per_axis_ = metadata->pages_per_axis;
  active_clip_level_count_ = metadata->clip_level_count;
  active_directional_address_space_hash
    = HashDirectionalVirtualFeedbackAddressSpace(*metadata);
  const auto active_clip_count
    = std::min(metadata->clip_level_count, kMaxSupportedClipLevels);
  for (std::uint32_t clip_index = 0U; clip_index < active_clip_count;
    ++clip_index) {
    active_clip_grid_origin_x_[clip_index]
      = ResolveDirectionalVirtualClipGridOriginX(*metadata, clip_index);
    active_clip_grid_origin_y_[clip_index]
      = ResolveDirectionalVirtualClipGridOriginY(*metadata, clip_index);
  }

  LOG_F(INFO,
    "VirtualShadowRequestPass: frame={} slot={} view={} prepared dispatch "
    "(words={} pages_per_axis={} clips={} address_hash=0x{:x} depth={}x{} "
    "cpu_prepare_us={})",
    Context().frame_sequence.get(), Context().frame_slot.get(),
    Context().current_view.view_id.get(), active_request_word_count_,
    active_pages_per_axis_, active_clip_level_count_,
    active_directional_address_space_hash, pass_constants.screen_dimensions.x,
    pass_constants.screen_dimensions.y, ElapsedMicroseconds(prepare_begin));

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
  const auto group_count_x
    = (width + kDispatchGroupSize - 1U) / kDispatchGroupSize;
  const auto group_count_y
    = (height + kDispatchGroupSize - 1U) / kDispatchGroupSize;
  recorder.Dispatch(group_count_x, group_count_y, 1U);

  auto& readback = slot_readbacks_[Context().frame_slot.get()];
  recorder.RequireResourceState(
    *request_words_buffer_, graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *page_mark_flags_buffer_, graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *stats_buffer_, graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *readback.buffer, graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*readback.buffer, 0U, *request_words_buffer_, 0U,
    static_cast<std::size_t>(kMaxRequestWordCount * sizeof(std::uint32_t)));
  recorder.CopyBuffer(*readback.buffer,
    static_cast<std::size_t>(kMaxRequestWordCount * sizeof(std::uint32_t)),
    *page_mark_flags_buffer_, 0U,
    static_cast<std::size_t>(kMaxSupportedPageCount * sizeof(std::uint32_t)));
  recorder.CopyBuffer(*readback.buffer,
    static_cast<std::size_t>(
      (kMaxRequestWordCount + kMaxSupportedPageCount)
      * sizeof(std::uint32_t)),
    *stats_buffer_, 0U,
    static_cast<std::size_t>(kStatsWordCount * sizeof(std::uint32_t)));

  readback.pending_feedback = true;
  readback.view_id = active_view_id_;
  readback.source_frame_sequence = Context().frame_sequence;
  readback.pages_per_axis = active_pages_per_axis_;
  readback.clip_level_count = active_clip_level_count_;
  readback.directional_address_space_hash
    = active_directional_address_space_hash;
  readback.clip_grid_origin_x = active_clip_grid_origin_x_;
  readback.clip_grid_origin_y = active_clip_grid_origin_y_;
  readback.request_word_count = active_request_word_count_;
  readback.total_page_count
    = active_pages_per_axis_ * active_pages_per_axis_ * active_clip_level_count_;

  LOG_F(INFO,
    "VirtualShadowRequestPass: frame={} slot={} view={} dispatched request "
    "pass (groups={}x{} words={})",
    Context().frame_sequence.get(), Context().frame_slot.get(),
    active_view_id_.get(), group_count_x, group_count_y,
    active_request_word_count_);

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
  if (request_words_buffer_ && clear_upload_buffer_
    && page_mark_flags_buffer_ && page_mark_flags_clear_upload_buffer_
    && stats_buffer_ && stats_clear_upload_buffer_
    && clear_upload_mapped_ptr_ != nullptr && request_words_uav_.IsValid()
    && page_mark_flags_clear_upload_mapped_ptr_ != nullptr
    && stats_clear_upload_mapped_ptr_ != nullptr
    && request_words_srv_.IsValid() && page_mark_flags_uav_.IsValid()
    && page_mark_flags_srv_.IsValid() && stats_uav_.IsValid()) {
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();

  constexpr std::uint64_t kBufferSize
    = kMaxRequestWordCount * sizeof(std::uint32_t);
  constexpr std::uint64_t kPageMarkFlagsSize
    = kMaxSupportedPageCount * sizeof(std::uint32_t);
  constexpr std::uint64_t kStatsBufferSize
    = kStatsWordCount * sizeof(std::uint32_t);

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
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to create page-mark flags clear upload buffer");
    }
    page_mark_flags_clear_upload_mapped_ptr_
      = page_mark_flags_clear_upload_buffer_->Map(0U, desc.size_bytes);
    if (page_mark_flags_clear_upload_mapped_ptr_ == nullptr) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to map page-mark flags clear upload buffer");
    }
    std::memset(page_mark_flags_clear_upload_mapped_ptr_, 0,
      static_cast<std::size_t>(desc.size_bytes));
  }

  if (!stats_buffer_) {
    const graphics::BufferDesc desc {
      .size_bytes = kStatsBufferSize,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowRequestPass.Stats",
    };
    stats_buffer_ = gfx_->CreateBuffer(desc);
    if (!stats_buffer_) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to create stats buffer");
    }
    registry.Register(stats_buffer_);

    auto uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to allocate stats UAV");
    }
    stats_uav_ = allocator.GetShaderVisibleIndex(uav_handle);

    graphics::BufferViewDescription uav_desc;
    uav_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_UAV;
    uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    uav_desc.range = { 0U, kStatsBufferSize };
    uav_desc.stride = sizeof(std::uint32_t);
    registry.RegisterView(*stats_buffer_, std::move(uav_handle), uav_desc);
  }

  if (!stats_clear_upload_buffer_) {
    const graphics::BufferDesc desc {
      .size_bytes = kStatsBufferSize,
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = "VirtualShadowRequestPass.StatsClearUpload",
    };
    stats_clear_upload_buffer_ = gfx_->CreateBuffer(desc);
    if (!stats_clear_upload_buffer_) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to create stats clear upload buffer");
    }
    stats_clear_upload_mapped_ptr_ = stats_clear_upload_buffer_->Map(0U, desc.size_bytes);
    if (stats_clear_upload_mapped_ptr_ == nullptr) {
      throw std::runtime_error(
        "VirtualShadowRequestPass: failed to map stats clear upload buffer");
    }
    std::memset(stats_clear_upload_mapped_ptr_, 0,
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

auto VirtualShadowRequestPass::EnsureReadbackBuffer(const frame::Slot slot)
  -> void
{
  auto& readback = slot_readbacks_[slot.get()];
  if (readback.buffer && readback.mapped_words != nullptr) {
    return;
  }

  constexpr std::uint64_t kReadbackWordCount
    = static_cast<std::uint64_t>(kMaxRequestWordCount)
    + static_cast<std::uint64_t>(kMaxSupportedPageCount)
    + static_cast<std::uint64_t>(kStatsWordCount);

  const graphics::BufferDesc desc {
    .size_bytes = kReadbackWordCount * sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kReadBack,
    .debug_name = "VirtualShadowRequestPass.Readback",
  };
  readback.buffer = gfx_->CreateBuffer(desc);
  if (!readback.buffer) {
    throw std::runtime_error(
      "VirtualShadowRequestPass: failed to create readback buffer");
  }
  readback.mapped_words
    = static_cast<std::uint32_t*>(readback.buffer->Map(0U, desc.size_bytes));
  if (readback.mapped_words == nullptr) {
    throw std::runtime_error(
      "VirtualShadowRequestPass: failed to map readback buffer");
  }
  readback.mapped_page_mark_flags = readback.mapped_words + kMaxRequestWordCount;
  readback.mapped_stats
    = readback.mapped_page_mark_flags + kMaxSupportedPageCount;
  std::memset(
    readback.mapped_words, 0, static_cast<std::size_t>(desc.size_bytes));
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

auto VirtualShadowRequestPass::ProcessCompletedFeedback(const frame::Slot slot)
  -> void
{
  auto& readback = slot_readbacks_[slot.get()];
  if (!readback.pending_feedback || readback.mapped_words == nullptr) {
    return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    readback.pending_feedback = false;
    return;
  }

  const auto total_pages = readback.pages_per_axis * readback.pages_per_axis
    * readback.clip_level_count;
  renderer::VirtualShadowRequestFeedback feedback {};
  feedback.source_frame_sequence = readback.source_frame_sequence;
  feedback.pages_per_axis = readback.pages_per_axis;
  feedback.clip_level_count = readback.clip_level_count;
  feedback.directional_address_space_hash
    = readback.directional_address_space_hash;
  feedback.kind = renderer::VirtualShadowFeedbackKind::kDetail;
  const auto pages_per_level
    = readback.pages_per_axis * readback.pages_per_axis;

  const auto max_words = std::min<std::uint32_t>(readback.request_word_count,
    static_cast<std::uint32_t>(kMaxRequestWordCount));
  for (std::uint32_t word_index = 0U; word_index < max_words; ++word_index) {
    auto word = readback.mapped_words[word_index];
    while (word != 0U) {
      const auto bit_index = static_cast<std::uint32_t>(std::countr_zero(word));
      const auto page_index = word_index * 32U + bit_index;
      if (page_index < total_pages) {
        const auto clip_index = page_index / pages_per_level;
        const auto local_page_index = page_index % pages_per_level;
        const auto page_y = local_page_index / readback.pages_per_axis;
        const auto page_x = local_page_index % readback.pages_per_axis;
        const auto resident_key = PackVirtualResidentPageKey(clip_index,
          readback.clip_grid_origin_x[clip_index]
            + static_cast<std::int32_t>(page_x),
          readback.clip_grid_origin_y[clip_index]
            + static_cast<std::int32_t>(page_y));
        feedback.requested_resident_keys.push_back(resident_key);
      }
      word &= (word - 1U);
    }
  }

  std::uint32_t marked_page_count = 0U;
  std::uint32_t used_marked_page_count = 0U;
  std::uint32_t detail_marked_page_count = 0U;
  std::uint32_t used_detail_marked_page_count = 0U;
  std::array<std::uint32_t, kMaxSupportedClipLevels> requested_pages_per_clip {};
  std::array<std::uint32_t, kMaxSupportedClipLevels> marked_pages_per_clip {};
  std::array<std::uint32_t, kMaxSupportedClipLevels> used_marked_pages_per_clip {};
  std::array<std::uint32_t, kMaxSupportedClipLevels> detail_marked_pages_per_clip {};
  std::array<std::uint32_t, kMaxSupportedClipLevels>
    used_detail_marked_pages_per_clip {};
  std::ostringstream first_requested_pages;
  std::uint32_t logged_requested_pages = 0U;
  for (const auto resident_key : feedback.requested_resident_keys) {
    const auto clip_index
      = renderer::internal::shadow_detail::VirtualResidentPageKeyClipLevel(
        resident_key);
    if (clip_index < requested_pages_per_clip.size()) {
      ++requested_pages_per_clip[clip_index];
    }
    if (logged_requested_pages < 8U) {
      if (logged_requested_pages > 0U) {
        first_requested_pages << ",";
      }
      first_requested_pages << clip_index << ":("
                           << renderer::internal::shadow_detail::
                                VirtualResidentPageKeyGridX(
                                resident_key)
                           << ","
                           << renderer::internal::shadow_detail::
                                VirtualResidentPageKeyGridY(
                                resident_key)
                           << ")";
      ++logged_requested_pages;
    }
  }
  const auto max_page_count
    = std::min<std::uint32_t>(readback.total_page_count, kMaxSupportedPageCount);
  for (std::uint32_t page_index = 0U; page_index < max_page_count; ++page_index) {
    const auto mark_flags = readback.mapped_page_mark_flags[page_index];
    if (mark_flags == 0U) {
      continue;
    }
    ++marked_page_count;
    const auto clip_index
      = pages_per_level > 0U ? page_index / pages_per_level : 0U;
    if (clip_index < marked_pages_per_clip.size()) {
      ++marked_pages_per_clip[clip_index];
    }
    const bool used_this_frame
      = (mark_flags
          & renderer::ToMask(renderer::VirtualShadowPageFlag::kUsedThisFrame))
      != 0U;
    const bool detail_geometry
      = (mark_flags
          & renderer::ToMask(renderer::VirtualShadowPageFlag::kDetailGeometry))
      != 0U;
    if (used_this_frame) {
      ++used_marked_page_count;
      if (clip_index < used_marked_pages_per_clip.size()) {
        ++used_marked_pages_per_clip[clip_index];
      }
    }
    if (detail_geometry) {
      ++detail_marked_page_count;
      if (clip_index < detail_marked_pages_per_clip.size()) {
        ++detail_marked_pages_per_clip[clip_index];
      }
    }
    if (used_this_frame && detail_geometry) {
      ++used_detail_marked_page_count;
      if (clip_index < used_detail_marked_pages_per_clip.size()) {
        ++used_detail_marked_pages_per_clip[clip_index];
      }
    }
  }

  const auto format_histogram =
    [](const auto& counts) {
      std::ostringstream stream;
      bool first = true;
      for (std::size_t clip_index = 0U; clip_index < counts.size();
        ++clip_index) {
        if (counts[clip_index] == 0U) {
          continue;
        }
        if (!first) {
          stream << ",";
        }
        first = false;
        stream << clip_index << ":" << counts[clip_index];
      }
      return stream.str();
    };

  std::array<std::uint32_t, kMaxSupportedClipLevels> selected_pixels_per_clip {};
  std::array<std::uint32_t, kMaxSupportedClipLevels> projected_pixels_per_clip {};
  std::uint32_t geometry_pixels = 0U;
  std::uint32_t clip_select_success = 0U;
  std::uint32_t clip_select_fail = 0U;
  std::uint32_t request_projection_success = 0U;
  if (readback.mapped_stats != nullptr) {
    geometry_pixels = readback.mapped_stats[0];
    clip_select_success = readback.mapped_stats[1];
    clip_select_fail = readback.mapped_stats[2];
    request_projection_success = readback.mapped_stats[3];
    for (std::uint32_t clip_index = 0U; clip_index < kMaxSupportedClipLevels;
      ++clip_index) {
      selected_pixels_per_clip[clip_index] = readback.mapped_stats[4U + clip_index];
      projected_pixels_per_clip[clip_index]
        = readback.mapped_stats[4U + kMaxSupportedClipLevels + clip_index];
    }
  }

  if (!feedback.requested_resident_keys.empty()) {
    auto& log_state = feedback_log_states_[readback.view_id.get()];
    LOG_F(INFO,
      "VirtualShadowRequestPass: frame={} slot={} view={} completed feedback "
      "(source_frame={} requested_pages={} marked_pages={} "
      "used_marked_pages={} detail_marked_pages={} "
      "used_detail_marked_pages={} address_hash=0x{:x} "
      "geometry_pixels={} clip_select_success={} clip_select_fail={} "
      "request_projection_success={} selected_hist=[{}] projected_hist=[{}] "
      "requested_hist=[{}] marked_hist=[{}] used_hist=[{}] "
      "detail_hist=[{}] used_detail_hist=[{}] first_pages=[{}])",
      Context().frame_sequence.get(), slot.get(), readback.view_id.get(),
      feedback.source_frame_sequence.get(),
      feedback.requested_resident_keys.size(),
      marked_page_count, used_marked_page_count, detail_marked_page_count,
      used_detail_marked_page_count,
      feedback.directional_address_space_hash,
      geometry_pixels, clip_select_success, clip_select_fail,
      request_projection_success, format_histogram(selected_pixels_per_clip),
      format_histogram(projected_pixels_per_clip),
      format_histogram(requested_pages_per_clip),
      format_histogram(marked_pages_per_clip),
      format_histogram(used_marked_pages_per_clip),
      format_histogram(detail_marked_pages_per_clip),
      format_histogram(used_detail_marked_pages_per_clip),
      first_requested_pages.str());
    log_state.last_feedback_count
      = static_cast<std::uint32_t>(feedback.requested_resident_keys.size());
    log_state.had_pending_feedback = true;
    shadow_manager->SubmitVirtualRequestFeedback(
      readback.view_id, std::move(feedback));
  } else {
    auto& log_state = feedback_log_states_[readback.view_id.get()];
    LOG_F(INFO,
      "VirtualShadowRequestPass: frame={} slot={} view={} completed feedback "
      "(source_frame={} requested_pages=0 marked_pages={} "
      "used_marked_pages={} detail_marked_pages={} "
      "used_detail_marked_pages={} address_hash=0x{:x} "
      "geometry_pixels={} clip_select_success={} clip_select_fail={} "
      "request_projection_success={} selected_hist=[{}] projected_hist=[{}] "
      "marked_hist=[{}] used_hist=[{}] detail_hist=[{}] "
      "used_detail_hist=[{}])",
      Context().frame_sequence.get(), slot.get(), readback.view_id.get(),
      readback.source_frame_sequence.get(),
      marked_page_count, used_marked_page_count, detail_marked_page_count,
      used_detail_marked_page_count,
      readback.directional_address_space_hash,
      geometry_pixels, clip_select_success, clip_select_fail,
      request_projection_success, format_histogram(selected_pixels_per_clip),
      format_histogram(projected_pixels_per_clip),
      format_histogram(marked_pages_per_clip),
      format_histogram(used_marked_pages_per_clip),
      format_histogram(detail_marked_pages_per_clip),
      format_histogram(used_detail_marked_pages_per_clip));
    log_state.last_feedback_count = 0U;
    log_state.had_pending_feedback = false;
    shadow_manager->ClearVirtualRequestFeedback(
      readback.view_id, renderer::VirtualShadowFeedbackKind::kDetail);
  }
  readback.pending_feedback = false;
}

} // namespace oxygen::engine
