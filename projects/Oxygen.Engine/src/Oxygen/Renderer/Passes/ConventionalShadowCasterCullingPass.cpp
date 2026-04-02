//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/ConventionalShadowCasterCullingPass.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Passes/ConventionalShadowReceiverMaskPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/ConventionalShadowCasterCullingPartition.h>
#include <Oxygen/Renderer/Types/ConventionalShadowIndirectDrawCommand.h>
#include <Oxygen/Renderer/Types/ConventionalShadowReceiverAnalysis.h>

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::ShaderVisibleIndex;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::ComputePipelineDesc;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::ResourceViewType;
using oxygen::renderer::ConventionalShadowCasterCullingPartition;
using oxygen::renderer::ConventionalShadowIndirectDrawCommand;

namespace oxygen::engine {

namespace {

  constexpr std::uint32_t kThreadGroupSize = 64U;

  struct alignas(packing::kShaderDataFieldAlignment)
    ConventionalShadowCasterCullingPassConstants {
    ShaderVisibleIndex draw_record_buffer_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex draw_metadata_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex job_buffer_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex receiver_mask_summary_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex receiver_mask_base_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex receiver_mask_hierarchy_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex partition_buffer_index { kInvalidShaderVisibleIndex };
    std::uint32_t job_count { 0U };
    std::uint32_t base_tile_resolution { 0U };
    std::uint32_t hierarchy_tile_resolution { 0U };
    std::uint32_t base_tiles_per_job { 0U };
    std::uint32_t hierarchy_tiles_per_job { 0U };
    std::uint32_t hierarchy_reduction { 0U };
    std::uint32_t partition_count { 0U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
  };

  static_assert(sizeof(ConventionalShadowCasterCullingPassConstants) == 64U);
  static_assert(sizeof(ConventionalShadowCasterCullingPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

  struct ActivePartitionRange {
    PassMask pass_mask {};
    std::uint32_t partition_index { 0U };
    std::uint32_t record_begin { 0U };
    std::uint32_t record_count { 0U };
  };

  [[nodiscard]] auto DivideRoundUp(
    const std::uint32_t value, const std::uint32_t divisor) -> std::uint32_t
  {
    return divisor == 0U ? 0U : (value + divisor - 1U) / divisor;
  }

  [[nodiscard]] auto MakeStructuredViewDesc(const ResourceViewType view_type,
    const std::uint64_t size_bytes, const std::uint32_t stride)
    -> graphics::BufferViewDescription
  {
    return graphics::BufferViewDescription {
      .view_type = view_type,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { 0U, size_bytes },
      .stride = stride,
    };
  }

  template <typename Resource>
  auto RegisterResourceIfNeeded(
    Graphics& gfx, const std::shared_ptr<Resource>& resource) -> void
  {
    if (!resource) {
      return;
    }
    auto& registry = gfx.GetResourceRegistry();
    if (!registry.Contains(*resource)) {
      registry.Register(resource);
    }
  }

  template <typename Resource>
  auto UnregisterResourceIfPresent(
    Graphics& gfx, const std::shared_ptr<Resource>& resource) -> void
  {
    if (!resource) {
      return;
    }
    auto& registry = gfx.GetResourceRegistry();
    if (registry.Contains(*resource)) {
      registry.UnRegisterResource(*resource);
    }
  }

  auto BindComputeStage(CommandRecorder& recorder,
    const graphics::ComputePipelineDesc& pso_desc,
    const ShaderVisibleIndex pass_constants_index,
    const std::uint32_t partition_slot, const RenderContext& context) -> void
  {
    DCHECK_NOTNULL_F(context.view_constants);
    recorder.SetPipelineState(pso_desc);
    recorder.SetComputeRootConstantBufferView(
      static_cast<std::uint32_t>(binding::RootParam::kViewConstants),
      context.view_constants->GetGPUVirtualAddress());
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(binding::RootParam::kRootConstants),
      partition_slot, 0U);
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(binding::RootParam::kRootConstants),
      pass_constants_index.get(), 1U);
  }

  [[nodiscard]] auto CollectActiveShadowRecordPartitions(
    const PreparedSceneFrame& prepared_frame)
    -> std::vector<ActivePartitionRange>
  {
    auto partitions = std::vector<ActivePartitionRange> {};
    if (prepared_frame.conventional_shadow_draw_records.empty()) {
      return partitions;
    }

    partitions.reserve(prepared_frame.partitions.size());
    auto seen_partition_indices = std::unordered_set<std::uint32_t> {};
    auto current = std::optional<ActivePartitionRange> {};

    for (std::uint32_t record_index = 0U;
      record_index < prepared_frame.conventional_shadow_draw_records.size();
      ++record_index) {
      const auto& record
        = prepared_frame.conventional_shadow_draw_records[record_index];

      if (!current.has_value()
        || current->partition_index != record.partition_index) {
        if (current.has_value()) {
          partitions.push_back(*current);
        }
        CHECK_F(seen_partition_indices.insert(record.partition_index).second,
          "ConventionalShadowCasterCullingPass: draw-record partition {} is "
          "not contiguous in the authoritative conventional draw stream",
          record.partition_index);
        current = ActivePartitionRange {
          .pass_mask = record.partition_pass_mask,
          .partition_index = record.partition_index,
          .record_begin = record_index,
          .record_count = 1U,
        };
      } else {
        CHECK_F(current->pass_mask == record.partition_pass_mask,
          "ConventionalShadowCasterCullingPass: draw-record partition {} "
          "changed pass mask inside one contiguous run",
          record.partition_index);
        ++current->record_count;
      }
    }

    if (current.has_value()) {
      partitions.push_back(*current);
    }
    return partitions;
  }

} // namespace

struct ConventionalShadowCasterCullingPass::Impl {
  struct PartitionState {
    PassMask pass_mask {};
    std::uint32_t partition_index { 0U };
    std::uint32_t record_begin { 0U };
    std::uint32_t record_count { 0U };
    std::uint32_t max_commands_per_job { 0U };
    std::shared_ptr<Buffer> command_buffer {};
    std::shared_ptr<Buffer> count_buffer {};
    std::shared_ptr<Buffer> count_clear_buffer {};
    void* count_clear_mapped_ptr { nullptr };
    ShaderVisibleIndex command_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex command_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex count_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex count_srv_index { kInvalidShaderVisibleIndex };
  };

  struct ViewState {
    std::shared_ptr<Buffer> job_buffer {};
    std::shared_ptr<Buffer> job_upload_buffer {};
    void* job_upload_mapped_ptr { nullptr };
    ShaderVisibleIndex job_srv_index { kInvalidShaderVisibleIndex };

    std::shared_ptr<Buffer> partition_buffer {};
    std::shared_ptr<Buffer> partition_upload_buffer {};
    void* partition_upload_mapped_ptr { nullptr };
    ShaderVisibleIndex partition_srv_index { kInvalidShaderVisibleIndex };

    std::vector<PartitionState> partition_states {};
    std::vector<IndirectPartitionInspection> inspection_partitions {};

    std::uint32_t capacity { 0U };
    std::uint32_t partition_capacity { 0U };
    std::uint32_t job_count { 0U };
    bool has_current_output { false };
  };

  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;
  std::unordered_map<ViewId, ViewState> view_states {};

  std::shared_ptr<Buffer> pass_constants_buffer {};
  void* pass_constants_mapped_ptr { nullptr };
  ShaderVisibleIndex pass_constants_index { kInvalidShaderVisibleIndex };

  ViewState* active_view_state { nullptr };
  ViewId active_view_id {};
  std::uint32_t active_job_count { 0U };
  std::shared_ptr<const Buffer> active_receiver_mask_summary_buffer {};
  std::shared_ptr<const Buffer> active_receiver_mask_base_buffer {};
  std::shared_ptr<const Buffer> active_receiver_mask_hierarchy_buffer {};
  std::vector<ActivePartitionRange> active_partitions {};
  bool resources_prepared { false };
  bool pipeline_ready { false };
  std::optional<graphics::ComputePipelineDesc> culling_pso {};

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl();

  auto ReleasePartitionResources(PartitionState& state) -> void;
  auto ReleaseViewResources(ViewState& state) -> void;
  auto EnsurePassConstantsBuffer() -> void;
  auto EnsureViewResources(ViewId view_id, std::uint32_t required_job_capacity,
    std::uint32_t required_partition_capacity) -> ViewState&;
  auto EnsurePartitionResources(ViewState& view_state, std::size_t slot_index,
    const ActivePartitionRange& partition) -> PartitionState&;
};

ConventionalShadowCasterCullingPass::ConventionalShadowCasterCullingPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(
      config ? config->debug_name : "ConventionalShadowCasterCullingPass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
}

ConventionalShadowCasterCullingPass::~ConventionalShadowCasterCullingPass()
  = default;

auto ConventionalShadowCasterCullingPass::GetCurrentOutput(
  const ViewId view_id) const -> Output
{
  if (const auto it = impl_->view_states.find(view_id);
    it != impl_->view_states.end() && it->second.has_current_output) {
    return Output {
      .job_count = it->second.job_count,
      .partition_count
      = static_cast<std::uint32_t>(it->second.inspection_partitions.size()),
      .available = true,
    };
  }
  return {};
}

auto ConventionalShadowCasterCullingPass::GetIndirectPartitionsForInspection(
  const ViewId view_id) const -> std::span<const IndirectPartitionInspection>
{
  if (const auto it = impl_->view_states.find(view_id);
    it != impl_->view_states.end() && it->second.has_current_output) {
    return { it->second.inspection_partitions.data(),
      it->second.inspection_partitions.size() };
  }
  return {};
}

auto ConventionalShadowCasterCullingPass::ValidateConfig() -> void
{
  if (!impl_->config) {
    throw std::runtime_error(
      "ConventionalShadowCasterCullingPass requires configuration");
  }
}

ConventionalShadowCasterCullingPass::Impl::~Impl()
{
  if (pass_constants_buffer && pass_constants_mapped_ptr != nullptr) {
    pass_constants_buffer->UnMap();
    pass_constants_mapped_ptr = nullptr;
  }

  if (gfx == nullptr) {
    return;
  }

  UnregisterResourceIfPresent(*gfx, pass_constants_buffer);
  for (auto& [view_id, state] : view_states) {
    static_cast<void>(view_id);
    ReleaseViewResources(state);
  }
}

auto ConventionalShadowCasterCullingPass::Impl::ReleasePartitionResources(
  PartitionState& state) -> void
{
  if (state.count_clear_buffer && state.count_clear_mapped_ptr != nullptr) {
    state.count_clear_buffer->UnMap();
    state.count_clear_mapped_ptr = nullptr;
  }

  UnregisterResourceIfPresent(*gfx, state.count_clear_buffer);
  UnregisterResourceIfPresent(*gfx, state.count_buffer);
  UnregisterResourceIfPresent(*gfx, state.command_buffer);

  state.command_buffer.reset();
  state.count_buffer.reset();
  state.count_clear_buffer.reset();
  state.command_uav_index = kInvalidShaderVisibleIndex;
  state.command_srv_index = kInvalidShaderVisibleIndex;
  state.count_uav_index = kInvalidShaderVisibleIndex;
  state.count_srv_index = kInvalidShaderVisibleIndex;
  state.pass_mask = {};
  state.partition_index = 0U;
  state.record_begin = 0U;
  state.record_count = 0U;
  state.max_commands_per_job = 0U;
}

auto ConventionalShadowCasterCullingPass::Impl::ReleaseViewResources(
  ViewState& state) -> void
{
  if (state.job_upload_buffer && state.job_upload_mapped_ptr != nullptr) {
    state.job_upload_buffer->UnMap();
    state.job_upload_mapped_ptr = nullptr;
  }
  if (state.partition_upload_buffer
    && state.partition_upload_mapped_ptr != nullptr) {
    state.partition_upload_buffer->UnMap();
    state.partition_upload_mapped_ptr = nullptr;
  }

  for (auto& partition_state : state.partition_states) {
    ReleasePartitionResources(partition_state);
  }

  UnregisterResourceIfPresent(*gfx, state.partition_upload_buffer);
  UnregisterResourceIfPresent(*gfx, state.partition_buffer);
  UnregisterResourceIfPresent(*gfx, state.job_upload_buffer);
  UnregisterResourceIfPresent(*gfx, state.job_buffer);

  state.job_buffer.reset();
  state.job_upload_buffer.reset();
  state.partition_buffer.reset();
  state.partition_upload_buffer.reset();
  state.job_srv_index = kInvalidShaderVisibleIndex;
  state.partition_srv_index = kInvalidShaderVisibleIndex;
  state.partition_states.clear();
  state.inspection_partitions.clear();
  state.capacity = 0U;
  state.partition_capacity = 0U;
  state.job_count = 0U;
  state.has_current_output = false;
}

auto ConventionalShadowCasterCullingPass::Impl::EnsurePassConstantsBuffer()
  -> void
{
  if (pass_constants_buffer) {
    return;
  }

  auto& registry = gfx->GetResourceRegistry();
  auto& allocator = gfx->GetDescriptorAllocator();

  const BufferDesc desc {
    .size_bytes = packing::kConstantBufferAlignment,
    .usage = BufferUsage::kConstant,
    .memory = BufferMemory::kUpload,
    .debug_name = config->debug_name + "_PassConstants",
  };
  pass_constants_buffer = gfx->CreateBuffer(desc);
  CHECK_NOTNULL_F(pass_constants_buffer.get(),
    "Failed to create conventional caster-culling constants buffer");
  pass_constants_buffer->SetName(desc.debug_name);
  RegisterResourceIfNeeded(*gfx, pass_constants_buffer);

  pass_constants_mapped_ptr = pass_constants_buffer->Map(0U, desc.size_bytes);
  CHECK_NOTNULL_F(pass_constants_mapped_ptr,
    "Failed to map conventional caster-culling constants buffer");

  auto cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
    graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(cbv_handle.IsValid(),
    "Failed to allocate conventional caster-culling constants CBV");
  pass_constants_index = allocator.GetShaderVisibleIndex(cbv_handle);
  registry.RegisterView(*pass_constants_buffer, std::move(cbv_handle),
    graphics::BufferViewDescription {
      .view_type = ResourceViewType::kConstantBuffer,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { 0U, desc.size_bytes },
    });
}

auto ConventionalShadowCasterCullingPass::Impl::EnsureViewResources(
  const ViewId view_id, const std::uint32_t required_job_capacity,
  const std::uint32_t required_partition_capacity) -> ViewState&
{
  auto& state = view_states[view_id];

  if (required_job_capacity == 0U || required_partition_capacity == 0U) {
    state.job_count = 0U;
    state.has_current_output = false;
    state.inspection_partitions.clear();
    return state;
  }

  if (state.capacity >= required_job_capacity
    && state.partition_capacity >= required_partition_capacity
    && state.job_buffer && state.job_upload_buffer && state.partition_buffer
    && state.partition_upload_buffer) {
    if (state.partition_states.size() < required_partition_capacity) {
      state.partition_states.resize(required_partition_capacity);
    }
    return state;
  }

  ReleaseViewResources(state);

  const auto create_buffer =
    [&](const std::string& name_suffix, const std::uint64_t size_bytes,
      const BufferUsage usage, const BufferMemory memory) {
      auto buffer = gfx->CreateBuffer(BufferDesc {
        .size_bytes = size_bytes,
        .usage = usage,
        .memory = memory,
        .debug_name = config->debug_name + "." + std::to_string(view_id.get())
          + "." + name_suffix,
      });
      CHECK_NOTNULL_F(buffer.get(),
        "Failed to create conventional caster-culling buffer {}", name_suffix);
      buffer->SetName(buffer->GetDescriptor().debug_name);
      RegisterResourceIfNeeded(*gfx, buffer);
      return buffer;
    };

  const auto job_buffer_size = static_cast<std::uint64_t>(required_job_capacity)
    * sizeof(renderer::ConventionalShadowReceiverAnalysisJob);
  state.job_buffer = create_buffer(
    "Jobs", job_buffer_size, BufferUsage::kStorage, BufferMemory::kDeviceLocal);
  state.job_upload_buffer = create_buffer(
    "JobsUpload", job_buffer_size, BufferUsage::kNone, BufferMemory::kUpload);
  state.job_upload_mapped_ptr
    = state.job_upload_buffer->Map(0U, job_buffer_size);
  CHECK_NOTNULL_F(state.job_upload_mapped_ptr,
    "Failed to map conventional caster-culling job upload buffer");

  const auto partition_buffer_size
    = static_cast<std::uint64_t>(required_partition_capacity)
    * sizeof(ConventionalShadowCasterCullingPartition);
  state.partition_buffer = create_buffer("Partitions", partition_buffer_size,
    BufferUsage::kStorage, BufferMemory::kDeviceLocal);
  state.partition_upload_buffer = create_buffer("PartitionsUpload",
    partition_buffer_size, BufferUsage::kNone, BufferMemory::kUpload);
  state.partition_upload_mapped_ptr
    = state.partition_upload_buffer->Map(0U, partition_buffer_size);
  CHECK_NOTNULL_F(state.partition_upload_mapped_ptr,
    "Failed to map conventional caster-culling partition upload buffer");

  auto& allocator = gfx->GetDescriptorAllocator();
  auto& registry = gfx->GetResourceRegistry();

  auto job_srv = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(job_srv.IsValid(),
    "Failed to allocate conventional caster-culling job SRV");
  state.job_srv_index = allocator.GetShaderVisibleIndex(job_srv);
  registry.RegisterView(*state.job_buffer, std::move(job_srv),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      job_buffer_size,
      sizeof(renderer::ConventionalShadowReceiverAnalysisJob)));

  auto partition_srv
    = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(partition_srv.IsValid(),
    "Failed to allocate conventional caster-culling partition SRV");
  state.partition_srv_index = allocator.GetShaderVisibleIndex(partition_srv);
  registry.RegisterView(*state.partition_buffer, std::move(partition_srv),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      partition_buffer_size, sizeof(ConventionalShadowCasterCullingPartition)));

  state.partition_states.resize(required_partition_capacity);
  state.capacity = required_job_capacity;
  state.partition_capacity = required_partition_capacity;
  state.job_count = 0U;
  state.has_current_output = false;
  return state;
}

auto ConventionalShadowCasterCullingPass::Impl::EnsurePartitionResources(
  ViewState& view_state, const std::size_t slot_index,
  const ActivePartitionRange& partition) -> PartitionState&
{
  CHECK_F(slot_index < view_state.partition_states.size(),
    "ConventionalShadowCasterCullingPass: partition slot {} exceeds capacity "
    "{}",
    slot_index, view_state.partition_states.size());

  auto& state = view_state.partition_states[slot_index];
  if (state.command_buffer && state.count_buffer && state.count_clear_buffer
    && state.count_clear_mapped_ptr != nullptr
    && state.record_count == partition.record_count
    && state.max_commands_per_job == partition.record_count) {
    state.pass_mask = partition.pass_mask;
    state.partition_index = partition.partition_index;
    state.record_begin = partition.record_begin;
    return state;
  }

  ReleasePartitionResources(state);

  state.pass_mask = partition.pass_mask;
  state.partition_index = partition.partition_index;
  state.record_begin = partition.record_begin;
  state.record_count = partition.record_count;
  state.max_commands_per_job = partition.record_count;

  const auto total_command_count
    = static_cast<std::uint64_t>(view_state.job_count)
    * static_cast<std::uint64_t>(partition.record_count);
  const auto command_bytes
    = total_command_count * sizeof(ConventionalShadowIndirectDrawCommand);
  const auto count_bytes
    = static_cast<std::uint64_t>(view_state.job_count) * sizeof(std::uint32_t);
  const auto debug_base = config != nullptr && !config->debug_name.empty()
    ? config->debug_name
    : "ConventionalShadowCasterCullingPass";
  const auto suffix = ".Partition" + std::to_string(slot_index);

  const BufferDesc command_desc {
    .size_bytes = command_bytes,
    .usage = BufferUsage::kStorage | BufferUsage::kIndirect,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = debug_base + suffix + ".IndirectCommands",
  };
  state.command_buffer = gfx->CreateBuffer(command_desc);
  CHECK_NOTNULL_F(state.command_buffer.get(),
    "ConventionalShadowCasterCullingPass: failed to create indirect-command "
    "buffer");
  state.command_buffer->SetName(command_desc.debug_name);
  RegisterResourceIfNeeded(*gfx, state.command_buffer);

  const BufferDesc count_desc {
    .size_bytes = count_bytes,
    .usage = BufferUsage::kStorage | BufferUsage::kIndirect,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = debug_base + suffix + ".CommandCounts",
  };
  state.count_buffer = gfx->CreateBuffer(count_desc);
  CHECK_NOTNULL_F(state.count_buffer.get(),
    "ConventionalShadowCasterCullingPass: failed to create command-count "
    "buffer");
  state.count_buffer->SetName(count_desc.debug_name);
  RegisterResourceIfNeeded(*gfx, state.count_buffer);

  const BufferDesc clear_desc {
    .size_bytes = count_bytes,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = debug_base + suffix + ".CommandCounts.Clear",
  };
  state.count_clear_buffer = gfx->CreateBuffer(clear_desc);
  CHECK_NOTNULL_F(state.count_clear_buffer.get(),
    "ConventionalShadowCasterCullingPass: failed to create count clear buffer");
  state.count_clear_buffer->SetName(clear_desc.debug_name);
  RegisterResourceIfNeeded(*gfx, state.count_clear_buffer);
  state.count_clear_mapped_ptr
    = state.count_clear_buffer->Map(0U, clear_desc.size_bytes);
  CHECK_NOTNULL_F(state.count_clear_mapped_ptr,
    "ConventionalShadowCasterCullingPass: failed to map count clear buffer");
  std::memset(state.count_clear_mapped_ptr, 0, clear_desc.size_bytes);

  auto& allocator = gfx->GetDescriptorAllocator();
  auto& registry = gfx->GetResourceRegistry();

  auto command_uav = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
    graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(command_uav.IsValid(),
    "Failed to allocate conventional caster-culling command UAV");
  state.command_uav_index = allocator.GetShaderVisibleIndex(command_uav);
  registry.RegisterView(*state.command_buffer, std::move(command_uav),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
      command_bytes, sizeof(ConventionalShadowIndirectDrawCommand)));

  auto command_srv = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(command_srv.IsValid(),
    "Failed to allocate conventional caster-culling command SRV");
  state.command_srv_index = allocator.GetShaderVisibleIndex(command_srv);
  registry.RegisterView(*state.command_buffer, std::move(command_srv),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      command_bytes, sizeof(ConventionalShadowIndirectDrawCommand)));

  auto count_uav = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
    graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(count_uav.IsValid(),
    "Failed to allocate conventional caster-culling count UAV");
  state.count_uav_index = allocator.GetShaderVisibleIndex(count_uav);
  registry.RegisterView(*state.count_buffer, std::move(count_uav),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV, count_bytes,
      sizeof(std::uint32_t)));

  auto count_srv = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(count_srv.IsValid(),
    "Failed to allocate conventional caster-culling count SRV");
  state.count_srv_index = allocator.GetShaderVisibleIndex(count_srv);
  registry.RegisterView(*state.count_buffer, std::move(count_srv),
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV, count_bytes,
      sizeof(std::uint32_t)));

  return state;
}

auto ConventionalShadowCasterCullingPass::DoPrepareResources(
  CommandRecorder& recorder) -> co::Co<>
{
  impl_->resources_prepared = false;
  impl_->active_view_state = nullptr;
  impl_->active_job_count = 0U;
  impl_->active_receiver_mask_summary_buffer.reset();
  impl_->active_receiver_mask_base_buffer.reset();
  impl_->active_receiver_mask_hierarchy_buffer.reset();
  impl_->active_partitions.clear();

  const auto* prepared_frame = Context().current_view.prepared_frame.get();
  if (prepared_frame == nullptr || !prepared_frame->IsValid()
    || prepared_frame->conventional_shadow_draw_records.empty()
    || !prepared_frame->bindless_conventional_shadow_draw_records_slot.IsValid()
    || !prepared_frame->bindless_draw_metadata_slot.IsValid()) {
    DLOG_F(2,
      "Conventional caster-culling pass skipped because the prepared frame is "
      "missing the authoritative conventional draw stream for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto* receiver_mask_pass
    = Context().GetPass<ConventionalShadowReceiverMaskPass>();
  if (receiver_mask_pass == nullptr) {
    DLOG_F(2,
      "Conventional caster-culling pass skipped because "
      "ConventionalShadowReceiverMaskPass is unavailable for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto receiver_mask_output
    = receiver_mask_pass->GetCurrentOutput(Context().current_view.view_id);
  if (!receiver_mask_output.available
    || receiver_mask_output.summary_buffer == nullptr
    || receiver_mask_output.base_mask_buffer == nullptr
    || receiver_mask_output.hierarchy_mask_buffer == nullptr
    || !receiver_mask_output.summary_srv_index.IsValid()
    || !receiver_mask_output.base_mask_srv_index.IsValid()
    || !receiver_mask_output.hierarchy_mask_srv_index.IsValid()
    || receiver_mask_output.job_count == 0U) {
    DLOG_F(2,
      "Conventional caster-culling pass skipped because receiver-mask output "
      "is unavailable for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    DLOG_F(2,
      "Conventional caster-culling pass skipped because ShadowManager is "
      "unavailable for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto* plan = shadow_manager->TryGetReceiverAnalysisPlan(
    Context().current_view.view_id);
  if (plan == nullptr || plan->jobs.empty()) {
    DLOG_F(2,
      "Conventional caster-culling pass skipped because no conventional "
      "receiver-analysis jobs were published for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  const auto job_count = static_cast<std::uint32_t>(plan->jobs.size());
  CHECK_F(job_count == receiver_mask_output.job_count,
    "ConventionalShadowCasterCullingPass job-count mismatch: plan={} mask={}",
    job_count, receiver_mask_output.job_count);

  impl_->active_partitions
    = CollectActiveShadowRecordPartitions(*prepared_frame);
  if (impl_->active_partitions.empty()) {
    DLOG_F(2,
      "Conventional caster-culling pass skipped because the authoritative "
      "conventional draw stream has no active partitions for view {}",
      Context().current_view.view_id.get());
    co_return;
  }

  impl_->EnsurePassConstantsBuffer();
  auto& view_state = impl_->EnsureViewResources(Context().current_view.view_id,
    job_count, static_cast<std::uint32_t>(impl_->active_partitions.size()));
  std::memcpy(view_state.job_upload_mapped_ptr, plan->jobs.data(),
    plan->jobs.size_bytes());
  view_state.job_count = job_count;
  view_state.has_current_output = false;
  view_state.inspection_partitions.clear();

  auto partition_upload
    = std::vector<ConventionalShadowCasterCullingPartition> {};
  partition_upload.reserve(impl_->active_partitions.size());
  for (std::size_t i = 0; i < impl_->active_partitions.size(); ++i) {
    const auto& partition = impl_->active_partitions[i];
    auto& partition_state
      = impl_->EnsurePartitionResources(view_state, i, partition);
    partition_upload.push_back(ConventionalShadowCasterCullingPartition {
      .record_begin = partition.record_begin,
      .record_count = partition.record_count,
      .command_uav_index = partition_state.command_uav_index.get(),
      .count_uav_index = partition_state.count_uav_index.get(),
      .max_commands_per_job = partition_state.max_commands_per_job,
      .partition_index = partition.partition_index,
      .pass_mask = partition.pass_mask,
    });

    view_state.inspection_partitions.push_back(IndirectPartitionInspection {
      .pass_mask = partition.pass_mask,
      .partition_index = partition.partition_index,
      .draw_record_count = partition.record_count,
      .max_commands_per_job = partition_state.max_commands_per_job,
      .command_buffer = partition_state.command_buffer.get(),
      .count_buffer = partition_state.count_buffer.get(),
    });
  }
  std::memcpy(view_state.partition_upload_mapped_ptr, partition_upload.data(),
    partition_upload.size() * sizeof(ConventionalShadowCasterCullingPartition));

  const auto constants = ConventionalShadowCasterCullingPassConstants {
    .draw_record_buffer_index
    = prepared_frame->bindless_conventional_shadow_draw_records_slot,
    .draw_metadata_index = prepared_frame->bindless_draw_metadata_slot,
    .job_buffer_index = view_state.job_srv_index,
    .receiver_mask_summary_index = receiver_mask_output.summary_srv_index,
    .receiver_mask_base_index = receiver_mask_output.base_mask_srv_index,
    .receiver_mask_hierarchy_index
    = receiver_mask_output.hierarchy_mask_srv_index,
    .partition_buffer_index = view_state.partition_srv_index,
    .job_count = job_count,
    .base_tile_resolution = receiver_mask_output.base_tile_resolution,
    .hierarchy_tile_resolution = receiver_mask_output.hierarchy_tile_resolution,
    .base_tiles_per_job = receiver_mask_output.base_tile_resolution
      * receiver_mask_output.base_tile_resolution,
    .hierarchy_tiles_per_job = receiver_mask_output.hierarchy_tile_resolution
      * receiver_mask_output.hierarchy_tile_resolution,
    .hierarchy_reduction = receiver_mask_output.hierarchy_reduction,
    .partition_count
    = static_cast<std::uint32_t>(impl_->active_partitions.size()),
  };
  std::memcpy(impl_->pass_constants_mapped_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(impl_->pass_constants_index);

  if (!recorder.IsResourceTracked(*view_state.job_upload_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.job_upload_buffer, ResourceStates::kGenericRead, true);
  }
  if (!recorder.IsResourceTracked(*view_state.job_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.job_buffer, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*view_state.partition_upload_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.partition_upload_buffer, ResourceStates::kGenericRead, true);
  }
  if (!recorder.IsResourceTracked(*view_state.partition_buffer)) {
    recorder.BeginTrackingResourceState(
      *view_state.partition_buffer, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*receiver_mask_output.summary_buffer)) {
    recorder.BeginTrackingResourceState(
      *std::const_pointer_cast<Buffer>(receiver_mask_output.summary_buffer),
      ResourceStates::kShaderResource, true);
  }
  if (!recorder.IsResourceTracked(*receiver_mask_output.base_mask_buffer)) {
    recorder.BeginTrackingResourceState(
      *std::const_pointer_cast<Buffer>(receiver_mask_output.base_mask_buffer),
      ResourceStates::kShaderResource, true);
  }
  if (!recorder.IsResourceTracked(
        *receiver_mask_output.hierarchy_mask_buffer)) {
    recorder.BeginTrackingResourceState(
      *std::const_pointer_cast<Buffer>(
        receiver_mask_output.hierarchy_mask_buffer),
      ResourceStates::kShaderResource, true);
  }

  for (std::size_t i = 0; i < impl_->active_partitions.size(); ++i) {
    auto& partition_state = view_state.partition_states[i];
    if (!recorder.IsResourceTracked(*partition_state.command_buffer)) {
      recorder.BeginTrackingResourceState(
        *partition_state.command_buffer, ResourceStates::kCommon, true);
    }
    if (!recorder.IsResourceTracked(*partition_state.count_buffer)) {
      recorder.BeginTrackingResourceState(
        *partition_state.count_buffer, ResourceStates::kCommon, true);
    }
    if (!recorder.IsResourceTracked(*partition_state.count_clear_buffer)) {
      recorder.BeginTrackingResourceState(*partition_state.count_clear_buffer,
        ResourceStates::kGenericRead, true);
    }
  }

  impl_->active_view_state = &view_state;
  impl_->active_view_id = Context().current_view.view_id;
  impl_->active_job_count = job_count;
  impl_->active_receiver_mask_summary_buffer
    = receiver_mask_output.summary_buffer;
  impl_->active_receiver_mask_base_buffer
    = receiver_mask_output.base_mask_buffer;
  impl_->active_receiver_mask_hierarchy_buffer
    = receiver_mask_output.hierarchy_mask_buffer;
  impl_->resources_prepared = true;

  DLOG_F(2,
    "Prepared conventional caster-culling pass view={} jobs={} partitions={} "
    "draw_records={}",
    impl_->active_view_id.get(), impl_->active_job_count,
    impl_->active_partitions.size(),
    prepared_frame->conventional_shadow_draw_records.size());

  co_return;
}

auto ConventionalShadowCasterCullingPass::DoExecute(CommandRecorder& recorder)
  -> co::Co<>
{
  if (!impl_->resources_prepared || impl_->active_view_state == nullptr
    || impl_->active_job_count == 0U || impl_->active_partitions.empty()
    || !impl_->culling_pso
    || impl_->active_receiver_mask_summary_buffer == nullptr
    || impl_->active_receiver_mask_base_buffer == nullptr
    || impl_->active_receiver_mask_hierarchy_buffer == nullptr) {
    DLOG_F(2, "Conventional caster-culling pass skipped execute");
    co_return;
  }

  auto& view_state = *impl_->active_view_state;

  recorder.RequireResourceState(
    *view_state.job_upload_buffer, ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *view_state.job_buffer, ResourceStates::kCopyDest);
  recorder.RequireResourceState(
    *view_state.partition_upload_buffer, ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *view_state.partition_buffer, ResourceStates::kCopyDest);
  for (std::size_t i = 0; i < impl_->active_partitions.size(); ++i) {
    auto& partition_state = view_state.partition_states[i];
    recorder.RequireResourceState(
      *partition_state.count_clear_buffer, ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *partition_state.count_buffer, ResourceStates::kCopyDest);
  }
  recorder.FlushBarriers();

  recorder.CopyBuffer(*view_state.job_buffer, 0U, *view_state.job_upload_buffer,
    0U,
    static_cast<std::uint64_t>(impl_->active_job_count)
      * sizeof(renderer::ConventionalShadowReceiverAnalysisJob));
  recorder.CopyBuffer(*view_state.partition_buffer, 0U,
    *view_state.partition_upload_buffer, 0U,
    static_cast<std::uint64_t>(impl_->active_partitions.size())
      * sizeof(ConventionalShadowCasterCullingPartition));
  for (std::size_t i = 0; i < impl_->active_partitions.size(); ++i) {
    auto& partition_state = view_state.partition_states[i];
    recorder.CopyBuffer(*partition_state.count_buffer, 0U,
      *partition_state.count_clear_buffer, 0U,
      static_cast<std::uint64_t>(impl_->active_job_count)
        * sizeof(std::uint32_t));
  }

  recorder.RequireResourceState(
    *view_state.job_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *view_state.partition_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(*std::const_pointer_cast<Buffer>(
                                  impl_->active_receiver_mask_summary_buffer),
    ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *std::const_pointer_cast<Buffer>(impl_->active_receiver_mask_base_buffer),
    ResourceStates::kShaderResource);
  recorder.RequireResourceState(*std::const_pointer_cast<Buffer>(
                                  impl_->active_receiver_mask_hierarchy_buffer),
    ResourceStates::kShaderResource);
  for (std::size_t i = 0; i < impl_->active_partitions.size(); ++i) {
    auto& partition_state = view_state.partition_states[i];
    recorder.RequireResourceState(
      *partition_state.command_buffer, ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(
      *partition_state.count_buffer, ResourceStates::kUnorderedAccess);
  }
  recorder.FlushBarriers();

  for (std::size_t i = 0; i < impl_->active_partitions.size(); ++i) {
    const auto& partition_state = view_state.partition_states[i];
    const auto dispatch_x
      = DivideRoundUp(partition_state.record_count, kThreadGroupSize);
    BindComputeStage(recorder, *impl_->culling_pso, GetPassConstantsIndex(),
      static_cast<std::uint32_t>(i), Context());
    recorder.Dispatch(dispatch_x, impl_->active_job_count, 1U);
  }

  for (std::size_t i = 0; i < impl_->active_partitions.size(); ++i) {
    auto& partition_state = view_state.partition_states[i];
    recorder.RequireResourceStateFinal(
      *partition_state.command_buffer, ResourceStates::kIndirectArgument);
    recorder.RequireResourceStateFinal(
      *partition_state.count_buffer, ResourceStates::kIndirectArgument);
  }
  recorder.RequireResourceStateFinal(
    *view_state.job_buffer, ResourceStates::kCommon);
  recorder.RequireResourceStateFinal(
    *view_state.partition_buffer, ResourceStates::kCommon);
  view_state.has_current_output = true;

  DLOG_F(2,
    "Executed conventional caster-culling pass view={} jobs={} partitions={}",
    impl_->active_view_id.get(), impl_->active_job_count,
    impl_->active_partitions.size());

  co_return;
}

auto ConventionalShadowCasterCullingPass::CreatePipelineStateDesc()
  -> ComputePipelineDesc
{
  const auto generated_bindings = BuildRootBindings();
  impl_->culling_pso
    = ComputePipelineDesc::Builder()
        .SetComputeShader({
          .stage = oxygen::ShaderType::kCompute,
          .source_path = "Renderer/ConventionalShadowCasterCulling.hlsl",
          .entry_point = "CS",
        })
        .SetRootBindings(std::span<const graphics::RootBindingItem>(
          generated_bindings.data(), generated_bindings.size()))
        .SetDebugName("ConventionalShadowCasterCulling_PSO")
        .Build();
  impl_->pipeline_ready = true;
  return *impl_->culling_pso;
}

auto ConventionalShadowCasterCullingPass::NeedRebuildPipelineState() const
  -> bool
{
  return !impl_->pipeline_ready;
}

} // namespace oxygen::engine
