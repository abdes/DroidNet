//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/GpuEventScope.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowRasterPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>

namespace oxygen::engine {

namespace {

  struct ShadowRasterDepthBias {
    float depth;
    float slope;
    float clamp;
  };

  // Conventional shadow maps now rasterize into reversed-Z depth, so the
  // raster depth bias must remain negative to push stored caster depths away
  // from the light under GREATER_EQUAL comparison. In D3D, a positive clamp
  // only caps positive bias results, so keeping it positive here intentionally
  // leaves the current always-negative bias path unclamped.
  constexpr ShadowRasterDepthBias kDirectionalShadowRasterBias {
    .depth = -1500.0F,
    .slope = -2.0F,
    .clamp = 0.0025F,
  };

} // namespace

ConventionalShadowRasterPass::ConventionalShadowRasterPass(
  std::shared_ptr<Config> config)
  : DepthPrePass(std::move(config))
{
  SetName("ConventionalShadowRasterPass");
}

ConventionalShadowRasterPass::~ConventionalShadowRasterPass()
{
  ReleaseShadowViewConstantsBuffer();
  shadow_view_constants_reclaimer_ = nullptr;
}

auto ConventionalShadowRasterPass::DoPrepareResources(
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

  const auto* raster_plan
    = shadow_manager->TryGetRasterRenderPlan(Context().current_view.view_id);
  if (raster_plan == nullptr || raster_plan->jobs.empty()) {
    co_return;
  }

  const auto job_count = raster_plan->jobs.size();
  ValidateRasterPlan(raster_plan->jobs);
  CacheDeferredReclaimer();
  EnsureShadowViewConstantsCapacity(static_cast<std::uint32_t>(job_count));
  UploadJobViewConstants(raster_plan->jobs);

  co_return;
}

auto ConventionalShadowRasterPass::UsesFramebufferDepthAttachment() const
  -> bool
{
  return false;
}

auto ConventionalShadowRasterPass::PublishesCanonicalDepthOutput() const -> bool
{
  return false;
}

auto ConventionalShadowRasterPass::BuildRasterizerStateDesc(
  const graphics::CullMode cull_mode) const -> graphics::RasterizerStateDesc
{
  auto desc = DepthPrePass::BuildRasterizerStateDesc(cull_mode);
  desc.depth_bias = kDirectionalShadowRasterBias.depth;
  desc.depth_bias_clamp = kDirectionalShadowRasterBias.clamp;
  desc.slope_scaled_depth_bias = kDirectionalShadowRasterBias.slope;
  return desc;
}

auto ConventionalShadowRasterPass::DoExecute(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (!shadow_manager) {
    LOG_F(INFO,
      "ConventionalShadowRasterPass: skipped for view {} (no ShadowManager)",
      Context().current_view.view_id.get());
    Context().RegisterPass(this);
    co_return;
  }

  const auto* raster_plan
    = shadow_manager->TryGetRasterRenderPlan(Context().current_view.view_id);
  if (raster_plan == nullptr || raster_plan->jobs.empty()
    || raster_plan->depth_texture == nullptr) {
    LOG_F(INFO,
      "ConventionalShadowRasterPass: skipped for view {} "
      "(published={} jobs={} depth_texture={})",
      Context().current_view.view_id.get(), raster_plan != nullptr,
      raster_plan ? raster_plan->jobs.size() : 0U,
      raster_plan && raster_plan->depth_texture != nullptr);
    Context().RegisterPass(this);
    co_return;
  }

  const auto psf = Context().current_view.prepared_frame;
  if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()
    || psf->partitions.empty()) {
    LOG_F(INFO,
      "ConventionalShadowRasterPass: skipped for view {} "
      "(prepared={} valid={} draw_bytes={} partitions={})",
      Context().current_view.view_id.get(), psf != nullptr,
      psf ? psf->IsValid() : false, psf ? psf->draw_metadata_bytes.size() : 0U,
      psf ? psf->partitions.size() : 0U);
    Context().RegisterPass(this);
    co_return;
  }

  ValidateRasterPlan(raster_plan->jobs);
  const auto job_count = raster_plan->jobs.size();
  auto& depth_texture = GetDepthTextureMutable();
  SetupViewPortAndScissors(recorder);
  const graphics::GpuEventScopeOptions scope_options {};
  graphics::GpuEventScope shadow_depth_work_scope(
    recorder, "ConventionalShadowRasterPass.ShadowDepthWork", scope_options);

  const auto* records
    = reinterpret_cast<const DrawMetadata*>(psf->draw_metadata_bytes.data());
  std::uint32_t emitted_count = 0U;
  std::uint32_t skipped_invalid = 0U;
  std::uint32_t draw_errors = 0U;
  // Track the last public descriptor we bound so partition traversal only
  // reissues SetPipelineState when the selected variant actually changes.
  auto current_pso = LastBuiltPsoDesc();

  for (std::uint32_t job_index = 0U; job_index < job_count; ++job_index) {
    const auto& job = raster_plan->jobs[job_index];
    const auto dsv = PrepareJobDepthStencilView(depth_texture, job);
    const auto job_scope_name
      = fmt::format("ConventionalShadowRasterPass.Job[{}].Slice[{}]", job_index,
        job.target_array_slice);
    graphics::GpuEventScope job_scope(recorder, job_scope_name, scope_options);

    recorder.SetRenderTargets({}, dsv);
    ClearDepthStencilView(recorder, dsv);
    BindJobViewConstants(recorder, job_index);

    for (const auto& pr : psf->partitions) {
      if (!pr.pass_mask.IsSet(PassMaskBit::kShadowCaster)) {
        continue;
      }
      if (!pr.pass_mask.IsSet(PassMaskBit::kOpaque)
        && !pr.pass_mask.IsSet(PassMaskBit::kMasked)) {
        continue;
      }

      const auto& pso_desc = SelectPipelineStateForPartition(pr.pass_mask);
      if (!current_pso.has_value() || *current_pso != pso_desc) {
        recorder.SetPipelineState(pso_desc);
        // CommandRecorder::SetPipelineState rebinds the PSO's root signature.
        // Restore the shared pass bindings, then override view constants with
        // the current shadow job CBV again.
        RebindCommonRootParameters(recorder);
        BindJobViewConstants(recorder, job_index);
        current_pso = pso_desc;
      }
      EmitDrawRange(recorder, records, pr.begin, pr.end, emitted_count,
        skipped_invalid, draw_errors);
    }
  }

  recorder.RequireResourceState(
    depth_texture, graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  if (emitted_count == 0U) {
    LOG_F(WARNING,
      "ConventionalShadowRasterPass: view {} produced no shadow-caster draws "
      "(jobs={} skipped_invalid={} errors={})",
      Context().current_view.view_id.get(), job_count, skipped_invalid,
      draw_errors);
  }

  Context().RegisterPass(this);
  co_return;
}

auto ConventionalShadowRasterPass::ValidateConfig() -> void
{
  SyncConfiguredDepthTextureFromShadowManager();
  DepthPrePass::ValidateConfig();

  const auto& depth_desc = GetDepthTexture().GetDescriptor();
  if (depth_desc.texture_type != TextureType::kTexture2DArray) {
    throw std::invalid_argument(
      "ConventionalShadowRasterPass: depth texture must be Texture2DArray");
  }
  if (depth_desc.array_size == 0U) {
    throw std::invalid_argument(
      "ConventionalShadowRasterPass: depth texture array must expose at least "
      "one slice");
  }
}

auto ConventionalShadowRasterPass::SyncConfiguredDepthTextureFromShadowManager()
  -> void
{
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (!shadow_manager) {
    return;
  }

  const auto& manager_depth_texture
    = shadow_manager->GetConventionalShadowDepthTexture();
  if (!manager_depth_texture) {
    return;
  }

  if (const auto* configured = TryGetConfiguredDepthTexture();
    configured != manager_depth_texture.get()) {
    SetConfiguredDepthTexture(manager_depth_texture);
  }

  if (const auto* raster_plan
    = shadow_manager->TryGetRasterRenderPlan(Context().current_view.view_id);
    raster_plan != nullptr && raster_plan->depth_texture != nullptr
    && raster_plan->depth_texture.get() != manager_depth_texture.get()) {
    throw std::runtime_error(
      "ConventionalShadowRasterPass: raster shadow plan depth texture does "
      "not match the ShadowManager conventional shadow texture");
  }
}

auto ConventionalShadowRasterPass::EnsureShadowViewConstantsCapacity(
  const std::uint32_t required_jobs) -> void
{
  if (required_jobs == 0U || required_jobs <= shadow_view_constants_capacity_) {
    return;
  }

  ReleaseShadowViewConstantsBuffer();
  shadow_view_constants_capacity_ = required_jobs;
  const auto total_bytes
    = static_cast<std::uint64_t>(sizeof(ViewConstants::GpuData))
    * static_cast<std::uint64_t>(frame::kFramesInFlight.get())
    * static_cast<std::uint64_t>(shadow_view_constants_capacity_);

  const graphics::BufferDesc desc {
    .size_bytes = total_bytes,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "ConventionalShadowRasterPass.ViewConstants",
  };

  shadow_view_constants_buffer_ = Context().GetGraphics().CreateBuffer(desc);
  if (!shadow_view_constants_buffer_) {
    shadow_view_constants_capacity_ = 0U;
    throw std::runtime_error(
      "ConventionalShadowRasterPass: failed to create shadow view constants "
      "buffer");
  }

  shadow_view_constants_buffer_->SetName(desc.debug_name);
  shadow_view_constants_mapped_ptr_
    = shadow_view_constants_buffer_->Map(0U, desc.size_bytes);
  if (shadow_view_constants_mapped_ptr_ == nullptr) {
    shadow_view_constants_buffer_.reset();
    shadow_view_constants_capacity_ = 0U;
    throw std::runtime_error(
      "ConventionalShadowRasterPass: failed to map shadow view constants "
      "buffer");
  }
}

auto ConventionalShadowRasterPass::CacheDeferredReclaimer() -> void
{
  shadow_view_constants_reclaimer_
    = observer_ptr(&Context().GetGraphics().GetDeferredReclaimer());
}

auto ConventionalShadowRasterPass::ReleaseShadowViewConstantsBuffer() noexcept
  -> void
{
  shadow_view_constants_capacity_ = 0U;

  if (shadow_view_constants_buffer_ && shadow_view_constants_mapped_ptr_) {
    shadow_view_constants_buffer_->UnMap();
  }
  shadow_view_constants_mapped_ptr_ = nullptr;

  if (!shadow_view_constants_buffer_) {
    return;
  }

  if (shadow_view_constants_reclaimer_) {
    graphics::DeferredObjectRelease(
      shadow_view_constants_buffer_, *shadow_view_constants_reclaimer_);
    return;
  }

  shadow_view_constants_buffer_.reset();
}

auto ConventionalShadowRasterPass::ValidateRasterPlan(
  const std::span<const renderer::RasterShadowJob> jobs) const -> void
{
  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (!shadow_manager) {
    throw std::runtime_error(
      "ConventionalShadowRasterPass: raster plan validation requires an "
      "active ShadowManager");
  }

  const auto* raster_plan
    = shadow_manager->TryGetRasterRenderPlan(Context().current_view.view_id);
  if (raster_plan == nullptr || raster_plan->depth_texture == nullptr) {
    throw std::runtime_error(
      "ConventionalShadowRasterPass: raster plan validation requires a "
      "published depth texture");
  }

  if (raster_plan->depth_texture.get() != &GetDepthTexture()) {
    throw std::runtime_error(
      "ConventionalShadowRasterPass: configured depth texture diverged from "
      "the published raster shadow plan texture");
  }

  const auto& depth_desc = GetDepthTexture().GetDescriptor();
  for (const auto& job : jobs) {
    if (job.target_kind
      != renderer::RasterShadowTargetKind::kTexture2DArraySlice) {
      throw std::runtime_error(
        "ConventionalShadowRasterPass: unsupported raster shadow target kind");
    }
    if (job.target_array_slice >= depth_desc.array_size) {
      throw std::out_of_range(
        "ConventionalShadowRasterPass: raster shadow job targets an array "
        "slice outside the configured depth texture");
    }
  }
}

auto ConventionalShadowRasterPass::UploadJobViewConstants(
  const std::span<const renderer::RasterShadowJob> jobs) -> void
{
  if (jobs.empty()) {
    return;
  }
  if (!shadow_view_constants_buffer_
    || shadow_view_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error("ConventionalShadowRasterPass: shadow view "
                             "constants buffer is not initialized");
  }
  if (Context().frame_slot == frame::kInvalidSlot) {
    throw std::runtime_error("ConventionalShadowRasterPass: invalid frame slot "
                             "for shadow view constants upload");
  }

  const auto base_index = static_cast<std::uint64_t>(Context().frame_slot.get())
    * static_cast<std::uint64_t>(shadow_view_constants_capacity_);
  auto* dst = static_cast<std::byte*>(shadow_view_constants_mapped_ptr_)
    + base_index * sizeof(ViewConstants::GpuData);
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    std::memcpy(dst + i * sizeof(ViewConstants::GpuData),
      &jobs[i].view_constants, sizeof(ViewConstants::GpuData));
  }
}

auto ConventionalShadowRasterPass::BindJobViewConstants(
  graphics::CommandRecorder& recorder, const std::uint32_t job_index) const
  -> void
{
  static_assert(sizeof(ViewConstants::GpuData) % 256U == 0U,
    "ConventionalShadowRasterPass view constants must remain 256-byte "
    "aligned for root CBV binding");
  if (!shadow_view_constants_buffer_
    || Context().frame_slot == frame::kInvalidSlot) {
    throw std::runtime_error("ConventionalShadowRasterPass: shadow view "
                             "constants buffer is not available");
  }
  if (job_index >= shadow_view_constants_capacity_) {
    throw std::out_of_range("ConventionalShadowRasterPass: job index exceeds "
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

auto ConventionalShadowRasterPass::PrepareJobDepthStencilView(
  graphics::Texture& depth_texture, const renderer::RasterShadowJob& job) const
  -> graphics::NativeView
{
  if (job.target_kind
    != renderer::RasterShadowTargetKind::kTexture2DArraySlice) {
    throw std::runtime_error(
      "ConventionalShadowRasterPass: unsupported raster shadow target kind");
  }

  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  const graphics::TextureViewDescription dsv_view_desc {
    .view_type = graphics::ResourceViewType::kTexture_DSV,
    .visibility = graphics::DescriptorVisibility::kCpuOnly,
    .format = depth_texture.GetDescriptor().format,
    .dimension = oxygen::TextureType::kTexture2DArray,
    .sub_resources = {
      .base_mip_level = 0U,
      .num_mip_levels = 1U,
      .base_array_slice = job.target_array_slice,
      .num_array_slices = 1U,
    },
    .is_read_only_dsv = false,
  };

  if (const auto dsv = registry.Find(depth_texture, dsv_view_desc);
    dsv->IsValid()) {
    return dsv;
  }

  auto dsv_desc_handle
    = allocator.Allocate(graphics::ResourceViewType::kTexture_DSV,
      graphics::DescriptorVisibility::kCpuOnly);
  if (!dsv_desc_handle.IsValid()) {
    throw std::runtime_error(
      "ConventionalShadowRasterPass: failed to allocate shadow DSV descriptor");
  }

  const auto dsv = registry.RegisterView(
    depth_texture, std::move(dsv_desc_handle), dsv_view_desc);
  if (!dsv->IsValid()) {
    throw std::runtime_error(
      "ConventionalShadowRasterPass: failed to register shadow DSV view");
  }
  return dsv;
}

} // namespace oxygen::engine
