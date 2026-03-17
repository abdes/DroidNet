//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Passes/Detail/VirtualShadowPassConstants.h>
#include <Oxygen/Renderer/Passes/VirtualShadowRequestPass.h>
#include <Oxygen/Renderer/Passes/VirtualShadowSchedulePass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>

namespace oxygen::engine {

VirtualShadowSchedulePass::VirtualShadowSchedulePass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "VirtualShadowSchedulePass")
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowSchedulePass::~VirtualShadowSchedulePass()
{
  if (clear_count_upload_buffer_ && clear_count_upload_mapped_ptr_ != nullptr) {
    clear_count_upload_buffer_->UnMap();
    clear_count_upload_mapped_ptr_ = nullptr;
  }
}

auto VirtualShadowSchedulePass::DoPrepareResources(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  active_dispatch_ = false;
  active_view_id_ = {};
  active_draw_bounds_srv_ = kInvalidShaderVisibleIndex;
  active_page_management_bindings_ = {};
  active_total_page_count_ = 0U;
  active_dispatch_group_count_ = 0U;
  active_draw_count_ = 0U;

  if (Context().frame_slot == frame::kInvalidSlot) {
    co_return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    co_return;
  }
  shadow_manager->ClearVirtualGpuRasterInputs(Context().current_view.view_id);

  const auto* prepared_frame = Context().current_view.prepared_frame.get();
  if (prepared_frame == nullptr || !prepared_frame->IsValid()
    || prepared_frame->draw_metadata_bytes.empty()
    || prepared_frame->partitions.empty()) {
    co_return;
  }

  const auto* request_pass = Context().GetPass<VirtualShadowRequestPass>();
  shadow_manager->ResolveVirtualCurrentFrame(Context().current_view.view_id);
  shadow_manager->PrepareVirtualPageTableResources(
    Context().current_view.view_id, recorder);

  const auto* metadata = shadow_manager->TryGetVirtualDirectionalMetadata(
    Context().current_view.view_id);
  const auto* page_management_bindings
    = shadow_manager->TryGetVirtualPageManagementBindings(
      Context().current_view.view_id);
  if (metadata == nullptr || page_management_bindings == nullptr
    || !page_management_bindings->page_table_uav.IsValid()
    || !page_management_bindings->page_flags_uav.IsValid()
    || !page_management_bindings->resolve_stats_uav.IsValid()
    || metadata->clip_level_count == 0U || metadata->pages_per_axis == 0U) {
    co_return;
  }

  const auto total_page_count = metadata->clip_level_count
    * metadata->pages_per_axis * metadata->pages_per_axis;
  if (total_page_count == 0U) {
    co_return;
  }

  const auto draw_count = static_cast<std::uint32_t>(
    prepared_frame->draw_metadata_bytes.size() / sizeof(DrawMetadata));
  pass_constants_.Ensure(
    *gfx_, "VirtualShadowSchedulePass.Constants",
    detail::kVirtualShadowPassConstantsStride);
  EnsureClearCountUploadBuffer();

  auto* resources = EnsureViewScheduleResources(
    Context().current_view.view_id, total_page_count, draw_count);
  if (resources == nullptr || !resources->schedule_uav.IsValid()
    || !resources->schedule_lookup_uav.IsValid()
    || !resources->count_uav.IsValid() || !resources->clear_args_uav.IsValid()
    || !resources->draw_args_uav.IsValid()
    || !resources->draw_page_ranges_uav.IsValid()
    || !resources->draw_page_indices_uav.IsValid()
    || !resources->draw_page_counter_uav.IsValid()) {
    co_return;
  }

  const bool has_request_dispatch = request_pass != nullptr
    && request_pass->HasActiveDispatch()
    && request_pass->GetRequestWordsSrv().IsValid()
    && request_pass->GetRequestWordsBuffer() != nullptr;
  const bool has_page_mark_flags = request_pass != nullptr
    && request_pass->GetPageMarkFlagsSrv().IsValid()
    && request_pass->GetPageMarkFlagsBuffer() != nullptr;

  active_dispatch_ = true;
  active_view_id_ = Context().current_view.view_id;
  active_draw_bounds_srv_ = prepared_frame->bindless_draw_bounds_slot;
  active_page_management_bindings_ = *page_management_bindings;
  active_total_page_count_ = total_page_count;
  active_dispatch_group_count_
    = (total_page_count + kDispatchGroupSize - 1U) / kDispatchGroupSize;
  active_draw_count_ = draw_count;

  if (has_request_dispatch
    && !recorder.IsResourceTracked(*request_pass->GetRequestWordsBuffer())) {
    recorder.BeginTrackingResourceState(*request_pass->GetRequestWordsBuffer(),
      graphics::ResourceStates::kCommon, true);
  }
  if (has_page_mark_flags
    && !recorder.IsResourceTracked(*request_pass->GetPageMarkFlagsBuffer())) {
    recorder.BeginTrackingResourceState(*request_pass->GetPageMarkFlagsBuffer(),
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*resources->schedule_buffer)) {
    recorder.BeginTrackingResourceState(
      *resources->schedule_buffer, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*resources->schedule_lookup_buffer)) {
    recorder.BeginTrackingResourceState(*resources->schedule_lookup_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*resources->count_buffer)) {
    recorder.BeginTrackingResourceState(
      *resources->count_buffer, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*resources->clear_args_buffer)) {
    recorder.BeginTrackingResourceState(
      *resources->clear_args_buffer, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*resources->draw_args_buffer)) {
    recorder.BeginTrackingResourceState(
      *resources->draw_args_buffer, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*resources->draw_page_ranges_buffer)) {
    recorder.BeginTrackingResourceState(*resources->draw_page_ranges_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*resources->draw_page_indices_buffer)) {
    recorder.BeginTrackingResourceState(*resources->draw_page_indices_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*resources->draw_page_counter_buffer)) {
    recorder.BeginTrackingResourceState(*resources->draw_page_counter_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*clear_count_upload_buffer_)) {
    recorder.BeginTrackingResourceState(*clear_count_upload_buffer_,
      graphics::ResourceStates::kCopySource, false);
  }

  if (has_request_dispatch) {
    recorder.RequireResourceState(*request_pass->GetRequestWordsBuffer(),
      graphics::ResourceStates::kShaderResource);
  }
  if (has_page_mark_flags) {
    recorder.RequireResourceState(*request_pass->GetPageMarkFlagsBuffer(),
      graphics::ResourceStates::kShaderResource);
  }
  recorder.RequireResourceState(
    *resources->count_buffer, graphics::ResourceStates::kCopyDest);
  recorder.RequireResourceState(
    *resources->draw_page_counter_buffer, graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*resources->count_buffer, 0U, *clear_count_upload_buffer_,
    0U, sizeof(std::uint32_t));
  recorder.CopyBuffer(*resources->draw_page_counter_buffer, 0U,
    *clear_count_upload_buffer_, 0U, sizeof(std::uint32_t));

  recorder.RequireResourceState(
    *resources->schedule_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*resources->schedule_lookup_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *resources->count_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *resources->clear_args_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *resources->draw_args_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*resources->draw_page_ranges_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*resources->draw_page_indices_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*resources->draw_page_counter_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  const auto slot = static_cast<std::size_t>(Context().frame_slot.get());
  const auto pass_constants_index = pass_constants_.Index(slot);
  const detail::VirtualShadowPassConstants constants {
    .schedule_uav_index = resources->schedule_uav,
    .schedule_lookup_uav_index = resources->schedule_lookup_uav,
    .schedule_count_uav_index = resources->count_uav,
    .page_table_uav_index = page_management_bindings->page_table_uav,
    .page_flags_uav_index = page_management_bindings->page_flags_uav,
    .resolve_stats_uav_index = page_management_bindings->resolve_stats_uav,
    .total_page_count = total_page_count,
    .schedule_capacity = resources->entry_capacity,
  };
  auto* slot_ptr = static_cast<std::byte*>(pass_constants_.MappedPtr())
    + static_cast<std::ptrdiff_t>(slot * detail::kVirtualShadowPassConstantsStride);
  std::memcpy(slot_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(pass_constants_index);

  shadow_manager->SubmitVirtualGpuRasterInputs(Context().current_view.view_id,
    renderer::VirtualShadowGpuRasterInputs {
      .schedule_buffer = resources->schedule_buffer,
      .schedule_srv = resources->schedule_srv,
      .schedule_count_buffer = resources->count_buffer,
      .schedule_count_srv = resources->count_srv,
      .draw_page_ranges_buffer = resources->draw_page_ranges_buffer,
      .draw_page_ranges_srv = resources->draw_page_ranges_srv,
      .draw_page_indices_buffer = resources->draw_page_indices_buffer,
      .draw_page_indices_srv = resources->draw_page_indices_srv,
      .clear_indirect_args_buffer = resources->clear_args_buffer,
      .draw_indirect_args_buffer = resources->draw_args_buffer,
      .source_frame_sequence = Context().frame_sequence,
      .draw_count = active_draw_count_,
      .pending_raster_page_count
      = page_management_bindings->pending_raster_page_count,
    });

  co_return;
}

auto VirtualShadowSchedulePass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  if (!active_dispatch_) {
    co_return;
  }

  const auto* resources = GetScheduleResources();
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (resources == nullptr || shadow_manager == nullptr) {
    co_return;
  }

  recorder.RequireResourceState(
    *resources->schedule_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*resources->schedule_lookup_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *resources->count_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  detail::DispatchVirtualPageManagementPass(
    *shadow_manager, active_view_id_, recorder, active_dispatch_group_count_);

  co_return;
}

auto VirtualShadowSchedulePass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowSchedulePass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowSchedulePass: config is null");
  }
}

auto VirtualShadowSchedulePass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader({ .stage = oxygen::ShaderType::kCompute,
      .source_path = "Lighting/VirtualShadowSchedule.hlsl",
      .entry_point = "CS" })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowSchedule_PSO")
    .Build();
}

auto VirtualShadowSchedulePass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

auto VirtualShadowSchedulePass::EnsureClearCountUploadBuffer() -> void
{
  if (clear_count_upload_buffer_ && clear_count_upload_mapped_ptr_ != nullptr) {
    return;
  }

  const graphics::BufferDesc desc {
    .size_bytes = sizeof(std::uint32_t),
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowSchedulePass.CountClearUpload",
  };
  clear_count_upload_buffer_ = gfx_->CreateBuffer(desc);
  if (!clear_count_upload_buffer_) {
    throw std::runtime_error(
      "VirtualShadowSchedulePass: failed to create count clear upload buffer");
  }

  clear_count_upload_mapped_ptr_
    = clear_count_upload_buffer_->Map(0U, desc.size_bytes);
  if (clear_count_upload_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "VirtualShadowSchedulePass: failed to map count clear upload buffer");
  }
  std::memset(clear_count_upload_mapped_ptr_, 0, desc.size_bytes);
}

auto VirtualShadowSchedulePass::EnsureViewScheduleResources(
  const ViewId view_id, const std::uint32_t required_entry_capacity,
  const std::uint32_t required_draw_count)
  -> detail::VirtualShadowScheduleResources*
{
  if (required_entry_capacity == 0U) {
    return nullptr;
  }

  auto [it, _] = view_schedule_resources_.try_emplace(view_id);
  auto& resources = it->second;
  const bool schedule_ready = resources.schedule_buffer
    && resources.schedule_lookup_buffer
    && resources.count_buffer
    && required_entry_capacity <= resources.entry_capacity;
  const auto draw_arg_capacity = std::max(required_draw_count, 1U);
  const auto draw_page_index_capacity
    = std::max(required_entry_capacity * draw_arg_capacity, 1U);
  const bool draw_args_ready = resources.clear_args_buffer
    && resources.draw_args_buffer
    && draw_arg_capacity <= resources.draw_arg_capacity;
  const bool draw_page_lists_ready = resources.draw_page_ranges_buffer
    && resources.draw_page_indices_buffer && resources.draw_page_counter_buffer
    && draw_arg_capacity <= resources.draw_arg_capacity
    && draw_page_index_capacity <= resources.draw_page_index_capacity;
  if (schedule_ready && draw_args_ready && draw_page_lists_ready) {
    return &resources;
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();

  if (!schedule_ready) {
    const auto schedule_size_bytes
      = static_cast<std::uint64_t>(required_entry_capacity)
      * sizeof(std::uint32_t) * 4U;
    const graphics::BufferDesc schedule_desc {
      .size_bytes = schedule_size_bytes,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowSchedulePass.Schedule",
    };
    resources.schedule_buffer = gfx_->CreateBuffer(schedule_desc);
    if (!resources.schedule_buffer) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to create schedule buffer");
    }
    registry.Register(resources.schedule_buffer);

    auto schedule_srv_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!schedule_srv_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to allocate schedule SRV");
    }
    resources.schedule_srv
      = allocator.GetShaderVisibleIndex(schedule_srv_handle);
    graphics::BufferViewDescription schedule_srv_desc;
    schedule_srv_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_SRV;
    schedule_srv_desc.visibility
      = graphics::DescriptorVisibility::kShaderVisible;
    schedule_srv_desc.range = { 0U, schedule_size_bytes };
    schedule_srv_desc.stride = sizeof(std::uint32_t) * 4U;
    registry.RegisterView(*resources.schedule_buffer,
      std::move(schedule_srv_handle), schedule_srv_desc);

    auto schedule_uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!schedule_uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to allocate schedule UAV");
    }
    resources.schedule_uav
      = allocator.GetShaderVisibleIndex(schedule_uav_handle);
    graphics::BufferViewDescription schedule_uav_desc;
    schedule_uav_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_UAV;
    schedule_uav_desc.visibility
      = graphics::DescriptorVisibility::kShaderVisible;
    schedule_uav_desc.range = { 0U, schedule_size_bytes };
    schedule_uav_desc.stride = sizeof(std::uint32_t) * 4U;
    registry.RegisterView(*resources.schedule_buffer,
      std::move(schedule_uav_handle), schedule_uav_desc);

    const graphics::BufferDesc lookup_desc {
      .size_bytes = static_cast<std::uint64_t>(required_entry_capacity)
        * sizeof(std::uint32_t),
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowSchedulePass.ScheduleLookup",
    };
    resources.schedule_lookup_buffer = gfx_->CreateBuffer(lookup_desc);
    if (!resources.schedule_lookup_buffer) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to create schedule lookup buffer");
    }
    registry.Register(resources.schedule_lookup_buffer);

    auto lookup_uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!lookup_uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to allocate schedule lookup UAV");
    }
    resources.schedule_lookup_uav
      = allocator.GetShaderVisibleIndex(lookup_uav_handle);
    graphics::BufferViewDescription lookup_uav_desc;
    lookup_uav_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_UAV;
    lookup_uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    lookup_uav_desc.range = { 0U,
      static_cast<std::uint64_t>(required_entry_capacity) * sizeof(std::uint32_t) };
    lookup_uav_desc.stride = sizeof(std::uint32_t);
    registry.RegisterView(*resources.schedule_lookup_buffer,
      std::move(lookup_uav_handle), lookup_uav_desc);

    const graphics::BufferDesc count_desc {
      .size_bytes = sizeof(std::uint32_t),
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowSchedulePass.ScheduleCount",
    };
    resources.count_buffer = gfx_->CreateBuffer(count_desc);
    if (!resources.count_buffer) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to create schedule count buffer");
    }
    registry.Register(resources.count_buffer);

    auto count_srv_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!count_srv_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to allocate schedule count SRV");
    }
    resources.count_srv = allocator.GetShaderVisibleIndex(count_srv_handle);
    graphics::BufferViewDescription count_srv_desc;
    count_srv_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_SRV;
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
        "VirtualShadowSchedulePass: failed to allocate schedule count UAV");
    }
    resources.count_uav = allocator.GetShaderVisibleIndex(count_uav_handle);
    graphics::BufferViewDescription count_uav_desc;
    count_uav_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_UAV;
    count_uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    count_uav_desc.range = { 0U, sizeof(std::uint32_t) };
    count_uav_desc.stride = sizeof(std::uint32_t);
    registry.RegisterView(
      *resources.count_buffer, std::move(count_uav_handle), count_uav_desc);

    resources.entry_capacity = required_entry_capacity;
  }

  if (!draw_args_ready) {
    const auto clear_arg_stride = sizeof(std::uint32_t) * 4U;
    const auto draw_arg_stride = sizeof(std::uint32_t) * 5U;

    const graphics::BufferDesc clear_args_desc {
      .size_bytes = clear_arg_stride,
      .usage
      = graphics::BufferUsage::kStorage | graphics::BufferUsage::kIndirect,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowSchedulePass.ClearIndirectArgs",
    };
    resources.clear_args_buffer = gfx_->CreateBuffer(clear_args_desc);
    if (!resources.clear_args_buffer) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to create clear indirect args buffer");
    }
    registry.Register(resources.clear_args_buffer);

    auto clear_args_uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!clear_args_uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to allocate clear indirect args UAV");
    }
    resources.clear_args_uav
      = allocator.GetShaderVisibleIndex(clear_args_uav_handle);
    graphics::BufferViewDescription clear_args_uav_desc;
    clear_args_uav_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_UAV;
    clear_args_uav_desc.visibility
      = graphics::DescriptorVisibility::kShaderVisible;
    clear_args_uav_desc.range = { 0U, clear_arg_stride };
    clear_args_uav_desc.stride = clear_arg_stride;
    registry.RegisterView(*resources.clear_args_buffer,
      std::move(clear_args_uav_handle), clear_args_uav_desc);

    const graphics::BufferDesc draw_args_desc {
      .size_bytes = static_cast<std::uint64_t>(draw_arg_capacity) * draw_arg_stride,
      .usage
      = graphics::BufferUsage::kStorage | graphics::BufferUsage::kIndirect,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowSchedulePass.DrawIndirectArgs",
    };
    resources.draw_args_buffer = gfx_->CreateBuffer(draw_args_desc);
    if (!resources.draw_args_buffer) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to create draw indirect args buffer");
    }
    registry.Register(resources.draw_args_buffer);

    auto draw_args_uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!draw_args_uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to allocate draw indirect args UAV");
    }
    resources.draw_args_uav
      = allocator.GetShaderVisibleIndex(draw_args_uav_handle);
    graphics::BufferViewDescription draw_args_uav_desc;
    draw_args_uav_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_UAV;
    draw_args_uav_desc.visibility
      = graphics::DescriptorVisibility::kShaderVisible;
    draw_args_uav_desc.range
      = { 0U, static_cast<std::uint64_t>(draw_arg_capacity) * draw_arg_stride };
    draw_args_uav_desc.stride = draw_arg_stride;
    registry.RegisterView(*resources.draw_args_buffer,
      std::move(draw_args_uav_handle), draw_args_uav_desc);

    resources.draw_arg_capacity = draw_arg_capacity;
  }

  if (!draw_page_lists_ready) {
    const auto range_stride = sizeof(std::uint32_t) * 4U;
    const auto index_stride = sizeof(std::uint32_t);

    const graphics::BufferDesc ranges_desc {
      .size_bytes = static_cast<std::uint64_t>(draw_arg_capacity) * range_stride,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowSchedulePass.DrawPageRanges",
    };
    resources.draw_page_ranges_buffer = gfx_->CreateBuffer(ranges_desc);
    if (!resources.draw_page_ranges_buffer) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to create draw page ranges buffer");
    }
    registry.Register(resources.draw_page_ranges_buffer);

    auto ranges_srv_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!ranges_srv_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to allocate draw page ranges SRV");
    }
    resources.draw_page_ranges_srv
      = allocator.GetShaderVisibleIndex(ranges_srv_handle);
    graphics::BufferViewDescription ranges_srv_desc;
    ranges_srv_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_SRV;
    ranges_srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    ranges_srv_desc.range
      = { 0U, static_cast<std::uint64_t>(draw_arg_capacity) * range_stride };
    ranges_srv_desc.stride = range_stride;
    registry.RegisterView(*resources.draw_page_ranges_buffer,
      std::move(ranges_srv_handle), ranges_srv_desc);

    auto ranges_uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!ranges_uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to allocate draw page ranges UAV");
    }
    resources.draw_page_ranges_uav
      = allocator.GetShaderVisibleIndex(ranges_uav_handle);
    graphics::BufferViewDescription ranges_uav_desc;
    ranges_uav_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_UAV;
    ranges_uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    ranges_uav_desc.range
      = { 0U, static_cast<std::uint64_t>(draw_arg_capacity) * range_stride };
    ranges_uav_desc.stride = range_stride;
    registry.RegisterView(*resources.draw_page_ranges_buffer,
      std::move(ranges_uav_handle), ranges_uav_desc);

    const graphics::BufferDesc indices_desc {
      .size_bytes
      = static_cast<std::uint64_t>(draw_page_index_capacity) * index_stride,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowSchedulePass.DrawPageIndices",
    };
    resources.draw_page_indices_buffer = gfx_->CreateBuffer(indices_desc);
    if (!resources.draw_page_indices_buffer) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to create draw page indices buffer");
    }
    registry.Register(resources.draw_page_indices_buffer);

    auto indices_srv_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!indices_srv_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to allocate draw page indices SRV");
    }
    resources.draw_page_indices_srv
      = allocator.GetShaderVisibleIndex(indices_srv_handle);
    graphics::BufferViewDescription indices_srv_desc;
    indices_srv_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_SRV;
    indices_srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    indices_srv_desc.range = { 0U,
      static_cast<std::uint64_t>(draw_page_index_capacity) * index_stride };
    indices_srv_desc.stride = index_stride;
    registry.RegisterView(*resources.draw_page_indices_buffer,
      std::move(indices_srv_handle), indices_srv_desc);

    auto indices_uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!indices_uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to allocate draw page indices UAV");
    }
    resources.draw_page_indices_uav
      = allocator.GetShaderVisibleIndex(indices_uav_handle);
    graphics::BufferViewDescription indices_uav_desc;
    indices_uav_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_UAV;
    indices_uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    indices_uav_desc.range = { 0U,
      static_cast<std::uint64_t>(draw_page_index_capacity) * index_stride };
    indices_uav_desc.stride = index_stride;
    registry.RegisterView(*resources.draw_page_indices_buffer,
      std::move(indices_uav_handle), indices_uav_desc);

    const graphics::BufferDesc counter_desc {
      .size_bytes = sizeof(std::uint32_t),
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowSchedulePass.DrawPageCounter",
    };
    resources.draw_page_counter_buffer = gfx_->CreateBuffer(counter_desc);
    if (!resources.draw_page_counter_buffer) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to create draw page counter buffer");
    }
    registry.Register(resources.draw_page_counter_buffer);

    auto counter_uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!counter_uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowSchedulePass: failed to allocate draw page counter UAV");
    }
    resources.draw_page_counter_uav
      = allocator.GetShaderVisibleIndex(counter_uav_handle);
    graphics::BufferViewDescription counter_uav_desc;
    counter_uav_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_UAV;
    counter_uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    counter_uav_desc.range = { 0U, sizeof(std::uint32_t) };
    counter_uav_desc.stride = sizeof(std::uint32_t);
    registry.RegisterView(*resources.draw_page_counter_buffer,
      std::move(counter_uav_handle), counter_uav_desc);

    resources.draw_page_index_capacity = draw_page_index_capacity;
  }

  return &resources;
}

} // namespace oxygen::engine
