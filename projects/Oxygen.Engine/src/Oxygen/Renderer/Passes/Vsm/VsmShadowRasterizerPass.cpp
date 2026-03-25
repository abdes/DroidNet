// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Passes/ScreenHzbBuildPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>

using oxygen::Graphics;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::ScreenHzbBuildPass;
using oxygen::engine::VsmShadowRasterizerPass;
using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::ComputePipelineDesc;
using oxygen::graphics::GraphicsPipelineDesc;
using oxygen::graphics::NativeView;
using oxygen::graphics::ResourceStates;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;

namespace {

constexpr std::uint32_t kInstanceCullingThreadGroupSize = 64U;
constexpr std::uint32_t kInstanceCullingConstantsStride
  = oxygen::packing::kConstantBufferAlignment;

struct alignas(oxygen::packing::kShaderDataFieldAlignment)
  InstanceCullingPassConstants {
  oxygen::ShaderVisibleIndex page_job_buffer_index {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex draw_metadata_index {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex draw_bounds_index {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex previous_frame_hzb_index {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex indirect_commands_uav_index {
    oxygen::kInvalidShaderVisibleIndex
  };
  oxygen::ShaderVisibleIndex command_counts_uav_index {
    oxygen::kInvalidShaderVisibleIndex
  };
  std::uint32_t prepared_page_count { 0U };
  std::uint32_t partition_begin { 0U };
  std::uint32_t partition_count { 0U };
  std::uint32_t draw_bounds_count { 0U };
  std::uint32_t max_commands_per_page { 0U };
  std::uint32_t previous_frame_hzb_width { 0U };
  std::uint32_t previous_frame_hzb_height { 0U };
  std::uint32_t previous_frame_hzb_mip_count { 0U };
  std::uint32_t previous_frame_hzb_available { 0U };
  std::uint32_t _pad0 { 0U };
};
static_assert(sizeof(InstanceCullingPassConstants) == 64U);

struct ActivePartitionRange {
  oxygen::engine::PassMask pass_mask {};
  std::uint32_t begin { 0U };
  std::uint32_t count { 0U };
};

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

auto IsShadowRasterPartition(
  const PreparedSceneFrame::PartitionRange& partition) noexcept -> bool
{
  return partition.end > partition.begin
    && partition.pass_mask.IsSet(oxygen::engine::PassMaskBit::kShadowCaster)
    && (partition.pass_mask.IsSet(oxygen::engine::PassMaskBit::kOpaque)
      || partition.pass_mask.IsSet(oxygen::engine::PassMaskBit::kMasked));
}

auto BuildPageCropMatrix(
  const oxygen::renderer::vsm::VsmShadowRasterPageJob& job) -> glm::mat4
{
  CHECK_F(job.projection.pages_x != 0U && job.projection.pages_y != 0U,
    "VsmShadowRasterizerPass: raster page crop requires non-zero page-grid "
    "dimensions");

  const auto scale_x = static_cast<float>(job.projection.pages_x);
  const auto scale_y = static_cast<float>(job.projection.pages_y);
  const auto offset_x = static_cast<float>(job.projection.pages_x)
    - 2.0F * static_cast<float>(job.virtual_page.page_x) - 1.0F;
  const auto offset_y = 2.0F * static_cast<float>(job.virtual_page.page_y)
    + 1.0F - static_cast<float>(job.projection.pages_y);

  return glm::mat4(glm::vec4 { scale_x, 0.0F, 0.0F, 0.0F },
    glm::vec4 { 0.0F, scale_y, 0.0F, 0.0F },
    glm::vec4 { 0.0F, 0.0F, 1.0F, 0.0F },
    glm::vec4 { offset_x, offset_y, 0.0F, 1.0F });
}

auto BuildPageProjectionMatrix(
  const oxygen::renderer::vsm::VsmShadowRasterPageJob& job) -> glm::mat4
{
  return BuildPageCropMatrix(job) * job.projection.projection.projection_matrix;
}

auto BuildPageViewProjectionMatrix(
  const oxygen::renderer::vsm::VsmShadowRasterPageJob& job) -> glm::mat4
{
  return BuildPageProjectionMatrix(job) * job.projection.projection.view_matrix;
}

auto MakeShaderRasterJob(
  const oxygen::renderer::vsm::VsmShadowRasterPageJob& job)
  -> oxygen::renderer::vsm::VsmShaderRasterPageJob
{
  return oxygen::renderer::vsm::VsmShaderRasterPageJob {
    .view_projection_matrix = BuildPageViewProjectionMatrix(job),
    .page_table_index = job.page_table_index,
    .map_id = job.map_id,
    .virtual_page_x = job.virtual_page.page_x,
    .virtual_page_y = job.virtual_page.page_y,
    .virtual_page_level = job.virtual_page.level,
  };
}

auto MakePartitionCommandOffset(const std::uint32_t page_index,
  const std::uint32_t max_commands_per_page) noexcept -> std::uint64_t
{
  return static_cast<std::uint64_t>(page_index)
    * static_cast<std::uint64_t>(max_commands_per_page)
    * sizeof(oxygen::renderer::vsm::VsmShaderIndirectDrawCommand);
}

} // namespace

namespace oxygen::engine {

struct VsmShadowRasterizerPass::Impl {
  struct PartitionState {
    PassMask pass_mask {};
    std::uint32_t begin { 0U };
    std::uint32_t count { 0U };
    std::uint32_t page_count { 0U };
    std::uint32_t max_commands_per_page { 0U };
    std::shared_ptr<Buffer> command_buffer {};
    std::shared_ptr<Buffer> count_buffer {};
    std::shared_ptr<Buffer> count_clear_buffer {};
    void* count_clear_mapped_ptr { nullptr };
    oxygen::ShaderVisibleIndex command_uav {
      oxygen::kInvalidShaderVisibleIndex
    };
    oxygen::ShaderVisibleIndex count_uav { oxygen::kInvalidShaderVisibleIndex };
  };

  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;
  std::optional<VsmShadowRasterizerPassInput> input {};

  std::vector<renderer::vsm::VsmShadowRasterPageJob> prepared_pages {};
  std::vector<std::uint32_t> eligible_page_job_indices {};
  std::vector<renderer::vsm::VsmShaderRasterPageJob> page_job_upload_ {};
  std::vector<ViewConstants::GpuData> job_view_constants_upload_ {};
  std::vector<ActivePartitionRange> active_partitions {};
  std::vector<PartitionState> partition_states {};
  std::vector<IndirectPartitionInspection> inspection_partitions {};

  bool resources_ready { false };
  bool job_view_constants_uploaded { false };
  bool instance_culling_ready { false };
  std::optional<std::uint32_t> dynamic_slice_index {};
  ScreenHzbBuildPass::ViewOutput previous_frame_hzb {};
  std::uint32_t deferred_static_pages { 0U };
  std::uint32_t deferred_non_dynamic_pages { 0U };

  std::shared_ptr<Buffer> shadow_view_constants_buffer_ {};
  void* shadow_view_constants_mapped_ptr_ { nullptr };
  std::uint32_t shadow_view_constants_capacity_ { 0U };

  std::shared_ptr<Buffer> page_job_buffer_ {};
  std::shared_ptr<Buffer> page_job_upload_buffer_ {};
  void* page_job_upload_ptr_ { nullptr };
  std::uint32_t page_job_capacity_ { 0U };
  oxygen::ShaderVisibleIndex page_job_srv_ {
    oxygen::kInvalidShaderVisibleIndex
  };

  std::shared_ptr<Buffer> instance_culling_constants_buffer_ {};
  void* instance_culling_constants_mapped_ptr_ { nullptr };
  std::vector<oxygen::ShaderVisibleIndex>
    instance_culling_constants_indices_ {};
  std::uint32_t instance_culling_constants_capacity_ { 0U };

  std::optional<ComputePipelineDesc> instance_culling_pso_ {};

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl();

  [[nodiscard]] auto HasUsableShadowTexture() const noexcept -> bool;
  auto ResetExecutionState() -> void;
  auto EnsureInstanceCullingPipelineState() -> void;
  auto EnsureShadowViewConstantsCapacity(
    const RenderContext& context, std::uint32_t required_jobs) -> void;
  auto UploadPreparedJobViewConstants(const RenderContext& context) -> void;
  auto BindJobViewConstants(CommandRecorder& recorder,
    const RenderContext& context, std::uint32_t local_job_index) const -> void;
  auto PrepareJobDepthStencilView(const RenderContext& context,
    graphics::Texture& depth_texture,
    const renderer::vsm::VsmShadowRasterPageJob& job) const -> NativeView;
  auto EnsureShaderVisibleIndex(Buffer& buffer,
    const graphics::BufferViewDescription& view_desc,
    const char* debug_label) const -> oxygen::ShaderVisibleIndex;
  auto EnsurePageJobBuffer(std::uint32_t required_jobs) -> void;
  auto EnsureInstanceCullingConstantsBuffer(std::uint32_t slot_count) -> void;
  auto WriteInstanceCullingConstants(std::uint32_t slot,
    const InstanceCullingPassConstants& constants) const -> void;
  auto EnsurePartitionStateCount(std::size_t required_count) -> void;
  auto EnsurePartitionResources(PartitionState& partition,
    std::uint32_t required_pages, std::uint32_t partition_count,
    std::uint32_t slot_index) -> void;
  auto CollectActiveShadowPartitions(const PreparedSceneFrame& prepared_frame)
    -> void;
  auto BuildEligiblePageIndices() -> void;
  auto PrepareInstanceCulling(CommandRecorder& recorder,
    const RenderContext& context, const PreparedSceneFrame& prepared_frame)
    -> void;
  auto ReleasePartitionResources(PartitionState& partition) const -> void;
  static auto ReleaseUploadBuffer(
    std::shared_ptr<Buffer>& buffer, void*& mapped_ptr) -> void;
};

VsmShadowRasterizerPass::Impl::~Impl()
{
  ReleaseUploadBuffer(
    shadow_view_constants_buffer_, shadow_view_constants_mapped_ptr_);
  ReleaseUploadBuffer(page_job_upload_buffer_, page_job_upload_ptr_);
  ReleaseUploadBuffer(
    instance_culling_constants_buffer_, instance_culling_constants_mapped_ptr_);

  if (gfx == nullptr) {
    return;
  }

  auto& registry = gfx->GetResourceRegistry();
  if (page_job_buffer_ && registry.Contains(*page_job_buffer_)) {
    registry.UnRegisterResource(*page_job_buffer_);
  }
  if (page_job_upload_buffer_ && registry.Contains(*page_job_upload_buffer_)) {
    registry.UnRegisterResource(*page_job_upload_buffer_);
  }
  if (instance_culling_constants_buffer_
    && registry.Contains(*instance_culling_constants_buffer_)) {
    registry.UnRegisterResource(*instance_culling_constants_buffer_);
  }

  for (auto& partition : partition_states) {
    ReleasePartitionResources(partition);
  }
}

auto VsmShadowRasterizerPass::Impl::ReleaseUploadBuffer(
  std::shared_ptr<Buffer>& buffer, void*& mapped_ptr) -> void
{
  if (buffer && mapped_ptr != nullptr) {
    buffer->UnMap();
    mapped_ptr = nullptr;
  }
  buffer.reset();
}

auto VsmShadowRasterizerPass::Impl::ReleasePartitionResources(
  PartitionState& partition) const -> void
{
  if (partition.count_clear_buffer && partition.count_clear_mapped_ptr) {
    partition.count_clear_buffer->UnMap();
    partition.count_clear_mapped_ptr = nullptr;
  }

  if (gfx != nullptr) {
    auto& registry = gfx->GetResourceRegistry();
    if (partition.command_buffer
      && registry.Contains(*partition.command_buffer)) {
      registry.UnRegisterResource(*partition.command_buffer);
    }
    if (partition.count_buffer && registry.Contains(*partition.count_buffer)) {
      registry.UnRegisterResource(*partition.count_buffer);
    }
    if (partition.count_clear_buffer
      && registry.Contains(*partition.count_clear_buffer)) {
      registry.UnRegisterResource(*partition.count_clear_buffer);
    }
  }

  partition.command_buffer.reset();
  partition.count_buffer.reset();
  partition.count_clear_buffer.reset();
  partition.command_uav = oxygen::kInvalidShaderVisibleIndex;
  partition.count_uav = oxygen::kInvalidShaderVisibleIndex;
  partition.page_count = 0U;
  partition.max_commands_per_page = 0U;
}

auto VsmShadowRasterizerPass::Impl::HasUsableShadowTexture() const noexcept
  -> bool
{
  return input.has_value() && input->physical_pool.is_available
    && input->physical_pool.shadow_texture != nullptr;
}

auto VsmShadowRasterizerPass::Impl::ResetExecutionState() -> void
{
  eligible_page_job_indices.clear();
  page_job_upload_.clear();
  active_partitions.clear();
  inspection_partitions.clear();
  deferred_static_pages = 0U;
  deferred_non_dynamic_pages = 0U;
  previous_frame_hzb = {};
  instance_culling_ready = false;
}

auto VsmShadowRasterizerPass::Impl::EnsureInstanceCullingPipelineState() -> void
{
  if (instance_culling_pso_.has_value()) {
    return;
  }

  auto root_bindings = RenderPass::BuildRootBindings();
  instance_culling_pso_
    = ComputePipelineDesc::Builder()
        .SetComputeShader(graphics::ShaderRequest {
          .stage = oxygen::ShaderType::kCompute,
          .source_path = "Renderer/Vsm/VsmInstanceCulling.hlsl",
          .entry_point = "CS",
        })
        .SetRootBindings(std::span<const graphics::RootBindingItem>(
          root_bindings.data(), root_bindings.size()))
        .SetDebugName("VsmInstanceCulling_PSO")
        .Build();
}

auto VsmShadowRasterizerPass::Impl::EnsureShadowViewConstantsCapacity(
  const RenderContext& context, const std::uint32_t required_jobs) -> void
{
  if (required_jobs == 0U || required_jobs <= shadow_view_constants_capacity_) {
    return;
  }

  ReleaseUploadBuffer(
    shadow_view_constants_buffer_, shadow_view_constants_mapped_ptr_);

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

auto VsmShadowRasterizerPass::Impl::UploadPreparedJobViewConstants(
  const RenderContext& context) -> void
{
  CHECK_F(input.has_value(),
    "VsmShadowRasterizerPass: job view constants upload requires bound input");
  CHECK_F(input->base_view_constants.has_value(),
    "VsmShadowRasterizerPass: missing base view constants for upload");
  CHECK_F(input->base_view_constants->view_frame_bindings_bslot.IsValid(),
    "VsmShadowRasterizerPass: invalid bindless view-frame bindings slot for "
    "upload");
  CHECK_F(context.frame_slot != frame::kInvalidSlot,
    "VsmShadowRasterizerPass: invalid frame slot during view constants upload");
  CHECK_F(eligible_page_job_indices.size() <= shadow_view_constants_capacity_,
    "VsmShadowRasterizerPass: uploaded job count exceeds constants capacity");
  CHECK_NOTNULL_F(shadow_view_constants_mapped_ptr_,
    "VsmShadowRasterizerPass: mapped view constants buffer is required");

  job_view_constants_upload_.resize(eligible_page_job_indices.size());
  const auto base_snapshot = *input->base_view_constants;
  for (std::size_t i = 0; i < eligible_page_job_indices.size(); ++i) {
    const auto& job = prepared_pages[eligible_page_job_indices[i]];
    auto snapshot = base_snapshot;
    snapshot.view_matrix = job.projection.projection.view_matrix;
    snapshot.projection_matrix = BuildPageProjectionMatrix(job);
    snapshot.camera_position = {
      job.projection.projection.view_origin_ws_pad.x,
      job.projection.projection.view_origin_ws_pad.y,
      job.projection.projection.view_origin_ws_pad.z,
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

auto VsmShadowRasterizerPass::Impl::BindJobViewConstants(
  CommandRecorder& recorder, const RenderContext& context,
  const std::uint32_t local_job_index) const -> void
{
  CHECK_NOTNULL_F(shadow_view_constants_buffer_.get(),
    "VsmShadowRasterizerPass: page view constants buffer is unavailable");
  CHECK_F(context.frame_slot != frame::kInvalidSlot,
    "VsmShadowRasterizerPass: invalid frame slot during view constants "
    "binding");
  CHECK_F(local_job_index < shadow_view_constants_capacity_,
    "VsmShadowRasterizerPass: job index exceeds view constants capacity");

  const auto slot_offset = static_cast<std::uint64_t>(context.frame_slot.get())
      * static_cast<std::uint64_t>(shadow_view_constants_capacity_)
    + local_job_index;
  const auto byte_offset = slot_offset * sizeof(ViewConstants::GpuData);
  recorder.SetGraphicsRootConstantBufferView(
    static_cast<std::uint32_t>(binding::RootParam::kViewConstants),
    shadow_view_constants_buffer_->GetGPUVirtualAddress() + byte_offset);
}

auto VsmShadowRasterizerPass::Impl::PrepareJobDepthStencilView(
  const RenderContext& context, graphics::Texture& depth_texture,
  const renderer::vsm::VsmShadowRasterPageJob& job) const -> NativeView
{
  auto& graphics = context.GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();
  CHECK_F(registry.Contains(depth_texture),
    "VsmShadowRasterizerPass: physical pool shadow texture must be registered "
    "before page DSV creation");

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

auto VsmShadowRasterizerPass::Impl::EnsureShaderVisibleIndex(Buffer& buffer,
  const graphics::BufferViewDescription& view_desc,
  const char* debug_label) const -> oxygen::ShaderVisibleIndex
{
  auto& registry = gfx->GetResourceRegistry();
  if (const auto existing = registry.FindShaderVisibleIndex(buffer, view_desc);
    existing.has_value()) {
    return *existing;
  }

  auto handle = gfx->GetDescriptorAllocator().Allocate(
    view_desc.view_type, graphics::DescriptorVisibility::kShaderVisible);
  if (!handle.IsValid()) {
    LOG_F(ERROR, "VsmShadowRasterizerPass: failed to allocate {} descriptor",
      debug_label);
    return oxygen::kInvalidShaderVisibleIndex;
  }

  const auto index
    = gfx->GetDescriptorAllocator().GetShaderVisibleIndex(handle);
  const auto view = registry.RegisterView(buffer, std::move(handle), view_desc);
  if (!view->IsValid()) {
    LOG_F(ERROR, "VsmShadowRasterizerPass: failed to register {} descriptor",
      debug_label);
    return oxygen::kInvalidShaderVisibleIndex;
  }
  return index;
}

auto VsmShadowRasterizerPass::Impl::EnsurePageJobBuffer(
  const std::uint32_t required_jobs) -> void
{
  if (required_jobs == 0U) {
    return;
  }
  if (page_job_buffer_ && page_job_upload_buffer_
    && page_job_upload_ptr_ != nullptr && page_job_capacity_ == required_jobs) {
    return;
  }

  if (gfx != nullptr) {
    auto& registry = gfx->GetResourceRegistry();
    if (page_job_buffer_ && registry.Contains(*page_job_buffer_)) {
      registry.UnRegisterResource(*page_job_buffer_);
    }
    if (page_job_upload_buffer_
      && registry.Contains(*page_job_upload_buffer_)) {
      registry.UnRegisterResource(*page_job_upload_buffer_);
    }
  }
  ReleaseUploadBuffer(page_job_upload_buffer_, page_job_upload_ptr_);
  page_job_buffer_.reset();
  page_job_capacity_ = required_jobs;
  page_job_srv_ = oxygen::kInvalidShaderVisibleIndex;

  const auto size_bytes = static_cast<std::uint64_t>(required_jobs)
    * sizeof(renderer::vsm::VsmShaderRasterPageJob);
  const auto debug_base = config != nullptr && !config->debug_name.empty()
    ? config->debug_name
    : "VsmShadowRasterizerPass";

  const graphics::BufferDesc page_job_desc {
    .size_bytes = size_bytes,
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = debug_base + ".PageJobs",
  };
  page_job_buffer_ = gfx->CreateBuffer(page_job_desc);
  CHECK_NOTNULL_F(page_job_buffer_.get(),
    "VsmShadowRasterizerPass: failed to create page-job buffer");
  page_job_buffer_->SetName(page_job_desc.debug_name);
  gfx->GetResourceRegistry().Register(page_job_buffer_);

  const graphics::BufferDesc upload_desc {
    .size_bytes = size_bytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = debug_base + ".PageJobs.Upload",
  };
  page_job_upload_buffer_ = gfx->CreateBuffer(upload_desc);
  CHECK_NOTNULL_F(page_job_upload_buffer_.get(),
    "VsmShadowRasterizerPass: failed to create page-job upload buffer");
  page_job_upload_buffer_->SetName(upload_desc.debug_name);
  gfx->GetResourceRegistry().Register(page_job_upload_buffer_);
  page_job_upload_ptr_
    = page_job_upload_buffer_->Map(0U, upload_desc.size_bytes);
  CHECK_NOTNULL_F(page_job_upload_ptr_,
    "VsmShadowRasterizerPass: failed to map page-job upload buffer");

  const graphics::BufferViewDescription view_desc {
    .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .range = { 0U, size_bytes },
    .stride = sizeof(renderer::vsm::VsmShaderRasterPageJob),
  };
  page_job_srv_
    = EnsureShaderVisibleIndex(*page_job_buffer_, view_desc, "page-job SRV");
  CHECK_F(page_job_srv_.IsValid(),
    "VsmShadowRasterizerPass: page-job SRV must be valid");
}

auto VsmShadowRasterizerPass::Impl::EnsureInstanceCullingConstantsBuffer(
  const std::uint32_t slot_count) -> void
{
  if (slot_count == 0U) {
    return;
  }
  if (instance_culling_constants_buffer_
    && instance_culling_constants_capacity_ == slot_count) {
    return;
  }

  if (gfx != nullptr) {
    auto& registry = gfx->GetResourceRegistry();
    if (instance_culling_constants_buffer_
      && registry.Contains(*instance_culling_constants_buffer_)) {
      registry.UnRegisterResource(*instance_culling_constants_buffer_);
    }
  }
  ReleaseUploadBuffer(
    instance_culling_constants_buffer_, instance_culling_constants_mapped_ptr_);

  instance_culling_constants_capacity_ = slot_count;
  instance_culling_constants_indices_.clear();

  const auto debug_base = config != nullptr && !config->debug_name.empty()
    ? config->debug_name
    : "VsmShadowRasterizerPass";
  const auto total_bytes
    = static_cast<std::uint64_t>(slot_count) * kInstanceCullingConstantsStride;
  const graphics::BufferDesc desc {
    .size_bytes = total_bytes,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = debug_base + ".InstanceCullingConstants",
  };
  instance_culling_constants_buffer_ = gfx->CreateBuffer(desc);
  CHECK_NOTNULL_F(instance_culling_constants_buffer_.get(),
    "VsmShadowRasterizerPass: failed to create instance-culling constants");
  instance_culling_constants_buffer_->SetName(desc.debug_name);
  gfx->GetResourceRegistry().Register(instance_culling_constants_buffer_);
  instance_culling_constants_mapped_ptr_
    = instance_culling_constants_buffer_->Map(0U, desc.size_bytes);
  CHECK_NOTNULL_F(instance_culling_constants_mapped_ptr_,
    "VsmShadowRasterizerPass: failed to map instance-culling constants");

  instance_culling_constants_indices_.reserve(slot_count);
  for (std::uint32_t slot = 0U; slot < slot_count; ++slot) {
    auto handle = gfx->GetDescriptorAllocator().Allocate(
      graphics::ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(),
      "VsmShadowRasterizerPass: failed to allocate instance-culling CBV");

    const graphics::BufferViewDescription view_desc {
      .view_type = graphics::ResourceViewType::kConstantBuffer,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range
      = { static_cast<std::uint64_t>(slot) * kInstanceCullingConstantsStride,
        kInstanceCullingConstantsStride },
    };
    instance_culling_constants_indices_.push_back(
      gfx->GetDescriptorAllocator().GetShaderVisibleIndex(handle));
    gfx->GetResourceRegistry().RegisterView(
      *instance_culling_constants_buffer_, std::move(handle), view_desc);
  }
}

auto VsmShadowRasterizerPass::Impl::WriteInstanceCullingConstants(
  const std::uint32_t slot, const InstanceCullingPassConstants& constants) const
  -> void
{
  CHECK_NOTNULL_F(instance_culling_constants_mapped_ptr_,
    "VsmShadowRasterizerPass: instance-culling constants are not mapped");
  CHECK_F(slot < instance_culling_constants_indices_.size(),
    "VsmShadowRasterizerPass: instance-culling constants slot {} exceeds "
    "capacity {}",
    slot, instance_culling_constants_indices_.size());

  auto* destination
    = static_cast<std::byte*>(instance_culling_constants_mapped_ptr_)
    + static_cast<std::size_t>(slot) * kInstanceCullingConstantsStride;
  std::memcpy(destination, &constants, sizeof(constants));
}

auto VsmShadowRasterizerPass::Impl::EnsurePartitionStateCount(
  const std::size_t required_count) -> void
{
  if (partition_states.size() < required_count) {
    partition_states.resize(required_count);
  }
}

auto VsmShadowRasterizerPass::Impl::EnsurePartitionResources(
  PartitionState& partition, const std::uint32_t required_pages,
  const std::uint32_t partition_count, const std::uint32_t slot_index) -> void
{
  CHECK_F(required_pages != 0U && partition_count != 0U,
    "VsmShadowRasterizerPass: partition resources require non-zero pages and "
    "command count");

  if (partition.command_buffer && partition.count_buffer
    && partition.count_clear_buffer
    && partition.count_clear_mapped_ptr != nullptr
    && partition.page_count == required_pages
    && partition.max_commands_per_page == partition_count) {
    return;
  }

  DLOG_F(2,
    "VsmShadowRasterizerPass: resizing partition {} buffers to pages={} "
    "commands_per_page={}",
    slot_index, required_pages, partition_count);
  ReleasePartitionResources(partition);

  partition.page_count = required_pages;
  partition.max_commands_per_page = partition_count;

  const auto total_command_count = static_cast<std::uint64_t>(required_pages)
    * static_cast<std::uint64_t>(partition_count);
  const auto command_bytes
    = total_command_count * sizeof(renderer::vsm::VsmShaderIndirectDrawCommand);
  const auto count_bytes
    = static_cast<std::uint64_t>(required_pages) * sizeof(std::uint32_t);
  const auto debug_base = config != nullptr && !config->debug_name.empty()
    ? config->debug_name
    : "VsmShadowRasterizerPass";
  const auto suffix = ".Partition" + std::to_string(slot_index);

  const graphics::BufferDesc command_desc {
    .size_bytes = command_bytes,
    .usage = graphics::BufferUsage::kStorage | graphics::BufferUsage::kIndirect,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = debug_base + suffix + ".IndirectCommands",
  };
  partition.command_buffer = gfx->CreateBuffer(command_desc);
  CHECK_NOTNULL_F(partition.command_buffer.get(),
    "VsmShadowRasterizerPass: failed to create indirect-command buffer");
  partition.command_buffer->SetName(command_desc.debug_name);
  gfx->GetResourceRegistry().Register(partition.command_buffer);

  const graphics::BufferDesc count_desc {
    .size_bytes = count_bytes,
    .usage = graphics::BufferUsage::kStorage | graphics::BufferUsage::kIndirect,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = debug_base + suffix + ".CommandCounts",
  };
  partition.count_buffer = gfx->CreateBuffer(count_desc);
  CHECK_NOTNULL_F(partition.count_buffer.get(),
    "VsmShadowRasterizerPass: failed to create command-count buffer");
  partition.count_buffer->SetName(count_desc.debug_name);
  gfx->GetResourceRegistry().Register(partition.count_buffer);

  const graphics::BufferDesc clear_desc {
    .size_bytes = count_bytes,
    .usage = graphics::BufferUsage::kNone,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = debug_base + suffix + ".CommandCounts.Clear",
  };
  partition.count_clear_buffer = gfx->CreateBuffer(clear_desc);
  CHECK_NOTNULL_F(partition.count_clear_buffer.get(),
    "VsmShadowRasterizerPass: failed to create command-count clear buffer");
  partition.count_clear_buffer->SetName(clear_desc.debug_name);
  gfx->GetResourceRegistry().Register(partition.count_clear_buffer);
  partition.count_clear_mapped_ptr
    = partition.count_clear_buffer->Map(0U, clear_desc.size_bytes);
  CHECK_NOTNULL_F(partition.count_clear_mapped_ptr,
    "VsmShadowRasterizerPass: failed to map command-count clear buffer");
  std::memset(partition.count_clear_mapped_ptr, 0, clear_desc.size_bytes);

  const graphics::BufferViewDescription command_uav_desc {
    .view_type = graphics::ResourceViewType::kStructuredBuffer_UAV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .range = { 0U, command_bytes },
    .stride = sizeof(renderer::vsm::VsmShaderIndirectDrawCommand),
  };
  partition.command_uav = EnsureShaderVisibleIndex(
    *partition.command_buffer, command_uav_desc, "indirect-command UAV");
  CHECK_F(partition.command_uav.IsValid(),
    "VsmShadowRasterizerPass: indirect-command UAV must be valid");

  const graphics::BufferViewDescription count_uav_desc {
    .view_type = graphics::ResourceViewType::kStructuredBuffer_UAV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .range = { 0U, count_bytes },
    .stride = sizeof(std::uint32_t),
  };
  partition.count_uav = EnsureShaderVisibleIndex(
    *partition.count_buffer, count_uav_desc, "command-count UAV");
  CHECK_F(partition.count_uav.IsValid(),
    "VsmShadowRasterizerPass: command-count UAV must be valid");
}

auto VsmShadowRasterizerPass::Impl::CollectActiveShadowPartitions(
  const PreparedSceneFrame& prepared_frame) -> void
{
  active_partitions.clear();
  for (const auto& partition : prepared_frame.partitions) {
    if (!IsShadowRasterPartition(partition)) {
      continue;
    }
    active_partitions.push_back(ActivePartitionRange {
      .pass_mask = partition.pass_mask,
      .begin = partition.begin,
      .count = partition.end - partition.begin,
    });
  }
}

auto VsmShadowRasterizerPass::Impl::BuildEligiblePageIndices() -> void
{
  eligible_page_job_indices.clear();
  deferred_static_pages = 0U;
  deferred_non_dynamic_pages = 0U;

  CHECK_F(dynamic_slice_index.has_value(),
    "VsmShadowRasterizerPass: dynamic slice must be resolved before page "
    "filtering");
  for (std::uint32_t i = 0U; i < prepared_pages.size(); ++i) {
    const auto& job = prepared_pages[i];
    if (job.static_only) {
      ++deferred_static_pages;
      continue;
    }
    if (job.physical_coord.slice != *dynamic_slice_index) {
      ++deferred_non_dynamic_pages;
      continue;
    }
    eligible_page_job_indices.push_back(i);
  }
}

auto VsmShadowRasterizerPass::Impl::PrepareInstanceCulling(
  CommandRecorder& recorder, const RenderContext& context,
  const PreparedSceneFrame& prepared_frame) -> void
{
  instance_culling_ready = false;
  inspection_partitions.clear();

  if (eligible_page_job_indices.empty() || active_partitions.empty()) {
    instance_culling_ready = true;
    return;
  }
  if (!prepared_frame.bindless_draw_metadata_slot.IsValid()) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: skipping {} prepared pages because the "
      "prepared scene frame has no valid bindless draw-metadata slot",
      eligible_page_job_indices.size());
    return;
  }
  if (!prepared_frame.bindless_draw_bounds_slot.IsValid()) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: skipping {} prepared pages because the "
      "prepared scene frame has no valid bindless draw-bounds slot",
      eligible_page_job_indices.size());
    return;
  }
  if (prepared_frame.draw_bounding_spheres.empty()) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: skipping {} prepared pages because draw bounds "
      "are unavailable",
      eligible_page_job_indices.size());
    return;
  }

  EnsurePageJobBuffer(
    static_cast<std::uint32_t>(eligible_page_job_indices.size()));
  page_job_upload_.resize(eligible_page_job_indices.size());
  for (std::size_t i = 0; i < eligible_page_job_indices.size(); ++i) {
    page_job_upload_[i]
      = MakeShaderRasterJob(prepared_pages[eligible_page_job_indices[i]]);
  }
  CHECK_NOTNULL_F(page_job_upload_ptr_,
    "VsmShadowRasterizerPass: page-job upload buffer must be mapped");
  std::memcpy(page_job_upload_ptr_, page_job_upload_.data(),
    page_job_upload_.size() * sizeof(renderer::vsm::VsmShaderRasterPageJob));

  EnsureInstanceCullingConstantsBuffer(
    static_cast<std::uint32_t>(active_partitions.size()));
  EnsurePartitionStateCount(active_partitions.size());

  if (!recorder.IsResourceTracked(*page_job_upload_buffer_)) {
    recorder.BeginTrackingResourceState(
      *page_job_upload_buffer_, ResourceStates::kGenericRead, true);
  }
  if (!recorder.IsResourceTracked(*page_job_buffer_)) {
    recorder.BeginTrackingResourceState(
      *page_job_buffer_, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*instance_culling_constants_buffer_)) {
    recorder.BeginTrackingResourceState(
      *instance_culling_constants_buffer_, ResourceStates::kGenericRead, true);
  }

  recorder.RequireResourceState(
    *page_job_upload_buffer_, ResourceStates::kCopySource);
  recorder.RequireResourceState(*page_job_buffer_, ResourceStates::kCopyDest);

  previous_frame_hzb = {};
  if (const auto* screen_hzb_pass = context.GetPass<ScreenHzbBuildPass>();
    screen_hzb_pass != nullptr) {
    previous_frame_hzb
      = screen_hzb_pass->GetPreviousFrameOutput(context.current_view.view_id);
    if (previous_frame_hzb.available && previous_frame_hzb.texture != nullptr
      && !recorder.IsResourceTracked(*previous_frame_hzb.texture)) {
      recorder.BeginTrackingResourceState(
        *previous_frame_hzb.texture, ResourceStates::kShaderResource, true);
    }
  }

  for (std::size_t i = 0; i < active_partitions.size(); ++i) {
    auto& state = partition_states[i];
    const auto& partition = active_partitions[i];
    state.pass_mask = partition.pass_mask;
    state.begin = partition.begin;
    state.count = partition.count;
    EnsurePartitionResources(state,
      static_cast<std::uint32_t>(eligible_page_job_indices.size()),
      partition.count, static_cast<std::uint32_t>(i));

    if (!recorder.IsResourceTracked(*state.command_buffer)) {
      recorder.BeginTrackingResourceState(
        *state.command_buffer, ResourceStates::kCommon, true);
    }
    if (!recorder.IsResourceTracked(*state.count_buffer)) {
      recorder.BeginTrackingResourceState(
        *state.count_buffer, ResourceStates::kCommon, true);
    }
    if (!recorder.IsResourceTracked(*state.count_clear_buffer)) {
      recorder.BeginTrackingResourceState(
        *state.count_clear_buffer, ResourceStates::kGenericRead, true);
    }

    recorder.RequireResourceState(
      *state.count_clear_buffer, ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *state.count_buffer, ResourceStates::kCopyDest);
  }

  recorder.FlushBarriers();

  recorder.CopyBuffer(*page_job_buffer_, 0U, *page_job_upload_buffer_, 0U,
    page_job_upload_.size() * sizeof(renderer::vsm::VsmShaderRasterPageJob));
  for (std::size_t i = 0; i < active_partitions.size(); ++i) {
    auto& state = partition_states[i];
    recorder.CopyBuffer(*state.count_buffer, 0U, *state.count_clear_buffer, 0U,
      static_cast<std::size_t>(state.page_count) * sizeof(std::uint32_t));
  }

  recorder.RequireResourceState(
    *page_job_buffer_, ResourceStates::kShaderResource);
  for (std::size_t i = 0; i < active_partitions.size(); ++i) {
    auto& state = partition_states[i];
    recorder.RequireResourceState(
      *state.command_buffer, ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(
      *state.count_buffer, ResourceStates::kUnorderedAccess);
  }
  if (previous_frame_hzb.available && previous_frame_hzb.texture != nullptr) {
    recorder.RequireResourceState(
      *previous_frame_hzb.texture, ResourceStates::kShaderResource);
  }
  recorder.FlushBarriers();

  for (std::size_t i = 0; i < active_partitions.size(); ++i) {
    const auto& partition = active_partitions[i];
    auto& state = partition_states[i];

    const auto constants = InstanceCullingPassConstants {
      .page_job_buffer_index = page_job_srv_,
      .draw_metadata_index = prepared_frame.bindless_draw_metadata_slot,
      .draw_bounds_index = prepared_frame.bindless_draw_bounds_slot,
      .previous_frame_hzb_index = previous_frame_hzb.available
        ? previous_frame_hzb.srv_index
        : oxygen::kInvalidShaderVisibleIndex,
      .indirect_commands_uav_index = state.command_uav,
      .command_counts_uav_index = state.count_uav,
      .prepared_page_count
      = static_cast<std::uint32_t>(eligible_page_job_indices.size()),
      .partition_begin = partition.begin,
      .partition_count = partition.count,
      .draw_bounds_count
      = static_cast<std::uint32_t>(prepared_frame.draw_bounding_spheres.size()),
      .max_commands_per_page = state.max_commands_per_page,
      .previous_frame_hzb_width = previous_frame_hzb.width,
      .previous_frame_hzb_height = previous_frame_hzb.height,
      .previous_frame_hzb_mip_count = previous_frame_hzb.mip_count,
      .previous_frame_hzb_available = previous_frame_hzb.available ? 1U : 0U,
    };
    WriteInstanceCullingConstants(static_cast<std::uint32_t>(i), constants);

    inspection_partitions.push_back(IndirectPartitionInspection {
      .pass_mask = partition.pass_mask,
      .draw_count = partition.count,
      .max_commands_per_page = state.max_commands_per_page,
      .command_buffer = state.command_buffer.get(),
      .count_buffer = state.count_buffer.get(),
    });
  }

  DLOG_F(2,
    "VsmShadowRasterizerPass: prepared instance culling pages={} "
    "partitions={} previous_hzb_available={}",
    eligible_page_job_indices.size(), active_partitions.size(),
    previous_frame_hzb.available);
  instance_culling_ready = true;
}

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
  impl_->ResetExecutionState();
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

auto VsmShadowRasterizerPass::GetIndirectPartitionsForInspection()
  const noexcept -> std::span<const IndirectPartitionInspection>
{
  return { impl_->inspection_partitions.data(),
    impl_->inspection_partitions.size() };
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

  impl_->EnsureInstanceCullingPipelineState();
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
  impl_->ResetExecutionState();

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
  impl_->BuildEligiblePageIndices();

  if (!impl_->eligible_page_job_indices.empty()
    && impl_->input->base_view_constants.has_value()
    && impl_->input->base_view_constants->view_frame_bindings_bslot.IsValid()) {
    impl_->EnsureShadowViewConstantsCapacity(Context(),
      static_cast<std::uint32_t>(impl_->eligible_page_job_indices.size()));
    impl_->UploadPreparedJobViewConstants(Context());
  }

  DLOG_F(2,
    "VsmShadowRasterizerPass: prepare map_count={} prepared_pages={} "
    "eligible_pages={} deferred_static={} deferred_non_dynamic={} "
    "dynamic_slice={} base_view_constants={} view_frame_slot_valid={}",
    impl_->input->projections.size(), impl_->prepared_pages.size(),
    impl_->eligible_page_job_indices.size(), impl_->deferred_static_pages,
    impl_->deferred_non_dynamic_pages, *impl_->dynamic_slice_index,
    impl_->input->base_view_constants.has_value(),
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

  if (impl_->deferred_static_pages > 0U
    || impl_->deferred_non_dynamic_pages > 0U) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: deferred page jobs outside F2 scope "
      "(static_only={} non_dynamic_slice={})",
      impl_->deferred_static_pages, impl_->deferred_non_dynamic_pages);
  }

  if (impl_->eligible_page_job_indices.empty()) {
    DLOG_F(2, "VsmShadowRasterizerPass: no eligible dynamic page jobs");
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
      "for {} eligible prepared pages",
      impl_->eligible_page_job_indices.size());
    recorder.RequireResourceState(
      *shadow_texture, ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    Context().RegisterPass(this);
    co_return;
  }

  if (!impl_->input->base_view_constants.has_value()) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: skipping {} eligible prepared pages because "
      "base_view_constants were not provided",
      impl_->eligible_page_job_indices.size());
    recorder.RequireResourceState(
      *shadow_texture, ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    Context().RegisterPass(this);
    co_return;
  }

  if (!impl_->input->base_view_constants->view_frame_bindings_bslot.IsValid()) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: skipping {} eligible prepared pages because "
      "the base view constants do not carry a valid bindless view-frame "
      "bindings slot",
      impl_->eligible_page_job_indices.size());
    recorder.RequireResourceState(
      *shadow_texture, ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    Context().RegisterPass(this);
    co_return;
  }

  if (!impl_->job_view_constants_uploaded) {
    LOG_F(WARNING,
      "VsmShadowRasterizerPass: skipping {} eligible prepared pages because "
      "page-local view constants were not uploaded",
      impl_->eligible_page_job_indices.size());
    recorder.RequireResourceState(
      *shadow_texture, ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    Context().RegisterPass(this);
    co_return;
  }

  CHECK_F(impl_->dynamic_slice_index.has_value(),
    "VsmShadowRasterizerPass: dynamic slice must be resolved before execute");

  impl_->CollectActiveShadowPartitions(*psf);
  impl_->PrepareInstanceCulling(recorder, Context(), *psf);
  if (!impl_->instance_culling_ready) {
    recorder.RequireResourceState(
      *shadow_texture, ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    Context().RegisterPass(this);
    co_return;
  }

  if (impl_->active_partitions.empty()) {
    DLOG_F(2,
      "VsmShadowRasterizerPass: no active shadow raster partitions for {} "
      "eligible pages",
      impl_->eligible_page_job_indices.size());
    recorder.RequireResourceState(
      *shadow_texture, ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    Context().RegisterPass(this);
    co_return;
  }

  CHECK_F(impl_->instance_culling_pso_.has_value(),
    "VsmShadowRasterizerPass: instance-culling pipeline state is unavailable");
  CHECK_NOTNULL_F(Context().view_constants.get(),
    "VsmShadowRasterizerPass: view constants are required for instance "
    "culling");

  for (std::size_t partition_index = 0U;
    partition_index < impl_->active_partitions.size(); ++partition_index) {
    const auto& partition = impl_->active_partitions[partition_index];
    auto& state = impl_->partition_states[partition_index];
    CHECK_F(partition_index < impl_->instance_culling_constants_indices_.size(),
      "VsmShadowRasterizerPass: partition constants slot {} exceeds capacity "
      "{}",
      partition_index, impl_->instance_culling_constants_indices_.size());
    CHECK_F(
      impl_->instance_culling_constants_indices_[partition_index].IsValid(),
      "VsmShadowRasterizerPass: partition constants slot {} is invalid",
      partition_index);

    recorder.SetPipelineState(*impl_->instance_culling_pso_);
    recorder.SetComputeRootConstantBufferView(
      static_cast<std::uint32_t>(binding::RootParam::kViewConstants),
      Context().view_constants->GetGPUVirtualAddress());
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(binding::RootParam::kRootConstants), 0U, 0U);
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(binding::RootParam::kRootConstants),
      impl_->instance_culling_constants_indices_[partition_index].get(), 1U);
    recorder.Dispatch((partition.count + (kInstanceCullingThreadGroupSize - 1U))
        / kInstanceCullingThreadGroupSize,
      static_cast<std::uint32_t>(impl_->eligible_page_job_indices.size()), 1U);

    recorder.RequireResourceState(
      *state.command_buffer, ResourceStates::kIndirectArgument);
    recorder.RequireResourceState(
      *state.count_buffer, ResourceStates::kIndirectArgument);
  }
  recorder.FlushBarriers();

  for (std::uint32_t local_page_index = 0U;
    local_page_index < impl_->eligible_page_job_indices.size();
    ++local_page_index) {
    const auto& job
      = impl_
          ->prepared_pages[impl_->eligible_page_job_indices[local_page_index]];
    const auto dsv
      = impl_->PrepareJobDepthStencilView(Context(), *shadow_texture, job);

    recorder.SetRenderTargets({}, dsv);
    recorder.SetViewport(job.viewport);
    recorder.SetScissors(job.scissors);

    for (std::size_t partition_index = 0U;
      partition_index < impl_->active_partitions.size(); ++partition_index) {
      const auto& partition = impl_->active_partitions[partition_index];
      const auto& state = impl_->partition_states[partition_index];
      recorder.SetPipelineState(
        SelectPipelineStateForPartition(partition.pass_mask));
      RebindCommonRootParameters(recorder);
      impl_->BindJobViewConstants(recorder, Context(), local_page_index);
      recorder.ExecuteIndirectCounted(*state.command_buffer,
        MakePartitionCommandOffset(
          local_page_index, state.max_commands_per_page),
        state.max_commands_per_page,
        CommandRecorder::IndirectCommandLayout::kDrawWithRootConstant,
        *state.count_buffer,
        static_cast<std::uint64_t>(local_page_index) * sizeof(std::uint32_t));
    }
  }

  recorder.RequireResourceState(
    *shadow_texture, ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  DLOG_F(2,
    "VsmShadowRasterizerPass: execute eligible_pages={} partitions={} "
    "previous_hzb_available={} counted_indirect=true",
    impl_->eligible_page_job_indices.size(), impl_->active_partitions.size(),
    impl_->previous_frame_hzb.available);

  Context().RegisterPass(this);
  co_return;
}

} // namespace oxygen::engine
