// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h>

#include <cstddef>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>

using oxygen::Graphics;
using oxygen::TextureType;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::VsmShadowRasterizerPass;
using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::GraphicsPipelineDesc;
using oxygen::graphics::NativeView;
using oxygen::graphics::ResourceStates;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;

namespace {

auto FindSliceIndex(const oxygen::renderer::vsm::VsmPhysicalPoolSnapshot& pool,
  const VsmPhysicalPoolSliceRole role) noexcept -> std::optional<std::uint32_t>
{
  for (std::uint32_t i = 0; i < pool.slice_roles.size(); ++i) {
    if (pool.slice_roles[i] == role) {
      return i;
    }
  }
  return std::nullopt;
}

auto HasRasterDrawMetadata(const PreparedSceneFrame& prepared_frame) noexcept
  -> bool
{
  return !prepared_frame.draw_metadata_bytes.empty()
    && !prepared_frame.partitions.empty();
}

} // namespace

namespace oxygen::engine {

struct VsmShadowRasterizerPass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;
  std::optional<VsmShadowRasterizerPassInput> input {};
  std::vector<renderer::vsm::VsmShadowRasterPageJob> prepared_pages {};
  bool resources_ready { false };
  bool job_view_constants_uploaded { false };
  std::optional<std::uint32_t> dynamic_slice_index {};

  std::shared_ptr<Buffer> shadow_view_constants_buffer_ {};
  void* shadow_view_constants_mapped_ptr_ { nullptr };
  std::uint32_t shadow_view_constants_capacity_ { 0U };
  std::vector<ViewConstants::GpuData> job_view_constants_upload_ {};

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl()
  {
    if (shadow_view_constants_buffer_ && shadow_view_constants_mapped_ptr_) {
      shadow_view_constants_buffer_->UnMap();
      shadow_view_constants_mapped_ptr_ = nullptr;
    }
    shadow_view_constants_buffer_.reset();
    shadow_view_constants_capacity_ = 0U;
  }

  [[nodiscard]] auto HasUsableShadowTexture() const noexcept -> bool
  {
    return input.has_value() && input->physical_pool.is_available
      && input->physical_pool.shadow_texture != nullptr;
  }

  auto EnsureShadowViewConstantsCapacity(
    const RenderContext& context, const std::uint32_t required_jobs) -> void
  {
    if (required_jobs == 0U
      || required_jobs <= shadow_view_constants_capacity_) {
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
      .debug_name = config != nullptr && !config->debug_name.empty()
        ? config->debug_name + ".ViewConstants"
        : "VsmShadowRasterizerPass.ViewConstants",
    };

    shadow_view_constants_buffer_ = context.GetGraphics().CreateBuffer(desc);
    CHECK_NOTNULL_F(shadow_view_constants_buffer_.get(),
      "VsmShadowRasterizerPass: failed to create page view constants buffer");

    shadow_view_constants_buffer_->SetName(desc.debug_name);
    shadow_view_constants_mapped_ptr_
      = shadow_view_constants_buffer_->Map(0U, desc.size_bytes);
    CHECK_NOTNULL_F(shadow_view_constants_mapped_ptr_,
      "VsmShadowRasterizerPass: failed to map page view constants buffer");
  }

  auto UploadPreparedJobViewConstants(const RenderContext& context) -> void
  {
    CHECK_F(input.has_value(),
      "VsmShadowRasterizerPass: job view constants upload requires bound "
      "input");
    CHECK_F(input->base_view_constants.has_value(),
      "VsmShadowRasterizerPass: missing base view constants for upload");
    CHECK_F(input->base_view_constants->view_frame_bindings_bslot.IsValid(),
      "VsmShadowRasterizerPass: invalid bindless view-frame bindings slot for "
      "upload");
    CHECK_F(context.frame_slot != frame::kInvalidSlot,
      "VsmShadowRasterizerPass: invalid frame slot during view constants "
      "upload");
    CHECK_F(prepared_pages.size() <= shadow_view_constants_capacity_,
      "VsmShadowRasterizerPass: uploaded job count exceeds constants capacity");
    CHECK_NOTNULL_F(shadow_view_constants_mapped_ptr_,
      "VsmShadowRasterizerPass: mapped view constants buffer is required");

    job_view_constants_upload_.resize(prepared_pages.size());
    const auto base_snapshot = *input->base_view_constants;
    for (std::size_t i = 0; i < prepared_pages.size(); ++i) {
      auto snapshot = base_snapshot;
      snapshot.view_matrix
        = prepared_pages[i].projection.projection.view_matrix;
      snapshot.projection_matrix
        = prepared_pages[i].projection.projection.projection_matrix;
      snapshot.camera_position = {
        prepared_pages[i].projection.projection.view_origin_ws_pad.x,
        prepared_pages[i].projection.projection.view_origin_ws_pad.y,
        prepared_pages[i].projection.projection.view_origin_ws_pad.z,
      };
      job_view_constants_upload_[i] = snapshot;
    }

    const auto base_index = static_cast<std::uint64_t>(context.frame_slot.get())
      * static_cast<std::uint64_t>(shadow_view_constants_capacity_);
    auto* dst = static_cast<std::byte*>(shadow_view_constants_mapped_ptr_)
      + base_index * sizeof(ViewConstants::GpuData);
    std::memcpy(dst, job_view_constants_upload_.data(),
      job_view_constants_upload_.size() * sizeof(ViewConstants::GpuData));
    job_view_constants_uploaded = true;
  }

  auto BindJobViewConstants(CommandRecorder& recorder,
    const RenderContext& context, const std::uint32_t job_index) const -> void
  {
    CHECK_NOTNULL_F(shadow_view_constants_buffer_.get(),
      "VsmShadowRasterizerPass: page view constants buffer is unavailable");
    CHECK_F(context.frame_slot != frame::kInvalidSlot,
      "VsmShadowRasterizerPass: invalid frame slot during view constants "
      "binding");
    CHECK_F(job_index < shadow_view_constants_capacity_,
      "VsmShadowRasterizerPass: job index exceeds view constants capacity");

    const auto slot_offset
      = static_cast<std::uint64_t>(context.frame_slot.get())
        * static_cast<std::uint64_t>(shadow_view_constants_capacity_)
      + job_index;
    const auto byte_offset = slot_offset * sizeof(ViewConstants::GpuData);
    recorder.SetGraphicsRootConstantBufferView(
      static_cast<std::uint32_t>(binding::RootParam::kViewConstants),
      shadow_view_constants_buffer_->GetGPUVirtualAddress() + byte_offset);
  }

  auto PrepareJobDepthStencilView(const RenderContext& context,
    graphics::Texture& depth_texture,
    const renderer::vsm::VsmShadowRasterPageJob& job) const -> NativeView
  {
    auto& graphics = context.GetGraphics();
    auto& registry = graphics.GetResourceRegistry();
    auto& allocator = graphics.GetDescriptorAllocator();
    CHECK_F(registry.Contains(depth_texture),
      "VsmShadowRasterizerPass: physical pool shadow texture must be "
      "registered before page DSV creation");

    const graphics::TextureViewDescription dsv_view_desc {
      .view_type = graphics::ResourceViewType::kTexture_DSV,
      .visibility = graphics::DescriptorVisibility::kCpuOnly,
      .format = depth_texture.GetDescriptor().format,
      .dimension = depth_texture.GetDescriptor().texture_type,
      .sub_resources = {
        .base_mip_level = 0U,
        .num_mip_levels = 1U,
        .base_array_slice = job.physical_coord.slice,
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
    CHECK_F(dsv_desc_handle.IsValid(),
      "VsmShadowRasterizerPass: failed to allocate page DSV descriptor");

    const auto dsv = registry.RegisterView(
      depth_texture, std::move(dsv_desc_handle), dsv_view_desc);
    CHECK_F(dsv->IsValid(),
      "VsmShadowRasterizerPass: failed to register page DSV view");
    return dsv;
  }
};

VsmShadowRasterizerPass::VsmShadowRasterizerPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .debug_name = config ? config->debug_name : "VsmShadowRasterizerPass",
    }))
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
  DCHECK_NOTNULL_F(gfx.get());
  DCHECK_NOTNULL_F(impl_->config.get());
}

VsmShadowRasterizerPass::~VsmShadowRasterizerPass() = default;

auto VsmShadowRasterizerPass::SetInput(VsmShadowRasterizerPassInput input)
  -> void
{
  impl_->input = std::move(input);
}

auto VsmShadowRasterizerPass::ResetInput() noexcept -> void
{
  impl_->input.reset();
  impl_->prepared_pages.clear();
  impl_->resources_ready = false;
  impl_->job_view_constants_uploaded = false;
  impl_->dynamic_slice_index.reset();
}

auto VsmShadowRasterizerPass::GetPreparedPageCount() const noexcept
  -> std::size_t
{
  return impl_->prepared_pages.size();
}

auto VsmShadowRasterizerPass::GetPreparedPages() const noexcept
  -> std::span<const renderer::vsm::VsmShadowRasterPageJob>
{
  return { impl_->prepared_pages.data(), impl_->prepared_pages.size() };
}

auto VsmShadowRasterizerPass::GetDepthTexture() const
  -> const graphics::Texture&
{
  if (impl_->input.has_value()
    && impl_->input->physical_pool.shadow_texture != nullptr) {
    return *impl_->input->physical_pool.shadow_texture;
  }

  throw std::runtime_error(
    "VsmShadowRasterizerPass requires a physical pool shadow texture");
}

auto VsmShadowRasterizerPass::ValidateConfig() -> void
{
  if (impl_->gfx == nullptr) {
    throw std::runtime_error("VsmShadowRasterizerPass requires Graphics");
  }
  if (!impl_->config) {
    throw std::runtime_error("VsmShadowRasterizerPass requires Config");
  }
}

auto VsmShadowRasterizerPass::OnPrepareResources(CommandRecorder& recorder)
  -> void
{
  impl_->resources_ready = impl_->HasUsableShadowTexture();
  if (!impl_->resources_ready) {
    return;
  }

  GraphicsRenderPass::OnPrepareResources(recorder);
}

auto VsmShadowRasterizerPass::OnExecute(CommandRecorder& recorder) -> void
{
  if (!impl_->resources_ready) {
    return;
  }

  GraphicsRenderPass::OnExecute(recorder);
}

auto VsmShadowRasterizerPass::NeedRebuildPipelineState() const -> bool
{
  if (!impl_->HasUsableShadowTexture()) {
    return false;
  }

  return DepthPrePass::NeedRebuildPipelineState();
}

auto VsmShadowRasterizerPass::CreatePipelineStateDesc() -> GraphicsPipelineDesc
{
  if (!impl_->HasUsableShadowTexture()) {
    throw std::runtime_error(
      "VsmShadowRasterizerPass requires a shadow texture before pipeline "
      "creation");
  }

  return DepthPrePass::CreatePipelineStateDesc();
}

auto VsmShadowRasterizerPass::UsesFramebufferDepthAttachment() const -> bool
{
  return false;
}

auto VsmShadowRasterizerPass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  impl_->prepared_pages.clear();
  impl_->dynamic_slice_index.reset();
  impl_->job_view_constants_uploaded = false;

  if (!impl_->input.has_value()) {
    DLOG_F(
      2, "VsmShadowRasterizerPass: skipped prepare because no input is bound");
    co_return;
  }

  if (!impl_->input->physical_pool.is_available
    || impl_->input->physical_pool.shadow_texture == nullptr) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: skipped prepare because the bound physical "
      "shadow pool is unavailable");
    co_return;
  }

  impl_->dynamic_slice_index = FindSliceIndex(
    impl_->input->physical_pool, VsmPhysicalPoolSliceRole::kDynamicDepth);
  if (!impl_->dynamic_slice_index.has_value()) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: skipped prepare because the physical pool has "
      "no dynamic depth slice");
    co_return;
  }

  const auto& shadow_texture = GetDepthTexture();
  if (!recorder.IsResourceTracked(shadow_texture)) {
    recorder.BeginTrackingResourceState(
      shadow_texture, ResourceStates::kCommon, true);
  }

  co_await DepthPrePass::DoPrepareResources(recorder);

  impl_->prepared_pages
    = renderer::vsm::BuildShadowRasterPageJobs(impl_->input->frame,
      impl_->input->physical_pool, impl_->input->projections);

  if (!impl_->prepared_pages.empty()
    && impl_->input->base_view_constants.has_value()
    && impl_->input->base_view_constants->view_frame_bindings_bslot.IsValid()) {
    impl_->EnsureShadowViewConstantsCapacity(
      Context(), static_cast<std::uint32_t>(impl_->prepared_pages.size()));
    impl_->UploadPreparedJobViewConstants(Context());
  }

  DLOG_F(2,
    "VsmShadowRasterizerPass: prepare map_count={} prepared_pages={} "
    "dynamic_slice={} base_view_constants={} view_frame_slot_valid={}",
    impl_->input->projections.size(), impl_->prepared_pages.size(),
    *impl_->dynamic_slice_index, impl_->input->base_view_constants.has_value(),
    impl_->input->base_view_constants.has_value()
      && impl_->input->base_view_constants->view_frame_bindings_bslot
        .IsValid());

  co_return;
}

auto VsmShadowRasterizerPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  if (!impl_->resources_ready || !impl_->input.has_value()
    || impl_->input->physical_pool.shadow_texture == nullptr) {
    DLOG_F(2,
      "VsmShadowRasterizerPass: skipped execute because resources are not "
      "ready");
    Context().RegisterPass(this);
    co_return;
  }

  auto shadow_texture = std::const_pointer_cast<graphics::Texture>(
    impl_->input->physical_pool.shadow_texture);
  CHECK_NOTNULL_F(
    shadow_texture.get(), "VsmShadowRasterizerPass requires a shadow texture");

  if (impl_->prepared_pages.empty()) {
    DLOG_F(2, "VsmShadowRasterizerPass: no prepared page jobs");
    recorder.RequireResourceState(
      *shadow_texture, ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    Context().RegisterPass(this);
    co_return;
  }

  const auto psf = Context().current_view.prepared_frame;
  if (psf == nullptr || !psf->IsValid() || !HasRasterDrawMetadata(*psf)) {
    DLOG_F(2,
      "VsmShadowRasterizerPass: no shadow-caster draw metadata was available "
      "for {} prepared pages",
      impl_->prepared_pages.size());
    recorder.RequireResourceState(
      *shadow_texture, ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    Context().RegisterPass(this);
    co_return;
  }

  if (!impl_->input->base_view_constants.has_value()) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: skipping {} prepared pages because "
      "base_view_constants were not provided",
      impl_->prepared_pages.size());
    recorder.RequireResourceState(
      *shadow_texture, ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    Context().RegisterPass(this);
    co_return;
  }

  if (!impl_->input->base_view_constants->view_frame_bindings_bslot.IsValid()) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: skipping {} prepared pages because the "
      "base view constants do not carry a valid bindless view-frame bindings "
      "slot",
      impl_->prepared_pages.size());
    recorder.RequireResourceState(
      *shadow_texture, ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    Context().RegisterPass(this);
    co_return;
  }

  if (!impl_->job_view_constants_uploaded) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: skipping {} prepared pages because page-local "
      "view constants were not uploaded",
      impl_->prepared_pages.size());
    recorder.RequireResourceState(
      *shadow_texture, ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    Context().RegisterPass(this);
    co_return;
  }

  CHECK_F(impl_->dynamic_slice_index.has_value(),
    "VsmShadowRasterizerPass: dynamic slice must be resolved before execute");

  const auto* records
    = reinterpret_cast<const DrawMetadata*>(psf->draw_metadata_bytes.data());
  std::uint32_t eligible_pages = 0U;
  std::uint32_t deferred_static_pages = 0U;
  std::uint32_t deferred_non_dynamic_pages = 0U;
  std::uint32_t emitted_count = 0U;
  std::uint32_t skipped_invalid = 0U;
  std::uint32_t draw_errors = 0U;

  for (std::uint32_t job_index = 0U; job_index < impl_->prepared_pages.size();
    ++job_index) {
    const auto& job = impl_->prepared_pages[job_index];
    if (job.static_only) {
      ++deferred_static_pages;
      continue;
    }
    if (job.physical_coord.slice != *impl_->dynamic_slice_index) {
      ++deferred_non_dynamic_pages;
      continue;
    }

    ++eligible_pages;
    const auto dsv
      = impl_->PrepareJobDepthStencilView(Context(), *shadow_texture, job);
    recorder.SetRenderTargets({}, dsv);
    recorder.SetViewport(job.viewport);
    recorder.SetScissors(job.scissors);

    for (const auto& partition : psf->partitions) {
      if (!partition.pass_mask.IsSet(PassMaskBit::kShadowCaster)) {
        continue;
      }
      if (!partition.pass_mask.IsSet(PassMaskBit::kOpaque)
        && !partition.pass_mask.IsSet(PassMaskBit::kMasked)) {
        continue;
      }

      recorder.SetPipelineState(
        SelectPipelineStateForPartition(partition.pass_mask));
      RebindCommonRootParameters(recorder);
      impl_->BindJobViewConstants(recorder, Context(), job_index);
      EmitDrawRange(recorder, records, partition.begin, partition.end,
        emitted_count, skipped_invalid, draw_errors);
    }
  }

  recorder.RequireResourceState(
    *shadow_texture, ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  if (deferred_static_pages > 0U || deferred_non_dynamic_pages > 0U) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: deferred page jobs outside F1 scope "
      "(static_only={} non_dynamic_slice={})",
      deferred_static_pages, deferred_non_dynamic_pages);
  }

  DLOG_F(2,
    "VsmShadowRasterizerPass: execute prepared_pages={} eligible_pages={} "
    "emitted={} skipped_invalid={} errors={}",
    impl_->prepared_pages.size(), eligible_pages, emitted_count,
    skipped_invalid, draw_errors);

  if (eligible_pages > 0U && emitted_count == 0U) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: prepared {} dynamic page jobs but emitted no "
      "shadow-caster draws (skipped_invalid={} errors={})",
      eligible_pages, skipped_invalid, draw_errors);
  }

  Context().RegisterPass(this);
  co_return;
}

} // namespace oxygen::engine
