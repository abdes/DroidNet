//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/VirtualShadowResolvePass.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Internal/ShadowBackendCommon.h>
#include <Oxygen/Renderer/Passes/VirtualShadowRequestPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::engine {

namespace {

  struct alignas(packing::kShaderDataFieldAlignment)
    VirtualShadowResolvePassConstants {
    ShaderVisibleIndex request_words_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_table_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex schedule_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex schedule_count_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t request_word_count { 0U };
    std::uint32_t total_page_count { 0U };
    std::uint32_t schedule_capacity { 0U };
    std::uint32_t _pad0 { 0U };
  };
  static_assert(sizeof(VirtualShadowResolvePassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

} // namespace

VirtualShadowResolvePass::VirtualShadowResolvePass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "VirtualShadowResolvePass")
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowResolvePass::~VirtualShadowResolvePass()
{
  for (auto& slot : slot_readbacks_) {
    if (slot.buffer && slot.mapped_bytes != nullptr) {
      slot.buffer->UnMap();
      slot.mapped_bytes = nullptr;
    }
    slot.buffer.reset();
  }
  if (clear_count_upload_buffer_ && clear_count_upload_mapped_ptr_ != nullptr) {
    clear_count_upload_buffer_->UnMap();
    clear_count_upload_mapped_ptr_ = nullptr;
  }
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
}

auto VirtualShadowResolvePass::DoPrepareResources(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  active_dispatch_ = false;
  active_view_id_ = {};
  active_request_word_count_ = 0U;
  active_dispatch_group_count_ = 0U;
  active_pages_per_axis_ = 0U;
  active_clip_level_count_ = 0U;
  active_directional_address_space_hash_ = 0U;
  active_clip_grid_origin_x_.fill(0);
  active_clip_grid_origin_y_.fill(0);
  active_schedule_capacity_ = 0U;

  if (Context().frame_slot == frame::kInvalidSlot) {
    co_return;
  }

  ProcessCompletedSchedule(Context().frame_slot);

  const auto* prepared_frame = Context().current_view.prepared_frame.get();
  if (prepared_frame == nullptr || !prepared_frame->IsValid()
    || prepared_frame->draw_metadata_bytes.empty()
    || prepared_frame->partitions.empty()) {
    co_return;
  }

  const auto* request_pass = Context().GetPass<VirtualShadowRequestPass>();
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    co_return;
  }

  // Step 4 bridge slice: the live resolve stage now explicitly owns the
  // current-frame CPU residency/page-table preparation before later upload
  // and raster consumption. The allocator still runs on the CPU, but the
  // pass-time consumers no longer need to recompute that state themselves.
  shadow_manager->ResolveVirtualCurrentFrame(Context().current_view.view_id);
  shadow_manager->PrepareVirtualPageTableResources(
    Context().current_view.view_id, recorder);

  if (request_pass == nullptr || !request_pass->HasActiveDispatch()) {
    co_return;
  }

  const auto* metadata = shadow_manager->TryGetVirtualDirectionalMetadata(
    Context().current_view.view_id);
  const auto* publication
    = shadow_manager->TryGetFramePublication(Context().current_view.view_id);
  if (metadata == nullptr || publication == nullptr
    || !publication->virtual_shadow_page_table_srv.IsValid()
    || !request_pass->GetRequestWordsSrv().IsValid()
    || !request_pass->GetRequestWordsBuffer()) {
    co_return;
  }

  if (metadata->clip_level_count == 0U || metadata->pages_per_axis == 0U) {
    co_return;
  }

  const auto total_page_count = metadata->clip_level_count
    * metadata->pages_per_axis * metadata->pages_per_axis;
  if (total_page_count == 0U) {
    co_return;
  }
  EnsurePassConstantsBuffer();
  EnsureClearCountUploadBuffer();
  EnsureReadbackBuffer(Context().frame_slot);

  auto* resources = EnsureViewScheduleResources(
    Context().current_view.view_id, total_page_count);
  if (resources == nullptr || !resources->schedule_uav.IsValid()
    || !resources->count_uav.IsValid()) {
    co_return;
  }

  if (!recorder.IsResourceTracked(*request_pass->GetRequestWordsBuffer())) {
    recorder.BeginTrackingResourceState(*request_pass->GetRequestWordsBuffer(),
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*resources->schedule_buffer)) {
    recorder.BeginTrackingResourceState(
      *resources->schedule_buffer, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*resources->count_buffer)) {
    recorder.BeginTrackingResourceState(
      *resources->count_buffer, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*clear_count_upload_buffer_)) {
    recorder.BeginTrackingResourceState(*clear_count_upload_buffer_,
      graphics::ResourceStates::kCopySource, false);
  }

  recorder.RequireResourceState(*request_pass->GetRequestWordsBuffer(),
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *resources->count_buffer, graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*resources->count_buffer, 0U, *clear_count_upload_buffer_,
    0U, sizeof(std::uint32_t));

  recorder.RequireResourceState(
    *resources->schedule_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *resources->count_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  const VirtualShadowResolvePassConstants pass_constants {
    .request_words_srv_index = request_pass->GetRequestWordsSrv(),
    .page_table_srv_index = publication->virtual_shadow_page_table_srv,
    .schedule_uav_index = resources->schedule_uav,
    .schedule_count_uav_index = resources->count_uav,
    .request_word_count = request_pass->GetActiveRequestWordCount(),
    .total_page_count = total_page_count,
    .schedule_capacity = resources->entry_capacity,
  };
  std::memcpy(
    pass_constants_mapped_ptr_, &pass_constants, sizeof(pass_constants));
  SetPassConstantsIndex(pass_constants_index_);

  active_dispatch_ = true;
  active_view_id_ = Context().current_view.view_id;
  active_request_word_count_ = request_pass->GetActiveRequestWordCount();
  active_pages_per_axis_ = metadata->pages_per_axis;
  active_clip_level_count_ = metadata->clip_level_count;
  active_directional_address_space_hash_ = renderer::internal::shadow_detail::
    HashDirectionalVirtualFeedbackAddressSpace(*metadata);
  const auto active_clip_count
    = std::min(metadata->clip_level_count, kMaxSupportedClipLevels);
  for (std::uint32_t clip_index = 0U; clip_index < active_clip_count;
    ++clip_index) {
    active_clip_grid_origin_x_[clip_index] = renderer::internal::shadow_detail::
      ResolveDirectionalVirtualClipGridOriginX(*metadata, clip_index);
    active_clip_grid_origin_y_[clip_index] = renderer::internal::shadow_detail::
      ResolveDirectionalVirtualClipGridOriginY(*metadata, clip_index);
  }
  active_schedule_capacity_ = resources->entry_capacity;
  active_dispatch_group_count_
    = (std::max(1U, active_request_word_count_) + kDispatchGroupSize - 1U)
    / kDispatchGroupSize;

  LOG_F(INFO,
    "VirtualShadowResolvePass: frame={} slot={} view={} prepared dispatch "
    "(words={} pages_per_axis={} clips={} address_hash=0x{:x} capacity={})",
    Context().frame_sequence.get(), Context().frame_slot.get(),
    Context().current_view.view_id.get(), active_request_word_count_,
    active_pages_per_axis_, active_clip_level_count_,
    active_directional_address_space_hash_, active_schedule_capacity_);

  co_return;
}

auto VirtualShadowResolvePass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  if (!active_dispatch_) {
    co_return;
  }

  recorder.Dispatch(active_dispatch_group_count_, 1U, 1U);

  auto it = view_schedule_resources_.find(active_view_id_);
  if (it != view_schedule_resources_.end()) {
    auto& readback = slot_readbacks_[Context().frame_slot.get()];
    if (!recorder.IsResourceTracked(*readback.buffer)) {
      recorder.BeginTrackingResourceState(
        *readback.buffer, graphics::ResourceStates::kCopyDest, false);
    }
    recorder.RequireResourceState(
      *it->second.count_buffer, graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *it->second.schedule_buffer, graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *readback.buffer, graphics::ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyBuffer(*readback.buffer, 0U, *it->second.count_buffer, 0U,
      sizeof(std::uint32_t));
    recorder.CopyBuffer(*readback.buffer, sizeof(std::uint32_t),
      *it->second.schedule_buffer, 0U,
      static_cast<std::size_t>(active_schedule_capacity_)
        * sizeof(ScheduleEntry));

    readback.pending_schedule = true;
    readback.view_id = active_view_id_;
    readback.source_frame_sequence = Context().frame_sequence;
    readback.pages_per_axis = active_pages_per_axis_;
    readback.clip_level_count = active_clip_level_count_;
    readback.directional_address_space_hash
      = active_directional_address_space_hash_;
    readback.clip_grid_origin_x = active_clip_grid_origin_x_;
    readback.clip_grid_origin_y = active_clip_grid_origin_y_;
    readback.schedule_capacity = active_schedule_capacity_;

    LOG_F(INFO,
      "VirtualShadowResolvePass: frame={} slot={} view={} dispatched resolve "
      "(groups={} scheduled_capacity={})",
      Context().frame_sequence.get(), Context().frame_slot.get(),
      Context().current_view.view_id.get(), active_dispatch_group_count_,
      active_schedule_capacity_);

    recorder.RequireResourceState(
      *it->second.schedule_buffer, graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *it->second.count_buffer, graphics::ResourceStates::kShaderResource);
    recorder.FlushBarriers();
  }

  co_return;
}

auto VirtualShadowResolvePass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowResolvePass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowResolvePass: config is null");
  }
}

auto VirtualShadowResolvePass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  graphics::ShaderRequest shader_request {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Lighting/VirtualShadowResolve.hlsl",
    .entry_point = "CS",
  };

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader(std::move(shader_request))
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowResolve_PSO")
    .Build();
}

auto VirtualShadowResolvePass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

auto VirtualShadowResolvePass::EnsurePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr
    && pass_constants_index_.IsValid()) {
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();

  const graphics::BufferDesc desc {
    .size_bytes = 256U,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowResolvePass.Constants",
  };
  pass_constants_buffer_ = gfx_->CreateBuffer(desc);
  if (!pass_constants_buffer_) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to create constants buffer");
  }
  registry.Register(pass_constants_buffer_);

  pass_constants_mapped_ptr_ = pass_constants_buffer_->Map(0U, desc.size_bytes);
  if (pass_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to map constants buffer");
  }

  auto cbv_handle
    = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!cbv_handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to allocate constants CBV");
  }
  pass_constants_index_ = allocator.GetShaderVisibleIndex(cbv_handle);

  graphics::BufferViewDescription cbv_desc;
  cbv_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
  cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  cbv_desc.range = { 0U, desc.size_bytes };
  pass_constants_cbv_ = registry.RegisterView(
    *pass_constants_buffer_, std::move(cbv_handle), cbv_desc);
}

auto VirtualShadowResolvePass::EnsureClearCountUploadBuffer() -> void
{
  if (clear_count_upload_buffer_ && clear_count_upload_mapped_ptr_ != nullptr) {
    return;
  }

  const graphics::BufferDesc desc {
    .size_bytes = sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowResolvePass.CountClearUpload",
  };
  clear_count_upload_buffer_ = gfx_->CreateBuffer(desc);
  if (!clear_count_upload_buffer_) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to create count clear upload buffer");
  }

  clear_count_upload_mapped_ptr_
    = clear_count_upload_buffer_->Map(0U, desc.size_bytes);
  if (clear_count_upload_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to map count clear upload buffer");
  }
  std::memset(clear_count_upload_mapped_ptr_, 0, desc.size_bytes);
}

auto VirtualShadowResolvePass::EnsureReadbackBuffer(const frame::Slot slot)
  -> void
{
  auto& readback = slot_readbacks_[slot.get()];
  if (readback.buffer && readback.mapped_bytes != nullptr) {
    return;
  }

  const auto schedule_bytes = sizeof(std::uint32_t)
    + static_cast<std::uint64_t>(kMaxSupportedPageCount)
      * sizeof(ScheduleEntry);
  const graphics::BufferDesc desc {
    .size_bytes = schedule_bytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kReadBack,
    .debug_name = "VirtualShadowResolvePass.Readback",
  };
  readback.buffer = gfx_->CreateBuffer(desc);
  if (!readback.buffer) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to create readback buffer");
  }

  readback.mapped_bytes
    = static_cast<std::byte*>(readback.buffer->Map(0U, desc.size_bytes));
  if (readback.mapped_bytes == nullptr) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to map readback buffer");
  }
  std::memset(
    readback.mapped_bytes, 0, static_cast<std::size_t>(desc.size_bytes));
}

auto VirtualShadowResolvePass::ProcessCompletedSchedule(const frame::Slot slot)
  -> void
{
  auto& readback = slot_readbacks_[slot.get()];
  if (!readback.pending_schedule || readback.mapped_bytes == nullptr) {
    return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    readback.pending_schedule = false;
    return;
  }

  const auto* schedule_count
    = reinterpret_cast<const std::uint32_t*>(readback.mapped_bytes);
  const auto* schedule_entries = reinterpret_cast<const ScheduleEntry*>(
    readback.mapped_bytes + sizeof(std::uint32_t));

  renderer::VirtualShadowResolvedRasterSchedule schedule {};
  schedule.source_frame_sequence = readback.source_frame_sequence;
  schedule.pages_per_axis = readback.pages_per_axis;
  schedule.clip_level_count = readback.clip_level_count;
  schedule.directional_address_space_hash
    = readback.directional_address_space_hash;

  const auto pages_per_level
    = readback.pages_per_axis * readback.pages_per_axis;
  const auto scheduled_page_count
    = std::min(*schedule_count, readback.schedule_capacity);
  schedule.requested_resident_keys.reserve(scheduled_page_count);
  for (std::uint32_t i = 0U; i < scheduled_page_count; ++i) {
    if (pages_per_level == 0U || readback.pages_per_axis == 0U) {
      break;
    }

    const auto global_page_index = schedule_entries[i].global_page_index;
    const auto clip_index = global_page_index / pages_per_level;
    if (clip_index >= readback.clip_level_count) {
      continue;
    }

    const auto local_page_index = global_page_index % pages_per_level;
    const auto page_y = local_page_index / readback.pages_per_axis;
    const auto page_x = local_page_index % readback.pages_per_axis;
    schedule.requested_resident_keys.push_back(
      renderer::internal::shadow_detail::PackVirtualResidentPageKey(clip_index,
        readback.clip_grid_origin_x[clip_index]
          + static_cast<std::int32_t>(page_x),
        readback.clip_grid_origin_y[clip_index]
          + static_cast<std::int32_t>(page_y)));
  }

  LOG_F(INFO,
    "VirtualShadowResolvePass: frame={} slot={} view={} completed schedule "
    "(source_frame={} scheduled_pages={} address_hash=0x{:x})",
    Context().frame_sequence.get(), slot.get(), readback.view_id.get(),
    schedule.source_frame_sequence.get(),
    schedule.requested_resident_keys.size(),
    schedule.directional_address_space_hash);

  shadow_manager->SubmitVirtualResolvedRasterSchedule(
    readback.view_id, std::move(schedule));
  readback.pending_schedule = false;
}

auto VirtualShadowResolvePass::EnsureViewScheduleResources(const ViewId view_id,
  const std::uint32_t required_entry_capacity) -> ViewScheduleResources*
{
  if (required_entry_capacity == 0U) {
    return nullptr;
  }

  auto [it, _] = view_schedule_resources_.try_emplace(view_id);
  auto& resources = it->second;
  if (resources.schedule_buffer && resources.count_buffer
    && required_entry_capacity <= resources.entry_capacity) {
    return &resources;
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();
  const auto schedule_size_bytes
    = static_cast<std::uint64_t>(required_entry_capacity)
    * sizeof(std::uint32_t) * 4U;

  const graphics::BufferDesc schedule_desc {
    .size_bytes = schedule_size_bytes,
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "VirtualShadowResolvePass.Schedule",
  };
  resources.schedule_buffer = gfx_->CreateBuffer(schedule_desc);
  if (!resources.schedule_buffer) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to create schedule buffer");
  }
  registry.Register(resources.schedule_buffer);

  auto schedule_srv_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!schedule_srv_handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to allocate schedule SRV");
  }
  resources.schedule_srv = allocator.GetShaderVisibleIndex(schedule_srv_handle);

  graphics::BufferViewDescription schedule_srv_desc;
  schedule_srv_desc.view_type
    = graphics::ResourceViewType::kStructuredBuffer_SRV;
  schedule_srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  schedule_srv_desc.range = { 0U, schedule_size_bytes };
  schedule_srv_desc.stride = sizeof(std::uint32_t) * 4U;
  registry.RegisterView(*resources.schedule_buffer,
    std::move(schedule_srv_handle), schedule_srv_desc);

  auto schedule_uav_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!schedule_uav_handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to allocate schedule UAV");
  }
  resources.schedule_uav = allocator.GetShaderVisibleIndex(schedule_uav_handle);

  graphics::BufferViewDescription schedule_uav_desc;
  schedule_uav_desc.view_type
    = graphics::ResourceViewType::kStructuredBuffer_UAV;
  schedule_uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  schedule_uav_desc.range = { 0U, schedule_size_bytes };
  schedule_uav_desc.stride = sizeof(std::uint32_t) * 4U;
  registry.RegisterView(*resources.schedule_buffer,
    std::move(schedule_uav_handle), schedule_uav_desc);

  const graphics::BufferDesc count_desc {
    .size_bytes = sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "VirtualShadowResolvePass.ScheduleCount",
  };
  resources.count_buffer = gfx_->CreateBuffer(count_desc);
  if (!resources.count_buffer) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to create schedule count buffer");
  }
  registry.Register(resources.count_buffer);

  auto count_srv_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!count_srv_handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to allocate schedule count SRV");
  }
  resources.count_srv = allocator.GetShaderVisibleIndex(count_srv_handle);

  graphics::BufferViewDescription count_srv_desc;
  count_srv_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  count_srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  count_srv_desc.range = { 0U, sizeof(std::uint32_t) };
  count_srv_desc.stride = sizeof(std::uint32_t);
  registry.RegisterView(
    *resources.count_buffer, std::move(count_srv_handle), count_srv_desc);

  auto count_uav_handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!count_uav_handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowResolvePass: failed to allocate schedule count UAV");
  }
  resources.count_uav = allocator.GetShaderVisibleIndex(count_uav_handle);

  graphics::BufferViewDescription count_uav_desc;
  count_uav_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_UAV;
  count_uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  count_uav_desc.range = { 0U, sizeof(std::uint32_t) };
  count_uav_desc.stride = sizeof(std::uint32_t);
  registry.RegisterView(
    *resources.count_buffer, std::move(count_uav_handle), count_uav_desc);

  resources.entry_capacity = required_entry_capacity;
  return &resources;
}

} // namespace oxygen::engine
