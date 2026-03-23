//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Internal/ShadowBackendCommon.h>
#include <Oxygen/Renderer/Passes/Detail/VirtualShadowPassConstants.h>
#include <Oxygen/Renderer/Passes/VirtualShadowPageAllocPass.h>
#include <Oxygen/Renderer/Passes/VirtualShadowRequestPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::engine {

using namespace oxygen::renderer::internal::shadow_detail;

VirtualShadowPageAllocPass::VirtualShadowPageAllocPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(
      config ? config->debug_name : "VirtualShadowPageAllocPass")
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowPageAllocPass::~VirtualShadowPageAllocPass() = default;

auto VirtualShadowPageAllocPass::DoPrepareResources(
  graphics::CommandRecorder&) -> co::Co<>
{
  active_dispatch_ = false;
  active_view_id_ = {};
  active_dispatch_group_count_ = 0U;
  active_clip_grid_origin_x_.fill(0);
  active_clip_grid_origin_y_.fill(0);
  active_page_management_bindings_ = {};
  active_request_words_srv_ = kInvalidShaderVisibleIndex;
  active_page_mark_flags_srv_ = kInvalidShaderVisibleIndex;
  active_request_word_count_ = 0U;
  active_total_page_count_ = 0U;
  active_pages_per_axis_ = 0U;
  active_clip_level_count_ = 0U;
  active_pages_per_level_ = 0U;

  if (Context().frame_slot == frame::kInvalidSlot) {
    co_return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    co_return;
  }

  shadow_manager->ResolveVirtualCurrentFrame(Context().current_view.view_id);

  const auto* request_pass = Context().GetPass<VirtualShadowRequestPass>();
  const auto* metadata = shadow_manager->TryGetVirtualDirectionalMetadata(
    Context().current_view.view_id);
  const auto* page_management_bindings
    = shadow_manager->TryGetVirtualPageManagementBindings(
      Context().current_view.view_id);
  if (metadata == nullptr || page_management_bindings == nullptr
    || !page_management_bindings->page_table_uav.IsValid()
    || !page_management_bindings->page_flags_uav.IsValid()
    || !page_management_bindings->physical_page_metadata_uav.IsValid()
    || !page_management_bindings->physical_page_lists_srv.IsValid()
    || !page_management_bindings->physical_page_lists_uav.IsValid()
    || !page_management_bindings->resolve_stats_uav.IsValid()
    || metadata->clip_level_count == 0U || metadata->pages_per_axis == 0U) {
    co_return;
  }

  const auto total_page_count = metadata->clip_level_count
    * metadata->pages_per_axis * metadata->pages_per_axis;
  if (total_page_count == 0U) {
    co_return;
  }

  const auto clip_count
    = std::min(metadata->clip_level_count, kMaxSupportedClipLevels);
  std::array<float, kMaxSupportedClipLevels> clip_origin_x {};
  std::array<float, kMaxSupportedClipLevels> clip_origin_y {};
  std::array<float, kMaxSupportedClipLevels> clip_page_world {};
  for (std::uint32_t clip_index = 0U; clip_index < clip_count; ++clip_index) {
    active_clip_grid_origin_x_[clip_index]
      = ResolveDirectionalVirtualClipGridOriginX(*metadata, clip_index);
    active_clip_grid_origin_y_[clip_index]
      = ResolveDirectionalVirtualClipGridOriginY(*metadata, clip_index);
    clip_origin_x[clip_index]
      = metadata->clip_metadata[clip_index].origin_page_scale.x;
    clip_origin_y[clip_index]
      = metadata->clip_metadata[clip_index].origin_page_scale.y;
    clip_page_world[clip_index]
      = metadata->clip_metadata[clip_index].origin_page_scale.z;
  }

  const bool has_active_request_dispatch
    = request_pass != nullptr && request_pass->HasActiveDispatch();
  const auto request_word_count = has_active_request_dispatch
    ? request_pass->GetActiveRequestWordCount()
    : 0U;
  const auto request_words_srv = has_active_request_dispatch
    ? request_pass->GetRequestWordsSrv()
    : kInvalidShaderVisibleIndex;
  const auto page_mark_flags_srv = has_active_request_dispatch
    ? request_pass->GetPageMarkFlagsSrv()
    : kInvalidShaderVisibleIndex;

  pass_constants_.Ensure(
    *gfx_, "VirtualShadowPageAllocPass.Constants",
    detail::kVirtualShadowPassConstantsStride);
  const auto base_slot = static_cast<std::size_t>(Context().frame_slot.get())
    * kPassConstantsSlotsPerFrame;
  const auto packed_clip_origin_x = detail::PackFloat4(clip_origin_x);
  const auto packed_clip_origin_y = detail::PackFloat4(clip_origin_y);
  const auto packed_clip_page_world = detail::PackFloat4(clip_page_world);

  const auto build_constants
    = [&](const std::uint32_t phase) -> detail::VirtualShadowPassConstants {
    return detail::VirtualShadowPassConstants {
    .request_words_srv_index = request_words_srv,
    .page_mark_flags_srv_index = page_mark_flags_srv,
    .page_table_uav_index = page_management_bindings->page_table_uav,
    .page_flags_uav_index = page_management_bindings->page_flags_uav,
    .physical_page_metadata_uav_index
    = page_management_bindings->physical_page_metadata_uav,
    .physical_page_lists_srv_index
    = page_management_bindings->physical_page_lists_srv,
    .physical_page_lists_uav_index
    = page_management_bindings->physical_page_lists_uav,
    .resolve_stats_uav_index = page_management_bindings->resolve_stats_uav,
    .request_word_count = request_word_count,
    .total_page_count = total_page_count,
    .pages_per_axis = metadata->pages_per_axis,
    .clip_level_count = metadata->clip_level_count,
    .pages_per_level = metadata->pages_per_axis * metadata->pages_per_axis,
    .physical_page_capacity = page_management_bindings->physical_page_capacity,
    .atlas_tiles_per_axis = page_management_bindings->atlas_tiles_per_axis,
    .phase = phase,
    .clip_grid_origin_x_packed = detail::PackInt4(active_clip_grid_origin_x_),
    .clip_grid_origin_y_packed = detail::PackInt4(active_clip_grid_origin_y_),
    .clip_origin_x_packed = packed_clip_origin_x,
    .clip_origin_y_packed = packed_clip_origin_y,
    .clip_page_world_packed = packed_clip_page_world,
    };
  };

  for (std::uint32_t phase = 0U; phase < kPassConstantsSlotsPerFrame; ++phase) {
    const auto slot = base_slot + phase;
    const auto constants = build_constants(phase);
    auto* slot_ptr = static_cast<std::byte*>(pass_constants_.MappedPtr())
      + static_cast<std::ptrdiff_t>(
        slot * detail::kVirtualShadowPassConstantsStride);
    std::memcpy(slot_ptr, &constants, sizeof(constants));
  }
  SetPassConstantsIndex(pass_constants_.Index(base_slot));

  active_dispatch_ = true;
  active_view_id_ = Context().current_view.view_id;
  active_dispatch_group_count_
    = (total_page_count + kDispatchGroupSize - 1U) / kDispatchGroupSize;
  active_page_management_bindings_ = *page_management_bindings;
  active_request_words_srv_ = request_words_srv;
  active_page_mark_flags_srv_ = page_mark_flags_srv;
  active_request_word_count_ = request_word_count;
  active_total_page_count_ = total_page_count;
  active_pages_per_axis_ = metadata->pages_per_axis;
  active_clip_level_count_ = metadata->clip_level_count;
  active_pages_per_level_ = metadata->pages_per_axis * metadata->pages_per_axis;

  LOG_F(INFO,
    "VirtualShadowPageAllocPass: frame={} view={} request_words_srv={} "
    "page_mark_flags_srv={} request_word_count={} dispatch_groups={} "
    "total_pages={} physical_capacity={} clip_levels={} pages_per_axis={}",
    Context().frame_sequence.get(), Context().current_view.view_id.get(),
    active_request_words_srv_.get(), active_page_mark_flags_srv_.get(),
    active_request_word_count_, active_dispatch_group_count_,
    active_total_page_count_, active_page_management_bindings_.physical_page_capacity,
    active_clip_level_count_, active_pages_per_axis_);

  co_return;
}

auto VirtualShadowPageAllocPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  if (!active_dispatch_) {
    co_return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    co_return;
  }

  const auto base_slot = static_cast<std::size_t>(Context().frame_slot.get())
    * kPassConstantsSlotsPerFrame;
  SetPassConstantsIndex(pass_constants_.Index(base_slot));
  detail::DispatchVirtualPageManagementPass(
    *shadow_manager, active_view_id_, recorder, active_dispatch_group_count_);
  LOG_F(INFO,
    "VirtualShadowPageAllocPass: frame={} view={} dispatched alloc phase=0 "
    "groups={} page_table_uav={} page_flags_uav={} phys_meta_uav={} "
    "phys_lists_srv={} phys_lists_uav={} resolve_stats_uav={}",
    Context().frame_sequence.get(), active_view_id_.get(),
    active_dispatch_group_count_,
    active_page_management_bindings_.page_table_uav.get(),
    active_page_management_bindings_.page_flags_uav.get(),
    active_page_management_bindings_.physical_page_metadata_uav.get(),
    active_page_management_bindings_.physical_page_lists_srv.get(),
    active_page_management_bindings_.physical_page_lists_uav.get(),
    active_page_management_bindings_.resolve_stats_uav.get());

  SetPassConstantsIndex(pass_constants_.Index(base_slot + 1U));
  detail::DispatchVirtualPageManagementPass(
    *shadow_manager, active_view_id_, recorder, active_dispatch_group_count_);
  LOG_F(INFO,
    "VirtualShadowPageAllocPass: frame={} view={} dispatched alloc phase=1 "
    "groups={} request_words_srv={} page_mark_flags_srv={}",
    Context().frame_sequence.get(), active_view_id_.get(),
    active_dispatch_group_count_, active_request_words_srv_.get(),
    active_page_mark_flags_srv_.get());

  co_return;
}

auto VirtualShadowPageAllocPass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowPageAllocPass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowPageAllocPass: config is null");
  }
}

auto VirtualShadowPageAllocPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader({ .stage = oxygen::ShaderType::kCompute,
      .source_path = "Lighting/VirtualShadowPageAlloc.hlsl",
      .entry_point = "CS" })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowPageAlloc_PSO")
    .Build();
}

auto VirtualShadowPageAllocPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
