//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Passes/VirtualShadowPageRasterPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>

namespace oxygen::engine {

namespace {

  // Directional VSM pages apply depth bias in the raster shader so the bias is
  // normalized in the active clip basis. Keep the fixed-function rasterizer
  // bias terms disabled to avoid reintroducing clip-dependent overbias.
  constexpr float kVirtualShadowRasterDepthBias = 0.0F;
  constexpr float kVirtualShadowRasterSlopeBias = 0.0F;
  constexpr float kVirtualShadowRasterDepthBiasClamp = 0.0F;
  constexpr std::uint32_t kPassConstantsStride
    = packing::kConstantBufferAlignment;
  constexpr std::uint64_t kIndirectDrawCommandStrideBytes
    = sizeof(std::uint32_t) * 5ULL;

} // namespace

VirtualShadowPageRasterPass::VirtualShadowPageRasterPass(
  std::shared_ptr<Config> config)
  : DepthPrePass(std::move(config))
{
  constexpr std::string_view kPassName = "VirtualShadowPageRasterPass";
  SetName(kPassName);
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
  finalize_pass_constants_indices_.fill(kInvalidShaderVisibleIndex);
}

VirtualShadowPageRasterPass::~VirtualShadowPageRasterPass()
{
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
  if (finalize_pass_constants_buffer_
    && finalize_pass_constants_mapped_ptr_ != nullptr) {
    finalize_pass_constants_buffer_->UnMap();
    finalize_pass_constants_mapped_ptr_ = nullptr;
  }
  for (auto& [view_id, resources] : raster_extraction_resources_) {
    (void)view_id;
    UnMapRasterExtractionResources(resources);
  }
}

auto VirtualShadowPageRasterPass::DoPrepareResources(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  auto& depth_texture = GetDepthTexture();
  if (!recorder.IsResourceTracked(depth_texture)) {
    recorder.BeginTrackingResourceState(
      depth_texture, graphics::ResourceStates::kCommon, true);
  }

  co_await DepthPrePass::DoPrepareResources(recorder);

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (!shadow_manager) {
    co_return;
  }

  const auto* packet = shadow_manager->TryGetVirtualFramePacket(
    Context().current_view.view_id);
  const auto& virtual_depth_texture
    = shadow_manager->GetVirtualShadowDepthTexture();
  if (packet == nullptr || !packet->has_gpu_raster_inputs
    || !packet->has_directional_metadata
    || !HasLiveGpuRasterInputs(packet->gpu_raster_inputs)
    || packet->directional_metadata.page_size_texels == 0U
    || !virtual_depth_texture) {
    co_return;
  }
  if (packet->has_page_management_state
    && packet->page_management_state.reset_request_pending != 0U) {
    LOG_F(INFO,
      "VirtualShadowPageRasterPass: frame={} view={} prepare skipped because "
      "reset barrier is active source_frame={} cache_epoch={} "
      "view_generation={}",
      Context().frame_sequence.get(), Context().current_view.view_id.get(),
      packet->source_frame_sequence.get(), packet->cache_epoch,
      packet->view_generation);
    co_return;
  }

  LOG_F(INFO,
    "VirtualShadowPageRasterPass: frame={} view={} prepare packet "
    "source_frame={} cache_epoch={} view_generation={} meta_srv={} "
    "schedule_srv={} schedule_count_srv={} page_table_srv={} page_flags_srv={} "
    "pool_srv={} has_page_management={} has_gpu_raster_inputs={}",
    Context().frame_sequence.get(), Context().current_view.view_id.get(),
    packet->source_frame_sequence.get(), packet->cache_epoch,
    packet->view_generation, packet->virtual_directional_shadow_metadata_srv.get(),
    packet->gpu_raster_inputs.schedule_srv.get(),
    packet->gpu_raster_inputs.schedule_count_srv.get(),
    packet->publication.virtual_shadow_page_table_srv.get(),
    packet->publication.virtual_shadow_page_flags_srv.get(),
    packet->publication.virtual_shadow_physical_pool_srv.get(),
    packet->has_page_management_bindings, packet->has_gpu_raster_inputs);

  EnsureClearPipelineState();
  EnsureFinalizePipelineState();
  EnsurePassConstantsCapacity();
  EnsureFinalizePassConstantsCapacity();
  UploadRasterPassConstants();
  UploadFinalizePassConstants();

  if (!recorder.IsResourceTracked(*packet->gpu_raster_inputs.schedule_buffer)) {
    recorder.BeginTrackingResourceState(
      *packet->gpu_raster_inputs.schedule_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(
        *packet->gpu_raster_inputs.schedule_count_buffer)) {
    recorder.BeginTrackingResourceState(
      *packet->gpu_raster_inputs.schedule_count_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(
        *packet->gpu_raster_inputs.draw_page_ranges_buffer)) {
    recorder.BeginTrackingResourceState(
      *packet->gpu_raster_inputs.draw_page_ranges_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(
        *packet->gpu_raster_inputs.draw_page_indices_buffer)) {
    recorder.BeginTrackingResourceState(
      *packet->gpu_raster_inputs.draw_page_indices_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(
        *packet->gpu_raster_inputs.clear_indirect_args_buffer)) {
    recorder.BeginTrackingResourceState(
      *packet->gpu_raster_inputs.clear_indirect_args_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(
        *packet->gpu_raster_inputs.draw_indirect_args_buffer)) {
    recorder.BeginTrackingResourceState(
      *packet->gpu_raster_inputs.draw_indirect_args_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (packet->gpu_raster_inputs.draw_page_counter_buffer != nullptr
    && !recorder.IsResourceTracked(
      *packet->gpu_raster_inputs.draw_page_counter_buffer)) {
    recorder.BeginTrackingResourceState(
      *packet->gpu_raster_inputs.draw_page_counter_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (packet->page_flags_buffer != nullptr
    && !recorder.IsResourceTracked(*packet->page_flags_buffer)) {
    recorder.BeginTrackingResourceState(
      *packet->page_flags_buffer, graphics::ResourceStates::kCommon, true);
  }
  if (packet->physical_page_metadata_buffer != nullptr
    && !recorder.IsResourceTracked(*packet->physical_page_metadata_buffer)) {
    recorder.BeginTrackingResourceState(
      *packet->physical_page_metadata_buffer,
      graphics::ResourceStates::kCommon, true);
  }
  if (packet->resolve_stats_buffer != nullptr
    && !recorder.IsResourceTracked(*packet->resolve_stats_buffer)) {
    recorder.BeginTrackingResourceState(
      *packet->resolve_stats_buffer, graphics::ResourceStates::kCommon, true);
  }

  co_return;
}

auto VirtualShadowPageRasterPass::UsesFramebufferDepthAttachment() const -> bool
{
  return false;
}

auto VirtualShadowPageRasterPass::BuildRasterizerStateDesc(
  const graphics::CullMode cull_mode) const -> graphics::RasterizerStateDesc
{
  auto desc = DepthPrePass::BuildRasterizerStateDesc(cull_mode);
  desc.depth_bias = kVirtualShadowRasterDepthBias;
  desc.depth_bias_clamp = kVirtualShadowRasterDepthBiasClamp;
  desc.slope_scaled_depth_bias = kVirtualShadowRasterSlopeBias;
  return desc;
}

auto VirtualShadowPageRasterPass::ExtendShaderDefines(
  std::vector<graphics::ShaderDefine>& defines) const -> void
{
  defines.push_back({ "OXYGEN_VIRTUAL_SHADOW_RASTER", "1" });
}

auto VirtualShadowPageRasterPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (!shadow_manager) {
    Context().RegisterPass(this);
    co_return;
  }

  const auto* packet = shadow_manager->TryGetVirtualFramePacket(
    Context().current_view.view_id);
  const auto& virtual_depth_texture
    = shadow_manager->GetVirtualShadowDepthTexture();
  if (packet == nullptr || !packet->has_gpu_raster_inputs
    || !packet->has_directional_metadata
    || !packet->has_page_management_bindings
    || !HasLiveGpuRasterInputs(packet->gpu_raster_inputs)
    || packet->directional_metadata.page_size_texels == 0U
    || !virtual_depth_texture) {
    Context().RegisterPass(this);
    co_return;
  }
  if (packet->has_page_management_state
    && packet->page_management_state.reset_request_pending != 0U) {
    LOG_F(INFO,
      "VirtualShadowPageRasterPass: frame={} view={} execute skipped because "
      "reset barrier is active source_frame={} cache_epoch={} "
      "view_generation={}",
      Context().frame_sequence.get(), Context().current_view.view_id.get(),
      packet->source_frame_sequence.get(), packet->cache_epoch,
      packet->view_generation);
    Context().RegisterPass(this);
    co_return;
  }
  const auto* gpu_inputs = &packet->gpu_raster_inputs;
  const auto* metadata = &packet->directional_metadata;

  LOG_F(INFO,
    "VirtualShadowPageRasterPass: frame={} view={} execute packet "
    "source_frame={} cache_epoch={} view_generation={} meta_srv={} "
    "schedule_srv={} schedule_count_srv={} draw_page_ranges_srv={} "
    "draw_page_indices_srv={} page_flags_uav={} phys_meta_uav={} "
    "resolve_stats_uav={}",
    Context().frame_sequence.get(), Context().current_view.view_id.get(),
    packet->source_frame_sequence.get(), packet->cache_epoch,
    packet->view_generation, packet->virtual_directional_shadow_metadata_srv.get(),
    gpu_inputs->schedule_srv.get(), gpu_inputs->schedule_count_srv.get(),
    gpu_inputs->draw_page_ranges_srv.get(),
    gpu_inputs->draw_page_indices_srv.get(),
    packet->page_management_bindings.page_flags_uav.get(),
    packet->page_management_bindings.physical_page_metadata_uav.get(),
    packet->page_management_bindings.resolve_stats_uav.get());

  if (Context().frame_slot == frame::kInvalidSlot) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: invalid frame slot for raster execution");
  }

  const auto atlas_resolution = virtual_depth_texture->GetDescriptor().width;

  const auto psf = Context().current_view.prepared_frame;
  if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()
    || psf->partitions.empty()) {
    LOG_F(ERROR,
      "VirtualShadowPageRasterPass: skipped for view {} "
      "(prepared={} valid={} draw_bytes={} partitions={}) while having {} "
      "live GPU raster draw(s)!",
      Context().current_view.view_id.get(), psf != nullptr,
      psf ? psf->IsValid() : false, psf ? psf->draw_metadata_bytes.size() : 0U,
      psf ? psf->partitions.size() : 0U, gpu_inputs->draw_count);
    Context().RegisterPass(this);
    co_return;
  }
  auto& depth_texture = const_cast<graphics::Texture&>(GetDepthTexture());
  const auto dsv = PrepareFullAtlasDepthStencilView(depth_texture);

  recorder.SetViewport(ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(atlas_resolution),
    .height = static_cast<float>(atlas_resolution),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  });
  recorder.SetScissors(Scissors {
    .left = 0,
    .top = 0,
    .right = static_cast<std::int32_t>(atlas_resolution),
    .bottom = static_cast<std::int32_t>(atlas_resolution),
  });
  recorder.SetRenderTargets({}, dsv);

  recorder.RequireResourceState(*gpu_inputs->clear_indirect_args_buffer,
    graphics::ResourceStates::kIndirectArgument);
  recorder.RequireResourceState(*gpu_inputs->draw_indirect_args_buffer,
    graphics::ResourceStates::kIndirectArgument);
  recorder.RequireResourceState(
    *gpu_inputs->schedule_buffer, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(*gpu_inputs->schedule_count_buffer,
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(*gpu_inputs->draw_page_ranges_buffer,
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(*gpu_inputs->draw_page_indices_buffer,
    graphics::ResourceStates::kShaderResource);
  if (gpu_inputs->draw_page_counter_buffer != nullptr) {
    recorder.RequireResourceState(*gpu_inputs->draw_page_counter_buffer,
      graphics::ResourceStates::kShaderResource);
  }
  recorder.FlushBarriers();

  if (!clear_pso_.has_value()) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: clear pipeline state was not built");
  }
  recorder.SetPipelineState(*clear_pso_);
  RebindCommonRootParameters(recorder);
  recorder.ExecuteIndirect(*gpu_inputs->clear_indirect_args_buffer, 0U);

  const auto* records
    = reinterpret_cast<const DrawMetadata*>(psf->draw_metadata_bytes.data());
  std::uint64_t indirect_draw_record_count = 0U;
  std::uint32_t cpu_draw_submission_count = 0U;
  std::uint32_t skipped_invalid = 0U;
  std::uint32_t draw_errors = 0U;

  for (const auto& pr : psf->partitions) {
    if (!pr.pass_mask.IsSet(PassMaskBit::kShadowCaster)) {
      continue;
    }
    if (!pr.pass_mask.IsSet(PassMaskBit::kOpaque)
      && !pr.pass_mask.IsSet(PassMaskBit::kMasked)) {
      continue;
    }

    recorder.SetPipelineState(SelectPipelineStateForPartition(pr.pass_mask));
    RebindCommonRootParameters(recorder);
    // Do not reconstruct draw/page overlap on the CPU here. Resolve already
    // authored the exact GPU schedule, and re-deriving a "shadow draw" count
    // from CPU-side bookkeeping only recreates the authority split we are
    // removing.
    EmitIndirectResolvedPageDrawRange(recorder,
      *gpu_inputs->draw_indirect_args_buffer, records, gpu_inputs->draw_count,
      pr.begin, pr.end, indirect_draw_record_count, cpu_draw_submission_count,
      skipped_invalid, draw_errors);
  }

  recorder.RequireResourceState(
    depth_texture, graphics::ResourceStates::kShaderResource);
  auto* extraction_resources = EnsureRasterExtractionResources(
    Context().current_view.view_id, Context().frame_slot);
  if (packet->page_flags_buffer != nullptr
    && packet->physical_page_metadata_buffer != nullptr
    && packet->resolve_stats_buffer != nullptr && finalize_pso_.has_value()) {
    const auto total_page_count = metadata->clip_level_count
      * metadata->pages_per_axis * metadata->pages_per_axis;
    const auto finalize_group_count = (total_page_count + 63U) / 64U;

    recorder.RequireResourceState(
      *gpu_inputs->schedule_buffer, graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(*gpu_inputs->schedule_count_buffer,
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *packet->page_flags_buffer, graphics::ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(*packet->physical_page_metadata_buffer,
      graphics::ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(
      *packet->resolve_stats_buffer, graphics::ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();

    recorder.SetPipelineState(*finalize_pso_);
    recorder.SetComputeRootConstantBufferView(
      static_cast<std::uint32_t>(binding::RootParam::kViewConstants),
      Context().view_constants->GetGPUVirtualAddress());
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(binding::RootParam::kRootConstants), 0U, 0);
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(binding::RootParam::kRootConstants),
      finalize_pass_constants_indices_[Context().frame_slot.get()].get(), 1);
    recorder.Dispatch(finalize_group_count, 1U, 1U);
  }
  if (packet->resolve_stats_buffer != nullptr && extraction_resources != nullptr
    && extraction_resources->resolve_stats_readback_buffer != nullptr) {
    recorder.RequireResourceState(
      *packet->resolve_stats_buffer, graphics::ResourceStates::kCopySource);
    if (!recorder.IsResourceTracked(
          *extraction_resources->resolve_stats_readback_buffer)) {
      recorder.BeginTrackingResourceState(
        *extraction_resources->resolve_stats_readback_buffer,
        graphics::ResourceStates::kCopyDest, false);
    }
    recorder.RequireResourceState(
      *extraction_resources->resolve_stats_readback_buffer,
      graphics::ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyBuffer(*extraction_resources->resolve_stats_readback_buffer,
      0U, *packet->resolve_stats_buffer, 0U,
      sizeof(renderer::VirtualShadowResolveStats));
    shadow_manager->RegisterVirtualResolveStatsExtraction(
      Context().current_view.view_id,
      extraction_resources->resolve_stats_readback_buffer,
      extraction_resources->mapped_resolve_stats_ptr, Context().frame_sequence,
      Context().frame_slot);
    LOG_F(INFO,
      "VirtualShadowPageRasterPass: frame={} view={} registered resolve "
      "readback source_frame={} slot={} resolve_stats_buffer={} "
      "readback_buffer={} mapped={}",
      Context().frame_sequence.get(), Context().current_view.view_id.get(),
      Context().frame_sequence.get(), Context().frame_slot.get(),
      static_cast<const void*>(packet->resolve_stats_buffer.get()),
      static_cast<const void*>(
        extraction_resources->resolve_stats_readback_buffer.get()),
      static_cast<const void*>(extraction_resources->mapped_resolve_stats_ptr));
  }
  recorder.FlushBarriers();
  const bool rendered_page_work = indirect_draw_record_count > 0U;
  LOG_F(INFO,
    "VirtualShadowPageRasterPass: frame={} view={} completed source_frame={} "
    "gpu_draw_count={} indirect_draw_records={} cpu_draw_submissions={} "
    "skipped_invalid={} draw_errors={} rendered_page_work={}",
    Context().frame_sequence.get(), Context().current_view.view_id.get(),
    packet->source_frame_sequence.get(), gpu_inputs->draw_count,
    indirect_draw_record_count, cpu_draw_submission_count, skipped_invalid,
    draw_errors, rendered_page_work);

  if (!rendered_page_work && gpu_inputs->draw_count > 0U) {
    LOG_F(ERROR,
      "VirtualShadowPageRasterPass: view {} produced no indirect draw records "
      "(gpu_draw_count={} indirect_draw_records=0 "
      "cpu_draw_submissions={} skipped_invalid={} errors={})",
      Context().current_view.view_id.get(), gpu_inputs->draw_count,
      cpu_draw_submission_count, skipped_invalid, draw_errors);
  }
  Context().RegisterPass(this);
  co_return;
}

auto VirtualShadowPageRasterPass::EnsureRasterExtractionResources(
  const ViewId view_id, const frame::Slot frame_slot)
  -> detail::VirtualShadowRasterExtractionSlotResources*
{
  auto [it, _] = raster_extraction_resources_.try_emplace(view_id);
  auto& resources
    = it->second.slots[static_cast<std::size_t>(frame_slot.get())];
  if (resources.resolve_stats_readback_buffer != nullptr
    && resources.mapped_resolve_stats_ptr != nullptr) {
    return &resources;
  }

  const graphics::BufferDesc resolve_stats_desc {
    .size_bytes = sizeof(renderer::VirtualShadowResolveStats),
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kReadBack,
    .debug_name = "VirtualShadowPageRasterPass.ResolveStatsReadback",
  };
  resources.resolve_stats_readback_buffer
    = Context().GetGraphics().CreateBuffer(resolve_stats_desc);
  if (!resources.resolve_stats_readback_buffer) {
    throw std::runtime_error("VirtualShadowPageRasterPass: failed to create "
                             "resolve-stats readback buffer");
  }

  resources.mapped_resolve_stats_ptr
    = static_cast<renderer::VirtualShadowResolveStats*>(
      resources.resolve_stats_readback_buffer->Map(
        0U, resolve_stats_desc.size_bytes));
  if (resources.mapped_resolve_stats_ptr == nullptr) {
    throw std::runtime_error("VirtualShadowPageRasterPass: failed to map "
                             "resolve-stats readback buffer");
  }

  *resources.mapped_resolve_stats_ptr = {};
  return &resources;
}

auto VirtualShadowPageRasterPass::UnMapRasterExtractionResources(
  detail::VirtualShadowRasterExtractionResources& resources) noexcept -> void
{
  for (auto& slot_resources : resources.slots) {
    if (slot_resources.resolve_stats_readback_buffer != nullptr
      && slot_resources.mapped_resolve_stats_ptr != nullptr) {
      slot_resources.resolve_stats_readback_buffer->UnMap();
      slot_resources.mapped_resolve_stats_ptr = nullptr;
    }
  }
}

auto VirtualShadowPageRasterPass::EmitIndirectResolvedPageDrawRange(
  graphics::CommandRecorder& recorder, const graphics::Buffer& draw_args_buffer,
  const DrawMetadata* records, const std::uint32_t draw_count,
  const std::uint32_t begin, const std::uint32_t end,
  std::uint64_t& indirect_draw_record_count,
  std::uint32_t& cpu_draw_submission_count, std::uint32_t& skipped_invalid,
  std::uint32_t& draw_errors) const noexcept -> void
{
  const auto clamped_end = std::min(end, draw_count);
  for (std::uint32_t draw_index = begin; draw_index < end; ++draw_index) {
    if (draw_index >= draw_count) {
      ++draw_errors;
      LOG_F(ERROR,
        "VirtualShadowPageRasterPass '{}': partition draw_index={} exceeds "
        "prepared indirect draw count {}; draw dropped.",
        GetName(), draw_index, draw_count);
      continue;
    }

    const auto& md = records[draw_index];
    if ((md.is_indexed && md.index_count == 0U)
      || (!md.is_indexed && md.vertex_count == 0U) || md.instance_count == 0U) {
      ++skipped_invalid;
      continue;
    }

    try {
      indirect_draw_record_count
        += static_cast<std::uint64_t>(md.instance_count);
    } catch (const std::exception& ex) {
      ++draw_errors;
      LOG_F(ERROR,
        "VirtualShadowPageRasterPass '{}' draw_index={} failed: {}. "
        "Draw dropped.",
        GetName(), draw_index, ex.what());
    } catch (...) {
      ++draw_errors;
      LOG_F(ERROR,
        "VirtualShadowPageRasterPass '{}' draw_index={} failed: unknown "
        "error. Draw dropped.",
        GetName(), draw_index);
    }
  }

  if (begin >= clamped_end) {
    return;
  }

  try {
    recorder.ExecuteIndirect(draw_args_buffer,
      static_cast<std::uint64_t>(begin) * kIndirectDrawCommandStrideBytes,
      clamped_end - begin,
      graphics::CommandRecorder::IndirectCommandLayout::kDrawWithRootConstant);
    ++cpu_draw_submission_count;
  } catch (const std::exception& ex) {
    ++draw_errors;
    LOG_F(ERROR,
      "VirtualShadowPageRasterPass '{}' range=[{}, {}) failed: {}. "
      "Draw range dropped.",
      GetName(), begin, clamped_end, ex.what());
  } catch (...) {
    ++draw_errors;
    LOG_F(ERROR,
      "VirtualShadowPageRasterPass '{}' range=[{}, {}) failed: unknown "
      "error. Draw range dropped.",
      GetName(), begin, clamped_end);
  }
}

auto VirtualShadowPageRasterPass::PrepareFullAtlasDepthStencilView(
  graphics::Texture& depth_texture) const -> graphics::NativeView
{
  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  const graphics::TextureViewDescription dsv_view_desc {
    .view_type = graphics::ResourceViewType::kTexture_DSV,
    .visibility = graphics::DescriptorVisibility::kCpuOnly,
    .format = depth_texture.GetDescriptor().format,
    .dimension = oxygen::TextureType::kTexture2D,
    .sub_resources = {
      .base_mip_level = 0U,
      .num_mip_levels = 1U,
      .base_array_slice = 0U,
      .num_array_slices = 1U,
    },
    .is_read_only_dsv = false,
  };

  if (full_atlas_dsv_->IsValid()) {
    return full_atlas_dsv_;
  }
  if (const auto existing = registry.Find(depth_texture, dsv_view_desc);
    existing->IsValid()) {
    full_atlas_dsv_ = existing;
    return full_atlas_dsv_;
  }

  auto handle = graphics.GetDescriptorAllocator().Allocate(
    graphics::ResourceViewType::kTexture_DSV,
    graphics::DescriptorVisibility::kCpuOnly);
  if (!handle.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: failed to allocate full-atlas DSV");
  }

  full_atlas_dsv_
    = registry.RegisterView(depth_texture, std::move(handle), dsv_view_desc);
  if (!full_atlas_dsv_->IsValid()) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: failed to register full-atlas DSV");
  }
  return full_atlas_dsv_;
}

auto VirtualShadowPageRasterPass::EnsureClearPipelineState() -> void
{
  if (clear_pso_.has_value()) {
    return;
  }

  using graphics::CompareOp;
  using graphics::CullMode;
  using graphics::DepthStencilStateDesc;
  using graphics::FramebufferLayoutDesc;
  using graphics::PrimitiveType;
  using graphics::ShaderRequest;

  constexpr DepthStencilStateDesc depth_state {
    .depth_test_enable = true,
    .depth_write_enable = true,
    .depth_func = CompareOp::kAlways,
    .stencil_enable = false,
    .stencil_read_mask = 0xFF,
    .stencil_write_mask = 0xFF,
  };
  const FramebufferLayoutDesc framebuffer_layout {
    .color_target_formats = {},
    .depth_stencil_format = GetDepthTexture().GetDescriptor().format,
    .sample_count = GetDepthTexture().GetDescriptor().sample_count,
  };

  auto generated_bindings = BuildRootBindings();
  clear_pso_ = graphics::GraphicsPipelineDesc::Builder()
                 .SetVertexShader(ShaderRequest {
                   .stage = ShaderType::kVertex,
                   .source_path = "Depth/VirtualShadowPageClear.hlsl",
                   .entry_point = "VS",
                 })
                 .SetPixelShader(ShaderRequest {
                   .stage = ShaderType::kPixel,
                   .source_path = "Depth/VirtualShadowPageClear.hlsl",
                   .entry_point = "PS",
                 })
                 .SetPrimitiveTopology(PrimitiveType::kTriangleList)
                 .SetRasterizerState(
                   DepthPrePass::BuildRasterizerStateDesc(CullMode::kNone))
                 .SetDepthStencilState(depth_state)
                 .SetBlendState({})
                 .SetFramebufferLayout(framebuffer_layout)
                 .SetRootBindings(std::span<const graphics::RootBindingItem>(
                   generated_bindings.data(), generated_bindings.size()))
                 .SetDebugName("VirtualShadowPageRasterPass_Clear")
                 .Build();
}

auto VirtualShadowPageRasterPass::EnsureFinalizePipelineState() -> void
{
  if (finalize_pso_.has_value()) {
    return;
  }

  auto generated_bindings = BuildRootBindings();
  finalize_pso_ = graphics::ComputePipelineDesc::Builder()
                    .SetComputeShader({ .stage = ShaderType::kCompute,
                      .source_path = "Lighting/VirtualShadowPageFinalize.hlsl",
                      .entry_point = "CS" })
                    .SetRootBindings(std::span<const graphics::RootBindingItem>(
                      generated_bindings.data(), generated_bindings.size()))
                    .SetDebugName("VirtualShadowPageRasterPass_Finalize")
                    .Build();
}

auto VirtualShadowPageRasterPass::HasLiveGpuRasterInputs(
  const renderer::VirtualShadowGpuRasterInputs& gpu_inputs) const -> bool
{
  return gpu_inputs.source_frame_sequence == Context().frame_sequence
    && gpu_inputs.schedule_buffer != nullptr
    && gpu_inputs.schedule_count_buffer != nullptr
    && gpu_inputs.draw_page_ranges_buffer != nullptr
    && gpu_inputs.draw_page_indices_buffer != nullptr
    && gpu_inputs.clear_indirect_args_buffer != nullptr
    && gpu_inputs.draw_indirect_args_buffer != nullptr
    && gpu_inputs.schedule_srv.IsValid()
    && gpu_inputs.schedule_count_srv.IsValid()
    && gpu_inputs.draw_page_ranges_srv.IsValid()
    && gpu_inputs.draw_page_indices_srv.IsValid();
}

auto VirtualShadowPageRasterPass::EnsurePassConstantsCapacity() -> void
{
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr
    && pass_constants_indices_[0].IsValid()) {
    return;
  }

  if (pass_constants_buffer_ && pass_constants_mapped_ptr_ != nullptr) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
  pass_constants_buffer_.reset();
  pass_constants_cbvs_.fill({});
  pass_constants_indices_.fill(kInvalidShaderVisibleIndex);

  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  const graphics::BufferDesc desc {
    .size_bytes = kPassConstantsStride * frame::kFramesInFlight.get(),
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowPageRasterPass.Constants",
  };
  pass_constants_buffer_ = graphics.CreateBuffer(desc);
  if (!pass_constants_buffer_) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: failed to create pass constants buffer");
  }
  registry.Register(pass_constants_buffer_);
  pass_constants_mapped_ptr_ = pass_constants_buffer_->Map(0U, desc.size_bytes);
  if (pass_constants_mapped_ptr_ == nullptr) {
    pass_constants_buffer_.reset();
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: failed to map pass constants buffer");
  }

  for (std::uint32_t slot = 0U; slot < frame::kFramesInFlight.get(); ++slot) {
    auto cbv_handle
      = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!cbv_handle.IsValid()) {
      throw std::runtime_error(
        "VirtualShadowPageRasterPass: failed to allocate pass constants CBV");
    }

    pass_constants_indices_[slot] = allocator.GetShaderVisibleIndex(cbv_handle);
    graphics::BufferViewDescription cbv_desc;
    cbv_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
    cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_desc.range = { slot * kPassConstantsStride, kPassConstantsStride };
    pass_constants_cbvs_[slot] = registry.RegisterView(
      *pass_constants_buffer_, std::move(cbv_handle), cbv_desc);
  }
}

auto VirtualShadowPageRasterPass::EnsureFinalizePassConstantsCapacity() -> void
{
  if (finalize_pass_constants_buffer_
    && finalize_pass_constants_mapped_ptr_ != nullptr
    && finalize_pass_constants_indices_[0].IsValid()) {
    return;
  }

  if (finalize_pass_constants_buffer_
    && finalize_pass_constants_mapped_ptr_ != nullptr) {
    finalize_pass_constants_buffer_->UnMap();
    finalize_pass_constants_mapped_ptr_ = nullptr;
  }
  finalize_pass_constants_buffer_.reset();
  finalize_pass_constants_cbvs_.fill({});
  finalize_pass_constants_indices_.fill(kInvalidShaderVisibleIndex);

  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  const graphics::BufferDesc desc {
    .size_bytes = kPassConstantsStride * frame::kFramesInFlight.get(),
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowPageRasterPass.FinalizeConstants",
  };
  finalize_pass_constants_buffer_ = graphics.CreateBuffer(desc);
  if (!finalize_pass_constants_buffer_) {
    throw std::runtime_error("VirtualShadowPageRasterPass: failed to create "
                             "finalize pass constants buffer");
  }
  registry.Register(finalize_pass_constants_buffer_);
  finalize_pass_constants_mapped_ptr_
    = finalize_pass_constants_buffer_->Map(0U, desc.size_bytes);
  if (finalize_pass_constants_mapped_ptr_ == nullptr) {
    finalize_pass_constants_buffer_.reset();
    throw std::runtime_error("VirtualShadowPageRasterPass: failed to map "
                             "finalize pass constants buffer");
  }

  for (std::uint32_t slot = 0U; slot < frame::kFramesInFlight.get(); ++slot) {
    auto cbv_handle
      = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!cbv_handle.IsValid()) {
      throw std::runtime_error("VirtualShadowPageRasterPass: failed to "
                               "allocate finalize pass constants CBV");
    }

    finalize_pass_constants_indices_[slot]
      = allocator.GetShaderVisibleIndex(cbv_handle);
    graphics::BufferViewDescription cbv_desc;
    cbv_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
    cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_desc.range = { slot * kPassConstantsStride, kPassConstantsStride };
    finalize_pass_constants_cbvs_[slot] = registry.RegisterView(
      *finalize_pass_constants_buffer_, std::move(cbv_handle), cbv_desc);
  }
}

auto VirtualShadowPageRasterPass::UploadRasterPassConstants() -> void
{
  if (!pass_constants_buffer_ || pass_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: pass constants buffer is not initialized");
  }
  if (Context().frame_slot == frame::kInvalidSlot) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: invalid frame slot for pass constants");
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: ShadowManager is unavailable");
  }
  const auto* packet = shadow_manager->TryGetVirtualFramePacket(
    Context().current_view.view_id);
  const auto& virtual_depth_texture
    = shadow_manager->GetVirtualShadowDepthTexture();
  if (packet == nullptr || !packet->has_directional_metadata
    || !packet->has_gpu_raster_inputs
    || packet->directional_metadata.page_size_texels == 0U
    || !HasLiveGpuRasterInputs(packet->gpu_raster_inputs)
    || !virtual_depth_texture) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: live GPU raster inputs are unavailable");
  }
  const auto& metadata = packet->directional_metadata;
  const auto& gpu_inputs = packet->gpu_raster_inputs;
  const auto atlas_width = virtual_depth_texture->GetDescriptor().width;
  const auto atlas_tiles_per_axis
    = std::max(1U, atlas_width / metadata.page_size_texels);

  const auto slot = Context().frame_slot.get();
  const RasterPassConstants constants {
    .alpha_cutoff_default = 0.5F,
    .schedule_srv_index = gpu_inputs.schedule_srv.get(),
    .schedule_count_srv_index = gpu_inputs.schedule_count_srv.get(),
    .atlas_tiles_per_axis = atlas_tiles_per_axis,
    .draw_page_ranges_srv_index = gpu_inputs.draw_page_ranges_srv.get(),
    .draw_page_indices_srv_index = gpu_inputs.draw_page_indices_srv.get(),
    .virtual_directional_shadow_metadata_srv_index
    = packet->virtual_directional_shadow_metadata_srv.get(),
  };
  auto* dst = static_cast<std::byte*>(pass_constants_mapped_ptr_)
    + static_cast<std::ptrdiff_t>(slot * kPassConstantsStride);
  std::memcpy(dst, &constants, sizeof(constants));
  SetPassConstantsIndex(pass_constants_indices_[slot]);
}

auto VirtualShadowPageRasterPass::UploadFinalizePassConstants() -> void
{
  if (!finalize_pass_constants_buffer_
    || finalize_pass_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error("VirtualShadowPageRasterPass: finalize pass "
                             "constants buffer is not initialized");
  }
  if (Context().frame_slot == frame::kInvalidSlot) {
    throw std::runtime_error("VirtualShadowPageRasterPass: invalid frame slot "
                             "for finalize pass constants");
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    throw std::runtime_error("VirtualShadowPageRasterPass: ShadowManager is "
                             "unavailable for finalize constants");
  }

  const auto* packet = shadow_manager->TryGetVirtualFramePacket(
    Context().current_view.view_id);
  if (packet == nullptr || !packet->has_directional_metadata
    || !packet->has_gpu_raster_inputs
    || !packet->has_page_management_bindings
    || !HasLiveGpuRasterInputs(packet->gpu_raster_inputs)
    || !packet->page_management_bindings.page_flags_uav.IsValid()
    || !packet->page_management_bindings.physical_page_metadata_uav.IsValid()
    || !packet->page_management_bindings.resolve_stats_uav.IsValid()) {
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: finalize GPU inputs are unavailable");
  }
  const auto& metadata = packet->directional_metadata;
  const auto& gpu_inputs = packet->gpu_raster_inputs;
  const auto& page_management_bindings = packet->page_management_bindings;

  const auto total_page_count = metadata.clip_level_count
    * metadata.pages_per_axis * metadata.pages_per_axis;
  const auto slot = Context().frame_slot.get();
  const RasterFinalizePassConstants constants {
    .schedule_srv_index = gpu_inputs.schedule_srv.get(),
    .schedule_count_srv_index = gpu_inputs.schedule_count_srv.get(),
    .page_flags_uav_index = page_management_bindings.page_flags_uav.get(),
    .physical_page_metadata_uav_index
    = page_management_bindings.physical_page_metadata_uav.get(),
    .resolve_stats_uav_index
    = page_management_bindings.resolve_stats_uav.get(),
    .atlas_tiles_per_axis = page_management_bindings.atlas_tiles_per_axis,
    .schedule_capacity = total_page_count,
  };

  auto* dst = static_cast<std::byte*>(finalize_pass_constants_mapped_ptr_)
    + static_cast<std::ptrdiff_t>(slot * kPassConstantsStride);
  std::memcpy(dst, &constants, sizeof(constants));
}

} // namespace oxygen::engine
