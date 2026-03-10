//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
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

  constexpr float kVirtualShadowRasterDepthBias = 1200.0F;
  // Virtual pages are small and heavily PCF-filtered, so leaving raster slope
  // bias at zero prints regular self-shadow bands on broad receiver planes.
  // Keep the hardware slope term enabled, but clamp it tightly so this does
  // not explode into peter-panning on steep geometry.
  constexpr float kVirtualShadowRasterSlopeBias = 2.0F;
  constexpr float kVirtualShadowRasterDepthBiasClamp = 0.0025F;

} // namespace

VirtualShadowPageRasterPass::VirtualShadowPageRasterPass(
  std::shared_ptr<Config> config)
  : DepthPrePass(std::move(config))
{
}

VirtualShadowPageRasterPass::~VirtualShadowPageRasterPass()
{
  if (shadow_view_constants_buffer_ && shadow_view_constants_mapped_ptr_) {
    shadow_view_constants_buffer_->UnMap();
    shadow_view_constants_mapped_ptr_ = nullptr;
  }
  shadow_view_constants_buffer_.reset();
  shadow_view_constants_capacity_ = 0U;
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

  const auto* render_plan
    = shadow_manager->TryGetVirtualRenderPlan(Context().current_view.view_id);
  if (render_plan == nullptr || render_plan->jobs.empty()) {
    co_return;
  }

  EnsureShadowViewConstantsCapacity(
    static_cast<std::uint32_t>(render_plan->jobs.size()));
  UploadJobViewConstants(render_plan->jobs);

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

auto VirtualShadowPageRasterPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (!shadow_manager) {
    LOG_F(INFO,
      "VirtualShadowPageRasterPass: skipped for view {} (no ShadowManager)",
      Context().current_view.view_id.get());
    Context().RegisterPass(this);
    co_return;
  }

  const auto* render_plan
    = shadow_manager->TryGetVirtualRenderPlan(Context().current_view.view_id);
  if (render_plan == nullptr || render_plan->jobs.empty()
    || render_plan->depth_texture == nullptr) {
    auto& log_state = view_log_states_[Context().current_view.view_id.get()];
    if (render_plan != nullptr && render_plan->depth_texture != nullptr
      && log_state.saw_live_plan_jobs) {
      log_state.saw_live_plan_jobs = false;
      LOG_F(INFO,
        "VirtualShadowPageRasterPass: view {} has no pending virtual raster "
        "jobs anymore",
        Context().current_view.view_id.get());
    }
    Context().RegisterPass(this);
    co_return;
  }

  auto& log_state = view_log_states_[Context().current_view.view_id.get()];
  if (!log_state.saw_live_plan_jobs) {
    log_state.saw_live_plan_jobs = true;
    LOG_F(INFO,
      "VirtualShadowPageRasterPass: view {} has {} pending virtual raster "
      "job(s)",
      Context().current_view.view_id.get(), render_plan->jobs.size());
  }

  const auto psf = Context().current_view.prepared_frame;
  if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()
    || psf->partitions.empty()) {
    LOG_F(INFO,
      "VirtualShadowPageRasterPass: skipped for view {} "
      "(prepared={} valid={} draw_bytes={} partitions={})",
      Context().current_view.view_id.get(), psf != nullptr,
      psf ? psf->IsValid() : false, psf ? psf->draw_metadata_bytes.size() : 0U,
      psf ? psf->partitions.size() : 0U);
    Context().RegisterPass(this);
    co_return;
  }
  if (!log_state.saw_live_prepared_frame) {
    log_state.saw_live_prepared_frame = true;
    LOG_F(INFO,
      "VirtualShadowPageRasterPass: view {} has a live prepared frame "
      "(draw_bytes={} partitions={})",
      Context().current_view.view_id.get(), psf->draw_metadata_bytes.size(),
      psf->partitions.size());
  }

  auto& depth_texture = const_cast<graphics::Texture&>(GetDepthTexture());
  const auto dsv = PrepareFullAtlasDepthStencilView(depth_texture);

  recorder.SetRenderTargets({}, dsv);

  const auto* records
    = reinterpret_cast<const DrawMetadata*>(psf->draw_metadata_bytes.data());
  std::uint32_t emitted_count = 0U;
  std::uint32_t skipped_invalid = 0U;
  std::uint32_t draw_errors = 0U;

  for (std::uint32_t job_index = 0U; job_index < render_plan->jobs.size();
    ++job_index) {
    const auto& job = render_plan->jobs[job_index];
    SetJobViewportAndScissors(recorder, *render_plan, job);
    recorder.SetRenderTargets({}, dsv);
    const auto left = static_cast<std::int32_t>(
      job.atlas_tile_x * render_plan->page_size_texels);
    const auto top = static_cast<std::int32_t>(
      job.atlas_tile_y * render_plan->page_size_texels);
    const auto size = static_cast<std::int32_t>(render_plan->page_size_texels);
    const Scissors clear_rect {
      .left = left,
      .top = top,
      .right = left + size,
      .bottom = top + size,
    };
    recorder.ClearDepthStencilView(depth_texture, dsv,
      graphics::ClearFlags::kDepth, 1.0F, 0, { &clear_rect, 1 });

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
      BindJobViewConstants(recorder, job_index);
      EmitDrawRange(recorder, records, pr.begin, pr.end, emitted_count,
        skipped_invalid, draw_errors);
    }
  }

  recorder.RequireResourceState(
    depth_texture, graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  if (emitted_count == 0U) {
    if (!log_state.saw_zero_draw_live_frame) {
      log_state.saw_zero_draw_live_frame = true;
      log_state.saw_nonzero_draw_live_frame = false;
    }
    LOG_F(WARNING,
      "VirtualShadowPageRasterPass: view {} produced no shadow-caster draws "
      "(jobs={} skipped_invalid={} errors={})",
      Context().current_view.view_id.get(), render_plan->jobs.size(),
      skipped_invalid, draw_errors);
  } else {
    if (!log_state.saw_nonzero_draw_live_frame) {
      log_state.saw_nonzero_draw_live_frame = true;
      log_state.saw_zero_draw_live_frame = false;
      LOG_F(INFO,
        "VirtualShadowPageRasterPass: view {} emitted {} shadow-caster draw(s) "
        "for {} virtual page job(s)",
        Context().current_view.view_id.get(), emitted_count,
        render_plan->jobs.size());
    }
    shadow_manager->MarkVirtualRenderPlanExecuted(
      Context().current_view.view_id);
  }

  Context().RegisterPass(this);
  co_return;
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

auto VirtualShadowPageRasterPass::EnsureShadowViewConstantsCapacity(
  const std::uint32_t required_jobs) -> void
{
  if (required_jobs == 0U || required_jobs <= shadow_view_constants_capacity_) {
    return;
  }

  if (shadow_view_constants_buffer_ && shadow_view_constants_mapped_ptr_) {
    shadow_view_constants_buffer_->UnMap();
    shadow_view_constants_mapped_ptr_ = nullptr;
  }
  shadow_view_constants_buffer_.reset();

  shadow_view_constants_capacity_ = required_jobs;
  const auto total_bytes
    = static_cast<std::uint64_t>(sizeof(ViewConstants::GpuData))
    * static_cast<std::uint64_t>(frame::kFramesInFlight.get())
    * static_cast<std::uint64_t>(shadow_view_constants_capacity_);

  const graphics::BufferDesc desc {
    .size_bytes = total_bytes,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "VirtualShadowPageRasterPass.ViewConstants",
  };

  shadow_view_constants_buffer_ = Context().GetGraphics().CreateBuffer(desc);
  if (!shadow_view_constants_buffer_) {
    shadow_view_constants_capacity_ = 0U;
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: failed to create shadow view constants "
      "buffer");
  }

  shadow_view_constants_buffer_->SetName(desc.debug_name);
  shadow_view_constants_mapped_ptr_
    = shadow_view_constants_buffer_->Map(0U, desc.size_bytes);
  if (shadow_view_constants_mapped_ptr_ == nullptr) {
    shadow_view_constants_buffer_.reset();
    shadow_view_constants_capacity_ = 0U;
    throw std::runtime_error(
      "VirtualShadowPageRasterPass: failed to map shadow view constants "
      "buffer");
  }
}

auto VirtualShadowPageRasterPass::UploadJobViewConstants(
  const std::span<const renderer::VirtualShadowRasterJob> jobs) -> void
{
  if (jobs.empty()) {
    return;
  }
  if (!shadow_view_constants_buffer_
    || shadow_view_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error("VirtualShadowPageRasterPass: shadow view "
                             "constants buffer is not initialized");
  }
  if (Context().frame_slot == frame::kInvalidSlot) {
    throw std::runtime_error("VirtualShadowPageRasterPass: invalid frame slot "
                             "for shadow view constants upload");
  }

  job_view_constants_upload_.resize(jobs.size());
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    job_view_constants_upload_[i] = jobs[i].view_constants;
  }

  const auto base_index = static_cast<std::uint64_t>(Context().frame_slot.get())
    * static_cast<std::uint64_t>(shadow_view_constants_capacity_);
  auto* dst = static_cast<std::byte*>(shadow_view_constants_mapped_ptr_)
    + base_index * sizeof(ViewConstants::GpuData);
  std::memcpy(dst, job_view_constants_upload_.data(),
    job_view_constants_upload_.size() * sizeof(ViewConstants::GpuData));
}

auto VirtualShadowPageRasterPass::BindJobViewConstants(
  graphics::CommandRecorder& recorder, const std::uint32_t job_index) const
  -> void
{
  if (!shadow_view_constants_buffer_
    || Context().frame_slot == frame::kInvalidSlot) {
    throw std::runtime_error("VirtualShadowPageRasterPass: shadow view "
                             "constants buffer is not available");
  }
  if (job_index >= shadow_view_constants_capacity_) {
    throw std::out_of_range("VirtualShadowPageRasterPass: job index exceeds "
                            "shadow view constants capacity");
  }

  const auto slot_offset
    = static_cast<std::uint64_t>(Context().frame_slot.get())
      * static_cast<std::uint64_t>(shadow_view_constants_capacity_)
    + job_index;
  const auto byte_offset = slot_offset * sizeof(ViewConstants::GpuData);
  recorder.SetGraphicsRootConstantBufferView(
    static_cast<std::uint32_t>(binding::RootParam::kViewConstants),
    shadow_view_constants_buffer_->GetGPUVirtualAddress() + byte_offset);
}

auto VirtualShadowPageRasterPass::SetJobViewportAndScissors(
  graphics::CommandRecorder& recorder,
  const renderer::VirtualShadowRenderPlan& render_plan,
  const renderer::VirtualShadowRasterJob& job) const -> void
{
  const float left
    = static_cast<float>(job.atlas_tile_x) * render_plan.page_size_texels;
  const float top
    = static_cast<float>(job.atlas_tile_y) * render_plan.page_size_texels;
  const float size = static_cast<float>(render_plan.page_size_texels);

  recorder.SetViewport(ViewPort {
    .top_left_x = left,
    .top_left_y = top,
    .width = size,
    .height = size,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  });
  recorder.SetScissors(Scissors {
    .left = static_cast<std::int32_t>(left),
    .top = static_cast<std::int32_t>(top),
    .right = static_cast<std::int32_t>(left + size),
    .bottom = static_cast<std::int32_t>(top + size),
  });
}

} // namespace oxygen::engine
