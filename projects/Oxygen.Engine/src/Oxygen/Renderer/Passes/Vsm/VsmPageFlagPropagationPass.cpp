//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/Vsm/VsmPageFlagPropagationPass.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::ShaderVisibleIndex;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::ComputePipelineDesc;
using oxygen::graphics::DescriptorAllocationHandle;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::ResourceViewType;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmShaderPageHierarchyDispatch;

namespace oxygen::engine {

namespace {

  constexpr std::uint32_t kPropagationGroupSizeX = 8U;
  constexpr std::uint32_t kPropagationGroupSizeY = 8U;

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmPageFlagPropagationPassConstants {
    ShaderVisibleIndex dispatch_buffer_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_flags_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_table_srv_index { kInvalidShaderVisibleIndex };
    std::uint32_t _pad0 { 0U };
  };
  static_assert(sizeof(VsmPageFlagPropagationPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

  struct DispatchRecord {
    VsmShaderPageHierarchyDispatch gpu {};
    std::uint32_t group_count_x { 0U };
    std::uint32_t group_count_y { 0U };
  };

  auto MakeDispatchGroups(const std::uint32_t count,
    const std::uint32_t group_size) noexcept -> std::uint32_t
  {
    return count == 0U ? 0U : (count + group_size - 1U) / group_size;
  }

  auto ReduceLevelDim(const std::uint32_t base_dim,
    const std::uint32_t level) noexcept -> std::uint32_t
  {
    auto dim = base_dim;
    for (std::uint32_t i = 0U; i < level && dim > 1U; ++i) {
      dim = (dim + 1U) >> 1U;
    }
    return std::max(dim, 1U);
  }

  auto BuildBufferViewDesc(const ResourceViewType view_type,
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

  auto BindComputeStage(CommandRecorder& recorder,
    const graphics::ComputePipelineDesc& pso_desc,
    const ShaderVisibleIndex pass_constants_index,
    const std::uint32_t dispatch_index, const RenderContext& context) -> void
  {
    recorder.SetPipelineState(pso_desc);
    DCHECK_NOTNULL_F(context.view_constants);
    recorder.SetComputeRootConstantBufferView(
      static_cast<std::uint32_t>(
        oxygen::bindless::generated::d3d12::RootParam::kViewConstants),
      context.view_constants->GetGPUVirtualAddress());
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(
        oxygen::bindless::generated::d3d12::RootParam::kRootConstants),
      dispatch_index, 0U);
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(
        oxygen::bindless::generated::d3d12::RootParam::kRootConstants),
      pass_constants_index.get(), 1U);
  }

  auto RequireValidIndex(
    const ShaderVisibleIndex index, const std::string_view label) -> bool
  {
    if (index.IsValid()) {
      return true;
    }

    LOG_F(ERROR,
      "VSM page-flag propagation pass skipped because {} is unavailable",
      label);
    return false;
  }

  auto AppendDispatches(std::vector<DispatchRecord>& dispatches,
    const std::uint32_t first_page_table_entry, const std::uint32_t pages_x,
    const std::uint32_t pages_y, const std::uint32_t level_count) -> void
  {
    if (pages_x == 0U || pages_y == 0U || level_count <= 1U) {
      return;
    }

    const auto pages_per_level = pages_x * pages_y;
    for (std::uint32_t target_level = 1U; target_level < level_count;
      ++target_level) {
      const auto source_level = target_level - 1U;
      const auto source_pages_x = ReduceLevelDim(pages_x, source_level);
      const auto source_pages_y = ReduceLevelDim(pages_y, source_level);
      dispatches.push_back(DispatchRecord {
        .gpu = VsmShaderPageHierarchyDispatch {
          .first_page_table_entry = first_page_table_entry,
          .level0_pages_x = pages_x,
          .level0_pages_y = pages_y,
          .pages_per_level = pages_per_level,
          .source_level = source_level,
          .target_level = target_level,
          .source_pages_x = source_pages_x,
          .source_pages_y = source_pages_y,
        },
        .group_count_x
        = MakeDispatchGroups(source_pages_x, kPropagationGroupSizeX),
        .group_count_y
        = MakeDispatchGroups(source_pages_y, kPropagationGroupSizeY),
      });
    }
  }

} // namespace

struct VsmPageFlagPropagationPass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;

  std::optional<VsmPageAllocationFrame> frame_input {};
  bool resources_prepared { false };
  std::vector<DispatchRecord> dispatches {};

  std::shared_ptr<Buffer> dispatch_buffer {};
  std::shared_ptr<Buffer> dispatch_upload_buffer {};
  void* dispatch_upload_ptr { nullptr };
  std::uint32_t dispatch_capacity { 0U };

  std::shared_ptr<Buffer> constants_buffer {};
  void* constants_ptr { nullptr };

  DescriptorAllocationHandle dispatch_srv_handle {};
  DescriptorAllocationHandle constants_cbv_handle {};
  DescriptorAllocationHandle page_flags_uav_handle {};
  DescriptorAllocationHandle page_table_srv_handle {};

  ShaderVisibleIndex dispatch_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex constants_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_flags_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_table_srv { kInvalidShaderVisibleIndex };

  const Buffer* dispatch_owner { nullptr };
  const Buffer* page_flags_owner { nullptr };
  const Buffer* page_table_owner { nullptr };

  std::optional<graphics::ComputePipelineDesc> hierarchical_pso {};
  std::optional<graphics::ComputePipelineDesc> mapped_mips_pso {};

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl();

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  auto EnsureMappedUploadBuffer(std::uint32_t required_count) -> void;
  auto EnsureConstantsBuffer() -> void;
  auto EnsureBufferView(Buffer& buffer,
    const graphics::BufferViewDescription& desc,
    DescriptorAllocationHandle& handle, ShaderVisibleIndex& index,
    const Buffer*& owner) -> ShaderVisibleIndex;
  auto BuildDispatches() -> void;
};

VsmPageFlagPropagationPass::Impl::~Impl()
{
  if (dispatch_upload_buffer != nullptr && dispatch_upload_ptr != nullptr) {
    dispatch_upload_buffer->UnMap();
    dispatch_upload_ptr = nullptr;
  }
  if (constants_buffer != nullptr && constants_ptr != nullptr) {
    constants_buffer->UnMap();
    constants_ptr = nullptr;
  }
}

auto VsmPageFlagPropagationPass::Impl::EnsureMappedUploadBuffer(
  const std::uint32_t required_count) -> void
{
  if (required_count <= dispatch_capacity && dispatch_buffer != nullptr
    && dispatch_upload_buffer != nullptr && dispatch_upload_ptr != nullptr) {
    return;
  }

  if (dispatch_upload_buffer != nullptr && dispatch_upload_ptr != nullptr) {
    dispatch_upload_buffer->UnMap();
    dispatch_upload_ptr = nullptr;
  }

  dispatch_capacity = std::max(required_count, 1U);
  const auto size_bytes = static_cast<std::uint64_t>(dispatch_capacity)
    * sizeof(VsmShaderPageHierarchyDispatch);

  dispatch_buffer = gfx->CreateBuffer(BufferDesc {
    .size_bytes = size_bytes,
    .usage = BufferUsage::kStorage,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = config->debug_name + ".Dispatches",
  });
  CHECK_NOTNULL_F(
    dispatch_buffer.get(), "Failed to create propagation dispatch buffer");

  dispatch_upload_buffer = gfx->CreateBuffer(BufferDesc {
    .size_bytes = size_bytes,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = config->debug_name + ".Dispatches.Upload",
  });
  CHECK_NOTNULL_F(dispatch_upload_buffer.get(),
    "Failed to create propagation dispatch upload buffer");
  dispatch_upload_ptr = dispatch_upload_buffer->Map(0U, size_bytes);
  CHECK_NOTNULL_F(
    dispatch_upload_ptr, "Failed to map propagation dispatch upload buffer");
}

auto VsmPageFlagPropagationPass::Impl::EnsureConstantsBuffer() -> void
{
  if (constants_buffer == nullptr) {
    constants_buffer = gfx->CreateBuffer(BufferDesc {
      .size_bytes = 256U,
      .usage = BufferUsage::kConstant,
      .memory = BufferMemory::kUpload,
      .debug_name = config->debug_name + ".Constants",
    });
    CHECK_NOTNULL_F(
      constants_buffer.get(), "Failed to create propagation constants buffer");
    constants_ptr = constants_buffer->Map(0U, 256U);
    CHECK_NOTNULL_F(
      constants_ptr, "Failed to map propagation constants buffer");
  }

  if (!constants_cbv_handle.IsValid()) {
    auto& allocator = gfx->GetDescriptorAllocator();
    constants_cbv_handle
      = allocator.AllocateRaw(ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(constants_cbv_handle.IsValid(),
      "Failed to allocate propagation constants CBV");
    constants_index = allocator.GetShaderVisibleIndex(constants_cbv_handle);
    const auto desc
      = BuildBufferViewDesc(ResourceViewType::kConstantBuffer, 256U, 0U);
    const auto view
      = constants_buffer->GetNativeView(constants_cbv_handle, desc);
    CHECK_F(view != graphics::NativeView {},
      "Failed to create propagation constants CBV");
  }
}

auto VsmPageFlagPropagationPass::Impl::EnsureBufferView(Buffer& buffer,
  const graphics::BufferViewDescription& desc,
  DescriptorAllocationHandle& handle, ShaderVisibleIndex& index,
  const Buffer*& owner) -> ShaderVisibleIndex
{
  if (handle.IsValid() && owner == &buffer) {
    return index;
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  handle = allocator.AllocateRaw(desc.view_type, desc.visibility);
  if (!handle.IsValid()) {
    LOG_F(ERROR, "Failed to allocate VSM propagation buffer view");
    return kInvalidShaderVisibleIndex;
  }

  const auto view = buffer.GetNativeView(handle, desc);
  if (view == graphics::NativeView {}) {
    LOG_F(ERROR, "Failed to create VSM propagation buffer view");
    handle = {};
    index = kInvalidShaderVisibleIndex;
    owner = nullptr;
    return index;
  }

  index = allocator.GetShaderVisibleIndex(handle);
  owner = &buffer;
  return index;
}

auto VsmPageFlagPropagationPass::Impl::BuildDispatches() -> void
{
  CHECK_F(frame_input.has_value(), "BuildDispatches requires frame input");

  dispatches.clear();
  for (const auto& layout :
    frame_input->snapshot.virtual_frame.local_light_layouts) {
    AppendDispatches(dispatches, layout.first_page_table_entry,
      layout.pages_per_level_x, layout.pages_per_level_y, layout.level_count);
  }
  for (const auto& layout :
    frame_input->snapshot.virtual_frame.directional_layouts) {
    AppendDispatches(dispatches, layout.first_page_table_entry,
      layout.pages_per_axis, layout.pages_per_axis, layout.clip_level_count);
  }
}

VsmPageFlagPropagationPass::VsmPageFlagPropagationPass(
  observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : RenderPass(config ? config->debug_name : "VsmPageFlagPropagationPass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
}

VsmPageFlagPropagationPass::~VsmPageFlagPropagationPass() = default;

auto VsmPageFlagPropagationPass::SetFrameInput(VsmPageAllocationFrame frame)
  -> void
{
  impl_->frame_input = std::move(frame);
}

auto VsmPageFlagPropagationPass::ResetFrameInput() noexcept -> void
{
  impl_->frame_input.reset();
}

auto VsmPageFlagPropagationPass::ValidateConfig() -> void
{
  if (impl_->gfx == nullptr) {
    throw std::runtime_error("VsmPageFlagPropagationPass requires Graphics");
  }
  if (impl_->config == nullptr) {
    throw std::runtime_error("VsmPageFlagPropagationPass requires Config");
  }
}

auto VsmPageFlagPropagationPass::OnPrepareResources(
  CommandRecorder& /*recorder*/) -> void
{
  auto root_bindings = BuildRootBindings();

  if (!impl_->hierarchical_pso.has_value()) {
    impl_->hierarchical_pso
      = ComputePipelineDesc::Builder()
          .SetComputeShader(graphics::ShaderRequest {
            .stage = ShaderType::kCompute,
            .source_path = "Renderer/Vsm/VsmGenerateHierarchicalFlags.hlsl",
            .entry_point = "CS",
          })
          .SetRootBindings(std::span<const graphics::RootBindingItem>(
            root_bindings.data(), root_bindings.size()))
          .SetDebugName("VsmGenerateHierarchicalFlags_PSO")
          .Build();
  }

  if (!impl_->mapped_mips_pso.has_value()) {
    impl_->mapped_mips_pso
      = ComputePipelineDesc::Builder()
          .SetComputeShader(graphics::ShaderRequest {
            .stage = ShaderType::kCompute,
            .source_path = "Renderer/Vsm/VsmPropagateMappedMips.hlsl",
            .entry_point = "CS",
          })
          .SetRootBindings(std::span<const graphics::RootBindingItem>(
            root_bindings.data(), root_bindings.size()))
          .SetDebugName("VsmPropagateMappedMips_PSO")
          .Build();
  }
}

auto VsmPageFlagPropagationPass::OnExecute(CommandRecorder& /*recorder*/)
  -> void
{
}

auto VsmPageFlagPropagationPass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  impl_->resources_prepared = false;

  if (!impl_->frame_input.has_value()) {
    LOG_F(WARNING,
      "VSM page-flag propagation pass skipped because frame input is "
      "unavailable");
    co_return;
  }
  if (!impl_->frame_input->is_ready) {
    LOG_F(WARNING,
      "VSM page-flag propagation pass skipped because frame input is not "
      "ready");
    co_return;
  }
  if (impl_->frame_input->page_flags_buffer == nullptr
    || impl_->frame_input->page_table_buffer == nullptr) {
    LOG_F(WARNING,
      "VSM page-flag propagation pass skipped because required GPU buffers are "
      "unavailable");
    co_return;
  }

  impl_->BuildDispatches();
  if (impl_->dispatches.empty()) {
    DLOG_F(2,
      "VSM page-flag propagation pass found no multi-level virtual maps to "
      "propagate");
    impl_->resources_prepared = true;
    co_return;
  }

  impl_->EnsureMappedUploadBuffer(
    static_cast<std::uint32_t>(impl_->dispatches.size()));
  impl_->EnsureConstantsBuffer();

  auto gpu_dispatches = std::vector<VsmShaderPageHierarchyDispatch> {};
  gpu_dispatches.reserve(impl_->dispatches.size());
  for (const auto& dispatch : impl_->dispatches) {
    gpu_dispatches.push_back(dispatch.gpu);
  }
  std::memcpy(impl_->dispatch_upload_ptr, gpu_dispatches.data(),
    gpu_dispatches.size() * sizeof(VsmShaderPageHierarchyDispatch));

  auto page_flags_buffer
    = std::const_pointer_cast<Buffer>(impl_->frame_input->page_flags_buffer);
  auto page_table_buffer
    = std::const_pointer_cast<Buffer>(impl_->frame_input->page_table_buffer);

  const auto constants = VsmPageFlagPropagationPassConstants {
    .dispatch_buffer_index = impl_->EnsureBufferView(*impl_->dispatch_buffer,
      BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_SRV,
        impl_->dispatch_buffer->GetDescriptor().size_bytes,
        sizeof(VsmShaderPageHierarchyDispatch)),
      impl_->dispatch_srv_handle, impl_->dispatch_srv, impl_->dispatch_owner),
    .page_flags_uav_index = impl_->EnsureBufferView(*page_flags_buffer,
      BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_UAV,
        page_flags_buffer->GetDescriptor().size_bytes, sizeof(std::uint32_t)),
      impl_->page_flags_uav_handle, impl_->page_flags_uav,
      impl_->page_flags_owner),
    .page_table_srv_index = impl_->EnsureBufferView(*page_table_buffer,
      BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_SRV,
        page_table_buffer->GetDescriptor().size_bytes, sizeof(std::uint32_t)),
      impl_->page_table_srv_handle, impl_->page_table_srv,
      impl_->page_table_owner),
  };
  std::memcpy(impl_->constants_ptr, &constants, sizeof(constants));

  if (!RequireValidIndex(constants.dispatch_buffer_index, "dispatch SRV")
    || !RequireValidIndex(constants.page_flags_uav_index, "page-flags UAV")
    || !RequireValidIndex(constants.page_table_srv_index, "page-table SRV")) {
    co_return;
  }

  recorder.BeginTrackingResourceState(
    *impl_->constants_buffer, ResourceStates::kGenericRead, true);
  recorder.BeginTrackingResourceState(
    *impl_->dispatch_buffer, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *impl_->dispatch_upload_buffer, ResourceStates::kGenericRead, true);
  recorder.BeginTrackingResourceState(
    *page_flags_buffer, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *page_table_buffer, ResourceStates::kCommon, true);

  recorder.RequireResourceState(
    *impl_->dispatch_upload_buffer, ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *impl_->dispatch_buffer, ResourceStates::kCopyDest);
  recorder.FlushBarriers();

  recorder.CopyBuffer(*impl_->dispatch_buffer, 0U,
    *impl_->dispatch_upload_buffer, 0U,
    gpu_dispatches.size() * sizeof(VsmShaderPageHierarchyDispatch));

  recorder.RequireResourceState(
    *impl_->dispatch_buffer, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *page_flags_buffer, ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *page_table_buffer, ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  DLOG_F(2,
    "prepared VSM page-flag propagation pass generation={} dispatches={}",
    impl_->frame_input->snapshot.frame_generation, impl_->dispatches.size());
  impl_->resources_prepared = true;
  co_return;
}

auto VsmPageFlagPropagationPass::DoExecute(CommandRecorder& recorder)
  -> co::Co<>
{
  if (!impl_->resources_prepared || !impl_->frame_input.has_value()) {
    DLOG_F(
      2, "VSM page-flag propagation resources were not prepared, skipping");
    co_return;
  }

  if (impl_->dispatches.empty()) {
    co_return;
  }

  auto page_flags_buffer
    = std::const_pointer_cast<Buffer>(impl_->frame_input->page_flags_buffer);

  for (std::uint32_t i = 0; i < impl_->dispatches.size(); ++i) {
    const auto& dispatch = impl_->dispatches[i];
    BindComputeStage(
      recorder, *impl_->hierarchical_pso, impl_->constants_index, i, Context());
    recorder.Dispatch(dispatch.group_count_x, dispatch.group_count_y, 1U);
    recorder.RequireResourceState(
      *page_flags_buffer, ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();
  }

  for (std::uint32_t i = 0; i < impl_->dispatches.size(); ++i) {
    const auto& dispatch = impl_->dispatches[i];
    BindComputeStage(
      recorder, *impl_->mapped_mips_pso, impl_->constants_index, i, Context());
    recorder.Dispatch(dispatch.group_count_x, dispatch.group_count_y, 1U);
    recorder.RequireResourceState(
      *page_flags_buffer, ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();
  }

  recorder.RequireResourceState(
    *page_flags_buffer, ResourceStates::kShaderResource);
  recorder.FlushBarriers();
  co_return;
}

} // namespace oxygen::engine
