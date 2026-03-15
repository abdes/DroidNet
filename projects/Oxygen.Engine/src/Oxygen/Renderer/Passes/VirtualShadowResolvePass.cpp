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
#include <Oxygen/Renderer/Passes/VirtualShadowResolvePass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>

namespace oxygen::engine {

namespace {

  struct alignas(16) ResolvePackedInt4 {
    std::int32_t x { 0 };
    std::int32_t y { 0 };
    std::int32_t z { 0 };
    std::int32_t w { 0 };
  };

  struct alignas(16) ResolvePackedFloat4 {
    float x { 0.0F };
    float y { 0.0F };
    float z { 0.0F };
    float w { 0.0F };
  };

  struct alignas(packing::kShaderDataFieldAlignment)
    VirtualShadowResolvePassConstants {
    ShaderVisibleIndex request_words_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_mark_flags_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex draw_bounds_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex previous_shadow_caster_bounds_srv_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex current_shadow_caster_bounds_srv_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex schedule_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex schedule_count_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex clear_args_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex draw_args_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex draw_page_ranges_uav_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex draw_page_indices_uav_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex draw_page_counter_uav_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex page_table_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_flags_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex dirty_page_flags_uav_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex physical_page_metadata_srv_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex physical_page_metadata_uav_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex physical_page_lists_srv_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex physical_page_lists_uav_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex resolve_stats_uav_index { kInvalidShaderVisibleIndex };
    glm::mat4 current_light_view_matrix { 1.0F };
    glm::mat4 previous_light_view_matrix { 1.0F };
    std::uint32_t shadow_caster_bound_count { 0U };
    std::uint32_t request_word_count { 0U };
    std::uint32_t total_page_count { 0U };
    std::uint32_t schedule_capacity { 0U };
    std::uint32_t pages_per_axis { 0U };
    std::uint32_t clip_level_count { 0U };
    std::uint32_t pages_per_level { 0U };
    std::uint32_t physical_page_capacity { 0U };
    std::uint32_t atlas_tiles_per_axis { 0U };
    std::uint32_t draw_count { 0U };
    std::uint32_t draw_page_list_capacity { 0U };
    std::uint32_t reset_page_management_state { 0U };
    std::uint32_t global_dirty_resident_contents { 0U };
    std::uint32_t phase { 0U };
    std::uint32_t target_clip_index { 0U };
    std::array<ResolvePackedInt4, 3U> clip_grid_origin_x_packed {};
    std::array<ResolvePackedInt4, 3U> clip_grid_origin_y_packed {};
    std::array<ResolvePackedFloat4, 3U> clip_origin_x_packed {};
    std::array<ResolvePackedFloat4, 3U> clip_origin_y_packed {};
    std::array<ResolvePackedFloat4, 3U> clip_page_world_packed {};
  };
  static_assert(sizeof(VirtualShadowResolvePassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);
  constexpr std::uint32_t kResolvePassConstantsStride
    = ((sizeof(VirtualShadowResolvePassConstants)
         + oxygen::packing::kConstantBufferAlignment - 1U)
        / oxygen::packing::kConstantBufferAlignment)
    * oxygen::packing::kConstantBufferAlignment;

} // namespace

VirtualShadowResolvePass::VirtualShadowResolvePass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "VirtualShadowResolvePass")
  , gfx_(gfx)
  , config_(std::move(config))
{
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
}

VirtualShadowResolvePass::~VirtualShadowResolvePass()
{
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
  active_request_words_srv_ = kInvalidShaderVisibleIndex;
  active_page_mark_flags_srv_ = kInvalidShaderVisibleIndex;
  active_draw_bounds_srv_ = kInvalidShaderVisibleIndex;
  active_request_word_count_ = 0U;
  active_dispatch_group_count_ = 0U;
  active_pages_per_axis_ = 0U;
  active_clip_level_count_ = 0U;
  active_pages_per_level_ = 0U;
  active_clip_grid_origin_x_.fill(0);
  active_clip_grid_origin_y_.fill(0);
  active_clip_origin_x_.fill(0.0F);
  active_clip_origin_y_.fill(0.0F);
  active_clip_page_world_.fill(0.0F);
  active_schedule_capacity_ = 0U;
  active_physical_page_capacity_ = 0U;
  active_draw_count_ = 0U;
  active_draw_page_index_capacity_ = 0U;
  active_current_light_view_ = glm::mat4 { 1.0F };
  active_previous_light_view_ = glm::mat4 { 1.0F };
  active_page_management_bindings_ = {};

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

  const bool has_request_dispatch = request_pass != nullptr
    && request_pass->HasActiveDispatch()
    && request_pass->GetRequestWordsSrv().IsValid()
    && request_pass->GetRequestWordsBuffer() != nullptr;
  const bool has_page_mark_flags = request_pass != nullptr
    && request_pass->GetPageMarkFlagsSrv().IsValid()
    && request_pass->GetPageMarkFlagsBuffer() != nullptr;

  const auto* metadata = shadow_manager->TryGetVirtualDirectionalMetadata(
    Context().current_view.view_id);
  const auto* page_management_bindings
    = shadow_manager->TryGetVirtualPageManagementBindings(
      Context().current_view.view_id);
  if (metadata == nullptr || page_management_bindings == nullptr
    || !page_management_bindings->page_table_uav.IsValid()
    || !page_management_bindings->page_flags_uav.IsValid()
    || !page_management_bindings->dirty_page_flags_uav.IsValid()
    || !page_management_bindings->physical_page_metadata_srv.IsValid()
    || !page_management_bindings->physical_page_metadata_uav.IsValid()
    || !page_management_bindings->physical_page_lists_srv.IsValid()
    || !page_management_bindings->physical_page_lists_uav.IsValid()
    || !page_management_bindings->resolve_stats_uav.IsValid()
    || (page_management_bindings->shadow_caster_bound_count > 0U
      && (!page_management_bindings->previous_shadow_caster_bounds_srv.IsValid()
        || !page_management_bindings->current_shadow_caster_bounds_srv
              .IsValid()))) {
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
  active_draw_count_ = static_cast<std::uint32_t>(
    prepared_frame->draw_metadata_bytes.size() / sizeof(DrawMetadata));
  EnsurePassConstantsBuffer();
  EnsureClearCountUploadBuffer();

  auto* resources = EnsureViewScheduleResources(
    Context().current_view.view_id, total_page_count, active_draw_count_);
  if (resources == nullptr || !resources->schedule_uav.IsValid()
    || !resources->count_uav.IsValid() || !resources->clear_args_uav.IsValid()
    || !resources->draw_args_uav.IsValid()
    || !resources->draw_page_ranges_uav.IsValid()
    || !resources->draw_page_indices_uav.IsValid()
    || !resources->draw_page_counter_uav.IsValid()) {
    co_return;
  }

  active_dispatch_ = true;
  active_view_id_ = Context().current_view.view_id;
  active_request_word_count_
    = has_request_dispatch ? request_pass->GetActiveRequestWordCount() : 0U;
  active_request_words_srv_ = has_request_dispatch
    ? request_pass->GetRequestWordsSrv()
    : kInvalidShaderVisibleIndex;
  active_page_mark_flags_srv_ = has_page_mark_flags
    ? request_pass->GetPageMarkFlagsSrv()
    : kInvalidShaderVisibleIndex;
  active_draw_bounds_srv_ = prepared_frame->bindless_draw_bounds_slot;
  active_pages_per_axis_ = metadata->pages_per_axis;
  active_clip_level_count_ = metadata->clip_level_count;
  active_pages_per_level_ = metadata->pages_per_axis * metadata->pages_per_axis;
  // UE-style VSM page management uses the live physical page pool capacity as
  // shader input. CPU introspection size is debug-only and may be empty/stale
  // before readback, so it must never drive resolve-time page management.
  active_physical_page_capacity_
    = std::max(1U, page_management_bindings->physical_page_capacity);
  active_current_light_view_ = metadata->light_view;
  active_previous_light_view_ = page_management_bindings->previous_light_view;
  active_page_management_bindings_ = *page_management_bindings;
  const auto active_clip_count
    = std::min(metadata->clip_level_count, kMaxSupportedClipLevels);
  for (std::uint32_t clip_index = 0U; clip_index < active_clip_count;
    ++clip_index) {
    active_clip_grid_origin_x_[clip_index] = renderer::internal::shadow_detail::
      ResolveDirectionalVirtualClipGridOriginX(*metadata, clip_index);
    active_clip_grid_origin_y_[clip_index] = renderer::internal::shadow_detail::
      ResolveDirectionalVirtualClipGridOriginY(*metadata, clip_index);
    active_clip_origin_x_[clip_index]
      = metadata->clip_metadata[clip_index].origin_page_scale.x;
    active_clip_origin_y_[clip_index]
      = metadata->clip_metadata[clip_index].origin_page_scale.y;
    active_clip_page_world_[clip_index]
      = metadata->clip_metadata[clip_index].origin_page_scale.z;
  }
  active_schedule_capacity_ = resources->entry_capacity;
  active_draw_page_index_capacity_ = resources->draw_page_index_capacity;
  active_dispatch_group_count_ = active_request_word_count_ > 0U
    ? (active_request_word_count_ + kDispatchGroupSize - 1U)
      / kDispatchGroupSize
    : 0U;

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

  const auto pack_int4
    = [](const std::array<std::int32_t, kMaxSupportedClipLevels>& values) {
        std::array<ResolvePackedInt4, 3U> packed {};
        for (std::uint32_t i = 0U; i < kMaxSupportedClipLevels; ++i) {
          auto& lane = packed[i / 4U];
          switch (i % 4U) {
          case 0U:
            lane.x = values[i];
            break;
          case 1U:
            lane.y = values[i];
            break;
          case 2U:
            lane.z = values[i];
            break;
          default:
            lane.w = values[i];
            break;
          }
        }
        return packed;
      };
  const auto pack_float4
    = [](const std::array<float, kMaxSupportedClipLevels>& values) {
        std::array<ResolvePackedFloat4, 3U> packed {};
        for (std::uint32_t i = 0U; i < kMaxSupportedClipLevels; ++i) {
          auto& lane = packed[i / 4U];
          switch (i % 4U) {
          case 0U:
            lane.x = values[i];
            break;
          case 1U:
            lane.y = values[i];
            break;
          case 2U:
            lane.z = values[i];
            break;
          default:
            lane.w = values[i];
            break;
          }
        }
        return packed;
      };

  const auto pass_constants_slot
    = static_cast<std::size_t>(Context().frame_slot.get())
    * kPassConstantsSlotsPerFrame;
  DCHECK_LT_F(pass_constants_slot, pass_constants_indices_.size());
  DCHECK_F(pass_constants_indices_[pass_constants_slot].IsValid(),
    "VirtualShadowResolvePass: invalid pass-constants slot {} for frame slot "
    "{}",
    pass_constants_slot, Context().frame_slot.get());
  SetPassConstantsIndex(pass_constants_indices_[pass_constants_slot]);

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

auto VirtualShadowResolvePass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  if (!active_dispatch_) {
    co_return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    co_return;
  }

  auto* pass_constants = static_cast<std::byte*>(pass_constants_mapped_ptr_);
  DCHECK_NOTNULL_F(pass_constants);

  std::uint32_t dispatch_slot = 0U;
  const auto base_slot = static_cast<std::uint32_t>(Context().frame_slot.get())
    * kPassConstantsSlotsPerFrame;
  const auto total_page_count
    = std::max(1U, active_pages_per_level_ * active_clip_level_count_);
  const auto clear_thread_count
    = std::max(total_page_count, active_physical_page_capacity_ * 3U);

  const auto dispatch_phase = [&](const std::uint32_t phase,
                                const std::uint32_t thread_count,
                                const std::uint32_t target_clip_index = 0U) {
    if (thread_count == 0U) {
      return;
    }
    CHECK_LT_F(dispatch_slot, kPassConstantsSlotsPerFrame,
      "VirtualShadowResolvePass: exhausted {} pass-constants slots for frame "
      "slot {}",
      kPassConstantsSlotsPerFrame, Context().frame_slot.get());

    const auto slot = base_slot + dispatch_slot;
    CHECK_LT_F(slot, pass_constants_indices_.size(),
      "VirtualShadowResolvePass: invalid pass-constants slot {}", slot);
    const auto pass_constants_index = pass_constants_indices_[slot];
    CHECK_F(pass_constants_index.IsValid(),
      "VirtualShadowResolvePass: invalid pass-constants index for slot {}",
      slot);

    const auto pack_int4
      = [](const std::array<std::int32_t, kMaxSupportedClipLevels>& values) {
          std::array<ResolvePackedInt4, 3U> packed {};
          for (std::uint32_t i = 0U; i < kMaxSupportedClipLevels; ++i) {
            auto& lane = packed[i / 4U];
            switch (i % 4U) {
            case 0U:
              lane.x = values[i];
              break;
            case 1U:
              lane.y = values[i];
              break;
            case 2U:
              lane.z = values[i];
              break;
            default:
              lane.w = values[i];
              break;
            }
          }
          return packed;
        };
    const auto pack_float4
      = [](const std::array<float, kMaxSupportedClipLevels>& values) {
          std::array<ResolvePackedFloat4, 3U> packed {};
          for (std::uint32_t i = 0U; i < kMaxSupportedClipLevels; ++i) {
            auto& lane = packed[i / 4U];
            switch (i % 4U) {
            case 0U:
              lane.x = values[i];
              break;
            case 1U:
              lane.y = values[i];
              break;
            case 2U:
              lane.z = values[i];
              break;
            default:
              lane.w = values[i];
              break;
            }
          }
          return packed;
        };

    const auto constants = VirtualShadowResolvePassConstants {
      .request_words_srv_index = active_request_word_count_ > 0U
        ? active_request_words_srv_
        : kInvalidShaderVisibleIndex,
      .page_mark_flags_srv_index = active_page_mark_flags_srv_,
      .draw_bounds_srv_index = active_draw_bounds_srv_,
      .previous_shadow_caster_bounds_srv_index
      = active_page_management_bindings_.previous_shadow_caster_bounds_srv,
      .current_shadow_caster_bounds_srv_index
      = active_page_management_bindings_.current_shadow_caster_bounds_srv,
      .schedule_uav_index
      = view_schedule_resources_.at(active_view_id_).schedule_uav,
      .schedule_count_uav_index
      = view_schedule_resources_.at(active_view_id_).count_uav,
      .clear_args_uav_index
      = view_schedule_resources_.at(active_view_id_).clear_args_uav,
      .draw_args_uav_index
      = view_schedule_resources_.at(active_view_id_).draw_args_uav,
      .draw_page_ranges_uav_index
      = view_schedule_resources_.at(active_view_id_).draw_page_ranges_uav,
      .draw_page_indices_uav_index
      = view_schedule_resources_.at(active_view_id_).draw_page_indices_uav,
      .draw_page_counter_uav_index
      = view_schedule_resources_.at(active_view_id_).draw_page_counter_uav,
      .page_table_uav_index = active_page_management_bindings_.page_table_uav,
      .page_flags_uav_index = active_page_management_bindings_.page_flags_uav,
      .dirty_page_flags_uav_index
      = active_page_management_bindings_.dirty_page_flags_uav,
      .physical_page_metadata_srv_index
      = active_page_management_bindings_.physical_page_metadata_srv,
      .physical_page_metadata_uav_index
      = active_page_management_bindings_.physical_page_metadata_uav,
      .physical_page_lists_srv_index
      = active_page_management_bindings_.physical_page_lists_srv,
      .physical_page_lists_uav_index
      = active_page_management_bindings_.physical_page_lists_uav,
      .resolve_stats_uav_index
      = active_page_management_bindings_.resolve_stats_uav,
      .current_light_view_matrix = active_current_light_view_,
      .previous_light_view_matrix = active_previous_light_view_,
      .shadow_caster_bound_count
      = active_page_management_bindings_.shadow_caster_bound_count,
      .request_word_count = active_request_word_count_,
      .total_page_count = total_page_count,
      .schedule_capacity = active_schedule_capacity_,
      .pages_per_axis = active_pages_per_axis_,
      .clip_level_count = active_clip_level_count_,
      .pages_per_level = active_pages_per_level_,
      .physical_page_capacity = active_physical_page_capacity_,
      .atlas_tiles_per_axis
      = active_page_management_bindings_.atlas_tiles_per_axis,
      .draw_count = active_draw_count_,
      .draw_page_list_capacity = active_draw_page_index_capacity_,
      .reset_page_management_state
      = active_page_management_bindings_.reset_page_management_state ? 1U : 0U,
      .global_dirty_resident_contents
      = active_page_management_bindings_.global_dirty_resident_contents ? 1U
                                                                        : 0U,
      .phase = phase,
      .target_clip_index = target_clip_index,
      .clip_grid_origin_x_packed = pack_int4(active_clip_grid_origin_x_),
      .clip_grid_origin_y_packed = pack_int4(active_clip_grid_origin_y_),
      .clip_origin_x_packed = pack_float4(active_clip_origin_x_),
      .clip_origin_y_packed = pack_float4(active_clip_origin_y_),
      .clip_page_world_packed = pack_float4(active_clip_page_world_),
    };
    auto* slot_ptr = pass_constants
      + static_cast<std::ptrdiff_t>(slot * kResolvePassConstantsStride);
    std::memcpy(slot_ptr, &constants, sizeof(constants));

    SetPassConstantsIndex(pass_constants_index);
    recorder.SetComputeRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants),
      pass_constants_index.get(), 1);
    shadow_manager->PrepareVirtualPageManagementOutputsForGpuWrite(
      active_view_id_, recorder);
    recorder.Dispatch(
      (thread_count + kDispatchGroupSize - 1U) / kDispatchGroupSize, 1U, 1U);
    ++dispatch_slot;
  };

  dispatch_phase(0U, clear_thread_count);
  dispatch_phase(1U, active_physical_page_capacity_);
  dispatch_phase(2U, active_physical_page_capacity_);
  dispatch_phase(3U, total_page_count);
  if (active_clip_level_count_ > 1U) {
    for (std::uint32_t clip_index = active_clip_level_count_ - 1U;
      clip_index-- > 0U;) {
      dispatch_phase(4U, active_pages_per_level_, clip_index);
    }
  }
  if (active_clip_level_count_ > 1U) {
    for (std::uint32_t fine_clip = 0U;
      fine_clip + 1U < active_clip_level_count_; ++fine_clip) {
      dispatch_phase(5U, active_pages_per_level_, fine_clip);
    }
  }
  dispatch_phase(6U, total_page_count);
  recorder.RequireResourceState(
    *view_schedule_resources_.at(active_view_id_).schedule_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *view_schedule_resources_.at(active_view_id_).count_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *view_schedule_resources_.at(active_view_id_).clear_args_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *view_schedule_resources_.at(active_view_id_).draw_args_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *view_schedule_resources_.at(active_view_id_).draw_page_ranges_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *view_schedule_resources_.at(active_view_id_).draw_page_indices_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *view_schedule_resources_.at(active_view_id_).draw_page_counter_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  dispatch_phase(7U, 1U);
  dispatch_phase(8U, active_draw_count_);

  shadow_manager->FinalizeVirtualPageManagementOutputs(
    active_view_id_, recorder);

  auto it = view_schedule_resources_.find(active_view_id_);
  if (it != view_schedule_resources_.end() && total_page_count > 0U) {
    recorder.RequireResourceState(
      *it->second.schedule_buffer, graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *it->second.count_buffer, graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(*it->second.draw_page_ranges_buffer,
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(*it->second.draw_page_indices_buffer,
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(*it->second.clear_args_buffer,
      graphics::ResourceStates::kIndirectArgument);
    recorder.RequireResourceState(*it->second.draw_args_buffer,
      graphics::ResourceStates::kIndirectArgument);
    recorder.FlushBarriers();

    shadow_manager->SubmitVirtualGpuRasterInputs(active_view_id_,
      renderer::VirtualShadowGpuRasterInputs {
        .schedule_buffer = it->second.schedule_buffer,
        .schedule_srv = it->second.schedule_srv,
        .schedule_count_buffer = it->second.count_buffer,
        .schedule_count_srv = it->second.count_srv,
        .draw_page_ranges_buffer = it->second.draw_page_ranges_buffer,
        .draw_page_ranges_srv = it->second.draw_page_ranges_srv,
        .draw_page_indices_buffer = it->second.draw_page_indices_buffer,
        .draw_page_indices_srv = it->second.draw_page_indices_srv,
        .clear_indirect_args_buffer = it->second.clear_args_buffer,
        .draw_indirect_args_buffer = it->second.draw_args_buffer,
        .source_frame_sequence = Context().frame_sequence,
        .draw_count = active_draw_count_,
        .pending_raster_page_count
        = active_page_management_bindings_.pending_raster_page_count,
      });
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
    && pass_constants_indices_[0].IsValid()) {
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();
  const graphics::BufferDesc desc {
    .size_bytes = kResolvePassConstantsStride * kPassConstantsSlotCount,
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

  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
  for (std::size_t slot = 0U; slot < kPassConstantsSlotCount; ++slot) {
    auto cbv_handle
      = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!cbv_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to allocate constants CBV");
    }
    pass_constants_indices_[slot] = allocator.GetShaderVisibleIndex(cbv_handle);

    graphics::BufferViewDescription cbv_desc;
    cbv_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
    cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_desc.range = {
      static_cast<std::uint32_t>(slot * kResolvePassConstantsStride),
      kResolvePassConstantsStride,
    };
    pass_constants_cbvs_[slot] = registry.RegisterView(
      *pass_constants_buffer_, std::move(cbv_handle), cbv_desc);
  }
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

auto VirtualShadowResolvePass::EnsureViewScheduleResources(const ViewId view_id,
  const std::uint32_t required_entry_capacity,
  const std::uint32_t required_draw_count) -> ViewScheduleResources*
{
  if (required_entry_capacity == 0U) {
    return nullptr;
  }

  auto [it, _] = view_schedule_resources_.try_emplace(view_id);
  auto& resources = it->second;
  const bool schedule_ready = resources.schedule_buffer
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
        "VirtualShadowResolvePass: failed to allocate schedule UAV");
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
        "VirtualShadowResolvePass: failed to allocate schedule count UAV");
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
    const auto draw_command_stride = sizeof(std::uint32_t) * 5U;

    const graphics::BufferDesc clear_args_desc {
      .size_bytes = clear_arg_stride,
      .usage
      = graphics::BufferUsage::kStorage | graphics::BufferUsage::kIndirect,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowResolvePass.ClearIndirectArgs",
    };
    resources.clear_args_buffer = gfx_->CreateBuffer(clear_args_desc);
    if (!resources.clear_args_buffer) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to create clear indirect args "
        "buffer");
    }
    registry.Register(resources.clear_args_buffer);

    auto clear_args_uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!clear_args_uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to allocate clear indirect args UAV");
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
      .size_bytes
      = static_cast<std::uint64_t>(draw_arg_capacity) * draw_command_stride,
      .usage
      = graphics::BufferUsage::kStorage | graphics::BufferUsage::kIndirect,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowResolvePass.DrawIndirectArgs",
    };
    resources.draw_args_buffer = gfx_->CreateBuffer(draw_args_desc);
    if (!resources.draw_args_buffer) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to create draw indirect args "
        "buffer");
    }
    registry.Register(resources.draw_args_buffer);

    auto draw_args_uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!draw_args_uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to allocate draw indirect args UAV");
    }
    resources.draw_args_uav
      = allocator.GetShaderVisibleIndex(draw_args_uav_handle);

    graphics::BufferViewDescription draw_args_uav_desc;
    draw_args_uav_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_UAV;
    draw_args_uav_desc.visibility
      = graphics::DescriptorVisibility::kShaderVisible;
    draw_args_uav_desc.range = { 0U,
      static_cast<std::uint64_t>(draw_arg_capacity) * draw_command_stride };
    draw_args_uav_desc.stride = draw_command_stride;
    registry.RegisterView(*resources.draw_args_buffer,
      std::move(draw_args_uav_handle), draw_args_uav_desc);

    resources.draw_arg_capacity = draw_arg_capacity;
  }

  if (!draw_page_lists_ready) {
    const auto range_stride = sizeof(std::uint32_t) * 4U;
    const auto index_stride = sizeof(std::uint32_t);

    const graphics::BufferDesc ranges_desc {
      .size_bytes
      = static_cast<std::uint64_t>(draw_arg_capacity) * range_stride,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowResolvePass.DrawPageRanges",
    };
    resources.draw_page_ranges_buffer = gfx_->CreateBuffer(ranges_desc);
    if (!resources.draw_page_ranges_buffer) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to create draw page ranges buffer");
    }
    registry.Register(resources.draw_page_ranges_buffer);

    auto draw_page_ranges_srv_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!draw_page_ranges_srv_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to allocate draw page ranges SRV");
    }
    resources.draw_page_ranges_srv
      = allocator.GetShaderVisibleIndex(draw_page_ranges_srv_handle);

    graphics::BufferViewDescription draw_page_ranges_srv_desc;
    draw_page_ranges_srv_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_SRV;
    draw_page_ranges_srv_desc.visibility
      = graphics::DescriptorVisibility::kShaderVisible;
    draw_page_ranges_srv_desc.range
      = { 0U, static_cast<std::uint64_t>(draw_arg_capacity) * range_stride };
    draw_page_ranges_srv_desc.stride = range_stride;
    registry.RegisterView(*resources.draw_page_ranges_buffer,
      std::move(draw_page_ranges_srv_handle), draw_page_ranges_srv_desc);

    auto draw_page_ranges_uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!draw_page_ranges_uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to allocate draw page ranges UAV");
    }
    resources.draw_page_ranges_uav
      = allocator.GetShaderVisibleIndex(draw_page_ranges_uav_handle);

    graphics::BufferViewDescription draw_page_ranges_uav_desc;
    draw_page_ranges_uav_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_UAV;
    draw_page_ranges_uav_desc.visibility
      = graphics::DescriptorVisibility::kShaderVisible;
    draw_page_ranges_uav_desc.range
      = { 0U, static_cast<std::uint64_t>(draw_arg_capacity) * range_stride };
    draw_page_ranges_uav_desc.stride = range_stride;
    registry.RegisterView(*resources.draw_page_ranges_buffer,
      std::move(draw_page_ranges_uav_handle), draw_page_ranges_uav_desc);

    const graphics::BufferDesc indices_desc {
      .size_bytes
      = static_cast<std::uint64_t>(draw_page_index_capacity) * index_stride,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowResolvePass.DrawPageIndices",
    };
    resources.draw_page_indices_buffer = gfx_->CreateBuffer(indices_desc);
    if (!resources.draw_page_indices_buffer) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to create draw page indices buffer");
    }
    registry.Register(resources.draw_page_indices_buffer);

    auto draw_page_indices_srv_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!draw_page_indices_srv_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to allocate draw page indices SRV");
    }
    resources.draw_page_indices_srv
      = allocator.GetShaderVisibleIndex(draw_page_indices_srv_handle);

    graphics::BufferViewDescription draw_page_indices_srv_desc;
    draw_page_indices_srv_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_SRV;
    draw_page_indices_srv_desc.visibility
      = graphics::DescriptorVisibility::kShaderVisible;
    draw_page_indices_srv_desc.range = { 0U,
      static_cast<std::uint64_t>(draw_page_index_capacity) * index_stride };
    draw_page_indices_srv_desc.stride = index_stride;
    registry.RegisterView(*resources.draw_page_indices_buffer,
      std::move(draw_page_indices_srv_handle), draw_page_indices_srv_desc);

    auto draw_page_indices_uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!draw_page_indices_uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to allocate draw page indices UAV");
    }
    resources.draw_page_indices_uav
      = allocator.GetShaderVisibleIndex(draw_page_indices_uav_handle);

    graphics::BufferViewDescription draw_page_indices_uav_desc;
    draw_page_indices_uav_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_UAV;
    draw_page_indices_uav_desc.visibility
      = graphics::DescriptorVisibility::kShaderVisible;
    draw_page_indices_uav_desc.range = { 0U,
      static_cast<std::uint64_t>(draw_page_index_capacity) * index_stride };
    draw_page_indices_uav_desc.stride = index_stride;
    registry.RegisterView(*resources.draw_page_indices_buffer,
      std::move(draw_page_indices_uav_handle), draw_page_indices_uav_desc);

    const graphics::BufferDesc counter_desc {
      .size_bytes = sizeof(std::uint32_t),
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = "VirtualShadowResolvePass.DrawPageCounter",
    };
    resources.draw_page_counter_buffer = gfx_->CreateBuffer(counter_desc);
    if (!resources.draw_page_counter_buffer) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to create draw page counter buffer");
    }
    registry.Register(resources.draw_page_counter_buffer);

    auto draw_page_counter_uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!draw_page_counter_uav_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowResolvePass: failed to allocate draw page counter UAV");
    }
    resources.draw_page_counter_uav
      = allocator.GetShaderVisibleIndex(draw_page_counter_uav_handle);

    graphics::BufferViewDescription draw_page_counter_uav_desc;
    draw_page_counter_uav_desc.view_type
      = graphics::ResourceViewType::kStructuredBuffer_UAV;
    draw_page_counter_uav_desc.visibility
      = graphics::DescriptorVisibility::kShaderVisible;
    draw_page_counter_uav_desc.range = { 0U, sizeof(std::uint32_t) };
    draw_page_counter_uav_desc.stride = sizeof(std::uint32_t);
    registry.RegisterView(*resources.draw_page_counter_buffer,
      std::move(draw_page_counter_uav_handle), draw_page_counter_uav_desc);

    resources.draw_page_index_capacity = draw_page_index_capacity;
  }

  return &resources;
}

} // namespace oxygen::engine
