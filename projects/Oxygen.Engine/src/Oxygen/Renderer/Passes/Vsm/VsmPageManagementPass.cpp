//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/Vsm/VsmPageManagementPass.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string_view>
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
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPageAllocationSnapshotHelpers.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::ShaderVisibleIndex;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::ComputePipelineDesc;
using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::ResourceViewType;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmPageAllocationDecision;
using oxygen::renderer::vsm::VsmPageAllocationFrame;
using oxygen::renderer::vsm::VsmPageRequestFlags;
using oxygen::renderer::vsm::VsmShaderPageAllocationDecision;
using oxygen::renderer::vsm::VsmShaderPageFlagBits;
using oxygen::renderer::vsm::VsmShaderPageFlags;
using oxygen::renderer::vsm::VsmShaderPageReuseDecision;

namespace oxygen::engine {

namespace {

  constexpr std::uint32_t kDecisionThreadGroupSize = 64U;

  struct alignas(packing::kShaderDataFieldAlignment) VsmPageReusePassConstants {
    ShaderVisibleIndex reuse_decision_buffer_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex metadata_seed_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_table_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_flags_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex metadata_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t reuse_decision_count { 0U };
    std::uint32_t virtual_page_count { 0U };
    std::uint32_t physical_page_count { 0U };
  };
  static_assert(
    sizeof(VsmPageReusePassConstants) % packing::kShaderDataFieldAlignment
    == 0U);

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmPackAvailablePagesPassConstants {
    ShaderVisibleIndex metadata_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex available_pages_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex available_page_count_uav_index {
      kInvalidShaderVisibleIndex
    };
    std::uint32_t physical_page_count { 0U };
  };
  static_assert(sizeof(VsmPackAvailablePagesPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmAllocateNewPagesPassConstants {
    ShaderVisibleIndex allocation_decision_buffer_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex available_pages_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex available_page_count_srv_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex metadata_seed_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_table_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_flags_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex metadata_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t allocation_decision_count { 0U };
    std::uint32_t virtual_page_count { 0U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };
  };
  static_assert(sizeof(VsmAllocateNewPagesPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

  auto IsDynamicRequest(const VsmPageRequestFlags flags) noexcept -> bool
  {
    return (flags & VsmPageRequestFlags::kStaticOnly)
      != VsmPageRequestFlags::kStaticOnly;
  }

  auto BuildLeafPageFlags(const VsmPageAllocationDecision& decision)
    -> VsmShaderPageFlags
  {
    auto bits = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kAllocated);
    const auto dynamic_request = IsDynamicRequest(decision.request.flags);

    switch (decision.action) {
    case VsmAllocationAction::kReuseExisting:
      break;
    case VsmAllocationAction::kInitializeOnly:
      if (dynamic_request) {
        bits |= static_cast<std::uint32_t>(
          VsmShaderPageFlagBits::kDynamicUncached);
      } else {
        bits
          |= static_cast<std::uint32_t>(VsmShaderPageFlagBits::kStaticUncached);
      }
      break;
    case VsmAllocationAction::kAllocateNew:
      bits
        |= static_cast<std::uint32_t>(VsmShaderPageFlagBits::kStaticUncached);
      if (dynamic_request) {
        bits |= static_cast<std::uint32_t>(
          VsmShaderPageFlagBits::kDynamicUncached);
      }
      break;
    case VsmAllocationAction::kEvict:
    case VsmAllocationAction::kReject:
      break;
    }

    return VsmShaderPageFlags { .bits = bits };
  }

  auto MakeDecisionGroups(const std::uint32_t count) noexcept -> std::uint32_t
  {
    return count == 0U
      ? 0U
      : (count + kDecisionThreadGroupSize - 1U) / kDecisionThreadGroupSize;
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
    const ShaderVisibleIndex pass_constants_index, const RenderContext& context)
    -> void
  {
    recorder.SetPipelineState(pso_desc);
    DCHECK_NOTNULL_F(context.view_constants);
    recorder.SetComputeRootConstantBufferView(
      static_cast<std::uint32_t>(binding::RootParam::kViewConstants),
      context.view_constants->GetGPUVirtualAddress());
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(binding::RootParam::kRootConstants), 0U, 0U);
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(binding::RootParam::kRootConstants),
      pass_constants_index.get(), 1U);
  }

  auto RequireValidIndex(
    const ShaderVisibleIndex index, const std::string_view label) -> bool
  {
    if (index.IsValid()) {
      return true;
    }

    LOG_F(ERROR, "VSM page-management pass skipped because {} is unavailable",
      label);
    return false;
  }

} // namespace

struct VsmPageManagementPass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;

  std::optional<VsmPageAllocationFrame> frame_input {};
  bool resources_prepared { false };

  std::vector<VsmShaderPageReuseDecision> reuse_decisions {};
  std::vector<VsmShaderPageAllocationDecision> allocation_decisions {};

  std::shared_ptr<Buffer> clear_buffer {};
  void* clear_mapped_ptr { nullptr };
  std::uint64_t clear_buffer_capacity { 0U };

  std::shared_ptr<Buffer> reuse_decision_buffer {};
  std::shared_ptr<Buffer> reuse_decision_upload_buffer {};
  void* reuse_decision_upload_ptr { nullptr };
  std::uint32_t reuse_decision_capacity { 0U };

  std::shared_ptr<Buffer> allocation_decision_buffer {};
  std::shared_ptr<Buffer> allocation_decision_upload_buffer {};
  void* allocation_decision_upload_ptr { nullptr };
  std::uint32_t allocation_decision_capacity { 0U };

  std::shared_ptr<Buffer> available_page_count_buffer {};

  std::shared_ptr<Buffer> reuse_constants_buffer {};
  std::shared_ptr<Buffer> pack_constants_buffer {};
  std::shared_ptr<Buffer> allocate_constants_buffer {};
  void* reuse_constants_ptr { nullptr };
  void* pack_constants_ptr { nullptr };
  void* allocate_constants_ptr { nullptr };

  DescriptorHandle reuse_decision_srv_handle {};
  DescriptorHandle allocation_decision_srv_handle {};
  DescriptorHandle available_page_count_uav_handle {};
  DescriptorHandle available_page_count_srv_handle {};
  DescriptorHandle reuse_constants_cbv_handle {};
  DescriptorHandle pack_constants_cbv_handle {};
  DescriptorHandle allocate_constants_cbv_handle {};
  DescriptorHandle page_table_uav_handle {};
  DescriptorHandle page_flags_uav_handle {};
  DescriptorHandle metadata_uav_handle {};
  DescriptorHandle metadata_seed_srv_handle {};
  DescriptorHandle available_pages_uav_handle {};
  DescriptorHandle available_pages_srv_handle {};

  ShaderVisibleIndex reuse_decision_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex allocation_decision_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex available_page_count_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex available_page_count_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex reuse_constants_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex pack_constants_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex allocate_constants_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_table_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_flags_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex metadata_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex metadata_seed_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex available_pages_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex available_pages_srv { kInvalidShaderVisibleIndex };

  const Buffer* page_table_owner { nullptr };
  const Buffer* page_flags_owner { nullptr };
  const Buffer* metadata_owner { nullptr };
  const Buffer* metadata_seed_owner { nullptr };
  const Buffer* available_pages_owner { nullptr };
  const Buffer* reuse_decision_owner { nullptr };
  const Buffer* allocation_decision_owner { nullptr };
  const Buffer* available_count_owner { nullptr };

  std::optional<graphics::ComputePipelineDesc> reuse_pso {};
  std::optional<graphics::ComputePipelineDesc> pack_pso {};
  std::optional<graphics::ComputePipelineDesc> allocate_pso {};

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl();

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  auto EnsureMappedUploadBuffer(std::shared_ptr<Buffer>& device_buffer,
    std::shared_ptr<Buffer>& upload_buffer, void*& mapped_ptr,
    std::uint32_t& capacity, std::uint32_t required_count, std::uint64_t stride,
    const std::string& base_name) -> void;
  auto EnsureClearBuffer(std::uint64_t required_size) -> void;
  auto EnsureAvailablePageCountBuffer() -> void;
  auto EnsureConstantsBuffer(std::shared_ptr<Buffer>& buffer, void*& mapped_ptr,
    DescriptorHandle& handle, ShaderVisibleIndex& index,
    const std::string& debug_name) -> void;
  auto EnsureBufferView(Buffer& buffer,
    const graphics::BufferViewDescription& desc, DescriptorHandle& handle,
    ShaderVisibleIndex& index, const Buffer*& owner) -> ShaderVisibleIndex;
  auto BuildStageUploads() -> void;
};

VsmPageManagementPass::Impl::~Impl()
{
  const auto unmap_if_needed
    = [](const std::shared_ptr<Buffer>& buffer, void*& ptr) {
        if (buffer != nullptr && ptr != nullptr) {
          buffer->UnMap();
          ptr = nullptr;
        }
      };

  unmap_if_needed(clear_buffer, clear_mapped_ptr);
  unmap_if_needed(reuse_decision_upload_buffer, reuse_decision_upload_ptr);
  unmap_if_needed(
    allocation_decision_upload_buffer, allocation_decision_upload_ptr);
  unmap_if_needed(reuse_constants_buffer, reuse_constants_ptr);
  unmap_if_needed(pack_constants_buffer, pack_constants_ptr);
  unmap_if_needed(allocate_constants_buffer, allocate_constants_ptr);
}

auto VsmPageManagementPass::Impl::EnsureMappedUploadBuffer(
  std::shared_ptr<Buffer>& device_buffer,
  std::shared_ptr<Buffer>& upload_buffer, void*& mapped_ptr,
  std::uint32_t& capacity, const std::uint32_t required_count,
  const std::uint64_t stride, const std::string& base_name) -> void
{
  if (required_count <= capacity && device_buffer != nullptr
    && upload_buffer != nullptr && mapped_ptr != nullptr) {
    return;
  }

  if (upload_buffer != nullptr && mapped_ptr != nullptr) {
    upload_buffer->UnMap();
    mapped_ptr = nullptr;
  }

  capacity = std::max(required_count, 1U);
  const auto size_bytes = static_cast<std::uint64_t>(capacity) * stride;

  device_buffer = gfx->CreateBuffer(BufferDesc {
    .size_bytes = size_bytes,
    .usage = BufferUsage::kStorage,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = base_name,
  });
  CHECK_NOTNULL_F(device_buffer.get(), "Failed to create {}", base_name);

  upload_buffer = gfx->CreateBuffer(BufferDesc {
    .size_bytes = size_bytes,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = base_name + ".Upload",
  });
  CHECK_NOTNULL_F(
    upload_buffer.get(), "Failed to create {} upload buffer", base_name);
  mapped_ptr = upload_buffer->Map(0U, size_bytes);
  CHECK_NOTNULL_F(mapped_ptr, "Failed to map {} upload buffer", base_name);
}

auto VsmPageManagementPass::Impl::EnsureClearBuffer(
  const std::uint64_t required_size) -> void
{
  if (required_size <= clear_buffer_capacity && clear_buffer != nullptr
    && clear_mapped_ptr != nullptr) {
    return;
  }

  if (clear_buffer != nullptr && clear_mapped_ptr != nullptr) {
    clear_buffer->UnMap();
    clear_mapped_ptr = nullptr;
  }

  clear_buffer_capacity = std::max<std::uint64_t>(required_size, 1U);
  clear_buffer = gfx->CreateBuffer(BufferDesc {
    .size_bytes = clear_buffer_capacity,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = config->debug_name + ".ZeroFill",
  });
  CHECK_NOTNULL_F(clear_buffer.get(), "Failed to create zero-fill buffer");
  clear_mapped_ptr = clear_buffer->Map(0U, clear_buffer_capacity);
  CHECK_NOTNULL_F(clear_mapped_ptr, "Failed to map zero-fill buffer");
  std::memset(clear_mapped_ptr, 0, clear_buffer_capacity);
}

auto VsmPageManagementPass::Impl::EnsureAvailablePageCountBuffer() -> void
{
  if (available_page_count_buffer != nullptr) {
    return;
  }

  available_page_count_buffer = gfx->CreateBuffer(BufferDesc {
    .size_bytes = sizeof(std::uint32_t),
    .usage = BufferUsage::kStorage,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = config->debug_name + ".AvailablePageCount",
  });
  CHECK_NOTNULL_F(available_page_count_buffer.get(),
    "Failed to create available-page-count buffer");
}

auto VsmPageManagementPass::Impl::EnsureConstantsBuffer(
  std::shared_ptr<Buffer>& buffer, void*& mapped_ptr, DescriptorHandle& handle,
  ShaderVisibleIndex& index, const std::string& debug_name) -> void
{
  if (buffer == nullptr) {
    buffer = gfx->CreateBuffer(BufferDesc {
      .size_bytes = 256U,
      .usage = BufferUsage::kConstant,
      .memory = BufferMemory::kUpload,
      .debug_name = debug_name,
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create {}", debug_name);
    mapped_ptr = buffer->Map(0U, 256U);
    CHECK_NOTNULL_F(mapped_ptr, "Failed to map {}", debug_name);
  }

  if (!handle.IsValid()) {
    auto& allocator = gfx->GetDescriptorAllocator();
    handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(), "Failed to allocate {}", debug_name);
    index = allocator.GetShaderVisibleIndex(handle);
    const auto desc
      = BuildBufferViewDesc(ResourceViewType::kConstantBuffer, 256U, 0U);
    const auto view = buffer->GetNativeView(handle, desc);
    CHECK_F(view != graphics::NativeView {}, "Failed to create {}", debug_name);
  }
}

auto VsmPageManagementPass::Impl::EnsureBufferView(Buffer& buffer,
  const graphics::BufferViewDescription& desc, DescriptorHandle& handle,
  ShaderVisibleIndex& index, const Buffer*& owner) -> ShaderVisibleIndex
{
  if (handle.IsValid() && owner == &buffer) {
    return index;
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  handle = allocator.Allocate(desc.view_type, desc.visibility);
  if (!handle.IsValid()) {
    LOG_F(ERROR, "Failed to allocate VSM page-management buffer view");
    return kInvalidShaderVisibleIndex;
  }

  const auto view = buffer.GetNativeView(handle, desc);
  if (view == graphics::NativeView {}) {
    LOG_F(ERROR, "Failed to create VSM page-management buffer view");
    handle = {};
    index = kInvalidShaderVisibleIndex;
    owner = nullptr;
    return index;
  }

  index = allocator.GetShaderVisibleIndex(handle);
  owner = &buffer;
  return index;
}

auto VsmPageManagementPass::Impl::BuildStageUploads() -> void
{
  CHECK_F(frame_input.has_value(), "BuildStageUploads requires frame input");

  reuse_decisions.clear();
  allocation_decisions.clear();

  const auto append_retained_mapping
    = [&](const std::uint32_t page_table_index,
        const renderer::vsm::VsmPageTableEntry& mapping) {
        if (!mapping.is_mapped) {
          return;
        }

        CHECK_F(page_table_index < frame_input->snapshot.page_table.size(),
          "Retained page-table index {} exceeds snapshot size {}",
          page_table_index, frame_input->snapshot.page_table.size());
        CHECK_F(mapping.physical_page.value
            < frame_input->snapshot.physical_pages.size(),
          "Retained physical page {} exceeds snapshot capacity {}",
          mapping.physical_page.value,
          frame_input->snapshot.physical_pages.size());

        reuse_decisions.push_back(VsmShaderPageReuseDecision {
      .page_table_index = page_table_index,
      .physical_page_index = mapping.physical_page.value,
      .page_flags = VsmShaderPageFlags {
        .bits = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kAllocated),
      },
      .physical_meta
      = frame_input->snapshot.physical_pages[mapping.physical_page.value],
    });
      };

  const auto append_retained_range
    = [&](const std::uint32_t first_page_table_entry,
        const std::uint32_t page_count) {
        CHECK_F(first_page_table_entry + page_count
            <= frame_input->snapshot.page_table.size(),
          "Retained page-table range [{}:{}) exceeds snapshot size {}",
          first_page_table_entry, first_page_table_entry + page_count,
          frame_input->snapshot.page_table.size());
        for (std::uint32_t i = 0U; i < page_count; ++i) {
          append_retained_mapping(first_page_table_entry + i,
            frame_input->snapshot.page_table[first_page_table_entry + i]);
        }
      };

  for (const auto& retained_layout :
    frame_input->snapshot.retained_local_light_layouts) {
    append_retained_range(
      retained_layout.first_page_table_entry, retained_layout.total_page_count);
  }
  for (const auto& retained_layout :
    frame_input->snapshot.retained_directional_layouts) {
    append_retained_range(retained_layout.first_page_table_entry,
      renderer::vsm::TotalPageCount(retained_layout));
  }

  std::uint32_t next_available_page_list_index = 0U;
  for (const auto& decision : frame_input->plan.decisions) {
    switch (decision.action) {
    case VsmAllocationAction::kReuseExisting:
    case VsmAllocationAction::kInitializeOnly: {
      CHECK_F(decision.current_physical_page.value
          < frame_input->snapshot.physical_pages.size(),
        "Reuse decision physical page {} exceeds snapshot capacity {}",
        decision.current_physical_page.value,
        frame_input->snapshot.physical_pages.size());
      const auto page_table_index
        = renderer::vsm::detail::TryResolvePageTableEntryIndex(
          frame_input->snapshot.virtual_frame, decision.request);
      CHECK_F(page_table_index.has_value(),
        "Failed to resolve page-table index for reuse decision map_id={} "
        "level={} page=({}, {})",
        decision.request.map_id, decision.request.page.level,
        decision.request.page.page_x, decision.request.page.page_y);

      reuse_decisions.push_back(VsmShaderPageReuseDecision {
        .page_table_index = *page_table_index,
        .physical_page_index = decision.current_physical_page.value,
        .page_flags = BuildLeafPageFlags(decision),
        .physical_meta = frame_input->snapshot
          .physical_pages[decision.current_physical_page.value],
      });
      break;
    }
    case VsmAllocationAction::kAllocateNew: {
      CHECK_F(decision.current_physical_page.value
          < frame_input->snapshot.physical_pages.size(),
        "Allocation decision physical page {} exceeds snapshot capacity {}",
        decision.current_physical_page.value,
        frame_input->snapshot.physical_pages.size());
      const auto page_table_index
        = renderer::vsm::detail::TryResolvePageTableEntryIndex(
          frame_input->snapshot.virtual_frame, decision.request);
      CHECK_F(page_table_index.has_value(),
        "Failed to resolve page-table index for allocation decision map_id={} "
        "level={} page=({}, {})",
        decision.request.map_id, decision.request.page.level,
        decision.request.page.page_x, decision.request.page.page_y);

      allocation_decisions.push_back(VsmShaderPageAllocationDecision {
        .page_table_index = *page_table_index,
        .available_page_list_index = next_available_page_list_index++,
        .page_flags = BuildLeafPageFlags(decision),
        .physical_meta = frame_input->snapshot
          .physical_pages[decision.current_physical_page.value],
      });
      break;
    }
    case VsmAllocationAction::kEvict:
    case VsmAllocationAction::kReject:
      break;
    }
  }
}

VsmPageManagementPass::VsmPageManagementPass(
  observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : RenderPass(config ? config->debug_name : "VsmPageManagementPass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
}

VsmPageManagementPass::~VsmPageManagementPass() = default;

auto VsmPageManagementPass::SetFrameInput(VsmPageAllocationFrame frame) -> void
{
  impl_->frame_input = std::move(frame);
}

auto VsmPageManagementPass::ResetFrameInput() noexcept -> void
{
  impl_->frame_input.reset();
  impl_->reuse_decisions.clear();
  impl_->allocation_decisions.clear();
}

auto VsmPageManagementPass::GetAvailablePageCountBuffer() const noexcept
  -> std::shared_ptr<const Buffer>
{
  return impl_->available_page_count_buffer;
}

auto VsmPageManagementPass::ValidateConfig() -> void
{
  if (impl_->gfx == nullptr) {
    throw std::runtime_error("VsmPageManagementPass requires Graphics");
  }
  if (impl_->config == nullptr) {
    throw std::runtime_error("VsmPageManagementPass requires Config");
  }
}

auto VsmPageManagementPass::OnPrepareResources(CommandRecorder& /*recorder*/)
  -> void
{
  auto root_bindings = BuildRootBindings();

  if (!impl_->reuse_pso.has_value()) {
    impl_->reuse_pso
      = ComputePipelineDesc::Builder()
          .SetComputeShader(graphics::ShaderRequest {
            .stage = ShaderType::kCompute,
            .source_path = "Renderer/Vsm/VsmPageReuse.hlsl",
            .entry_point = "CS",
          })
          .SetRootBindings(std::span<const graphics::RootBindingItem>(
            root_bindings.data(), root_bindings.size()))
          .SetDebugName("VsmPageReuse_PSO")
          .Build();
  }

  if (!impl_->pack_pso.has_value()) {
    impl_->pack_pso
      = ComputePipelineDesc::Builder()
          .SetComputeShader(graphics::ShaderRequest {
            .stage = ShaderType::kCompute,
            .source_path = "Renderer/Vsm/VsmPackAvailablePages.hlsl",
            .entry_point = "CS",
          })
          .SetRootBindings(std::span<const graphics::RootBindingItem>(
            root_bindings.data(), root_bindings.size()))
          .SetDebugName("VsmPackAvailablePages_PSO")
          .Build();
  }

  if (!impl_->allocate_pso.has_value()) {
    impl_->allocate_pso
      = ComputePipelineDesc::Builder()
          .SetComputeShader(graphics::ShaderRequest {
            .stage = ShaderType::kCompute,
            .source_path = "Renderer/Vsm/VsmAllocateNewPages.hlsl",
            .entry_point = "CS",
          })
          .SetRootBindings(std::span<const graphics::RootBindingItem>(
            root_bindings.data(), root_bindings.size()))
          .SetDebugName("VsmAllocateNewPages_PSO")
          .Build();
  }
}

auto VsmPageManagementPass::OnExecute(CommandRecorder& /*recorder*/) -> void { }

auto VsmPageManagementPass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  impl_->resources_prepared = false;

  if (!impl_->frame_input.has_value()) {
    LOG_F(WARNING,
      "VSM page-management pass skipped because frame input is unavailable");
    co_return;
  }
  if (!impl_->frame_input->is_ready) {
    LOG_F(WARNING,
      "VSM page-management pass skipped because frame input is not ready");
    co_return;
  }
  if (impl_->frame_input->physical_page_meta_buffer == nullptr
    || impl_->frame_input->page_table_buffer == nullptr
    || impl_->frame_input->page_flags_buffer == nullptr
    || impl_->frame_input->physical_page_list_buffer == nullptr) {
    LOG_F(WARNING,
      "VSM page-management pass skipped because required GPU buffers are "
      "unavailable");
    co_return;
  }
  if (impl_->frame_input->physical_page_meta_seed_buffer != nullptr
    && impl_->frame_input->physical_page_meta_seed_buffer.get()
      == impl_->frame_input->physical_page_meta_buffer.get()) {
    LOG_F(ERROR,
      "VSM page-management pass skipped because the physical metadata seed "
      "buffer aliases the current-frame metadata output buffer");
    co_return;
  }

  impl_->BuildStageUploads();
  impl_->EnsureAvailablePageCountBuffer();
  impl_->EnsureMappedUploadBuffer(impl_->reuse_decision_buffer,
    impl_->reuse_decision_upload_buffer, impl_->reuse_decision_upload_ptr,
    impl_->reuse_decision_capacity,
    static_cast<std::uint32_t>(impl_->reuse_decisions.size()),
    sizeof(VsmShaderPageReuseDecision), impl_->config->debug_name + ".Reuse");
  impl_->EnsureMappedUploadBuffer(impl_->allocation_decision_buffer,
    impl_->allocation_decision_upload_buffer,
    impl_->allocation_decision_upload_ptr, impl_->allocation_decision_capacity,
    static_cast<std::uint32_t>(impl_->allocation_decisions.size()),
    sizeof(VsmShaderPageAllocationDecision),
    impl_->config->debug_name + ".Allocate");
  impl_->EnsureConstantsBuffer(impl_->reuse_constants_buffer,
    impl_->reuse_constants_ptr, impl_->reuse_constants_cbv_handle,
    impl_->reuse_constants_index,
    impl_->config->debug_name + ".ReuseConstants");
  impl_->EnsureConstantsBuffer(impl_->pack_constants_buffer,
    impl_->pack_constants_ptr, impl_->pack_constants_cbv_handle,
    impl_->pack_constants_index, impl_->config->debug_name + ".PackConstants");
  impl_->EnsureConstantsBuffer(impl_->allocate_constants_buffer,
    impl_->allocate_constants_ptr, impl_->allocate_constants_cbv_handle,
    impl_->allocate_constants_index,
    impl_->config->debug_name + ".AllocateConstants");

  const auto max_clear_size = std::max(
    { impl_->frame_input->physical_page_meta_buffer->GetDescriptor().size_bytes,
      impl_->frame_input->page_table_buffer->GetDescriptor().size_bytes,
      impl_->frame_input->page_flags_buffer->GetDescriptor().size_bytes,
      impl_->frame_input->dirty_flags_buffer
        ? impl_->frame_input->dirty_flags_buffer->GetDescriptor().size_bytes
        : 0ULL,
      impl_->frame_input->physical_page_list_buffer->GetDescriptor().size_bytes,
      impl_->frame_input->page_rect_bounds_buffer
        ? impl_->frame_input->page_rect_bounds_buffer->GetDescriptor()
            .size_bytes
        : 0ULL,
      sizeof(std::uint32_t) });
  impl_->EnsureClearBuffer(max_clear_size);

  if (!impl_->reuse_decisions.empty()) {
    std::memcpy(impl_->reuse_decision_upload_ptr, impl_->reuse_decisions.data(),
      impl_->reuse_decisions.size() * sizeof(VsmShaderPageReuseDecision));
  }
  if (!impl_->allocation_decisions.empty()) {
    std::memcpy(impl_->allocation_decision_upload_ptr,
      impl_->allocation_decisions.data(),
      impl_->allocation_decisions.size()
        * sizeof(VsmShaderPageAllocationDecision));
  }

  auto page_table_buffer
    = std::const_pointer_cast<Buffer>(impl_->frame_input->page_table_buffer);
  auto page_flags_buffer
    = std::const_pointer_cast<Buffer>(impl_->frame_input->page_flags_buffer);
  auto metadata_buffer = std::const_pointer_cast<Buffer>(
    impl_->frame_input->physical_page_meta_buffer);
  auto metadata_seed_buffer = std::const_pointer_cast<Buffer>(
    impl_->frame_input->physical_page_meta_seed_buffer);
  auto available_pages_buffer = std::const_pointer_cast<Buffer>(
    impl_->frame_input->physical_page_list_buffer);

  const auto reuse_constants = VsmPageReusePassConstants {
    .reuse_decision_buffer_index
    = impl_->EnsureBufferView(*impl_->reuse_decision_buffer,
      BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_SRV,
        impl_->reuse_decision_buffer->GetDescriptor().size_bytes,
        sizeof(VsmShaderPageReuseDecision)),
      impl_->reuse_decision_srv_handle, impl_->reuse_decision_srv,
      impl_->reuse_decision_owner),
    .metadata_seed_srv_index = metadata_seed_buffer != nullptr
      ? impl_->EnsureBufferView(*metadata_seed_buffer,
          BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_SRV,
            metadata_seed_buffer->GetDescriptor().size_bytes,
            sizeof(renderer::vsm::VsmPhysicalPageMeta)),
          impl_->metadata_seed_srv_handle, impl_->metadata_seed_srv,
          impl_->metadata_seed_owner)
      : kInvalidShaderVisibleIndex,
    .page_table_uav_index = impl_->EnsureBufferView(*page_table_buffer,
      BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_UAV,
        page_table_buffer->GetDescriptor().size_bytes, sizeof(std::uint32_t)),
      impl_->page_table_uav_handle, impl_->page_table_uav,
      impl_->page_table_owner),
    .page_flags_uav_index = impl_->EnsureBufferView(*page_flags_buffer,
      BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_UAV,
        page_flags_buffer->GetDescriptor().size_bytes, sizeof(std::uint32_t)),
      impl_->page_flags_uav_handle, impl_->page_flags_uav,
      impl_->page_flags_owner),
    .metadata_uav_index = impl_->EnsureBufferView(*metadata_buffer,
      BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_UAV,
        metadata_buffer->GetDescriptor().size_bytes,
        sizeof(renderer::vsm::VsmPhysicalPageMeta)),
      impl_->metadata_uav_handle, impl_->metadata_uav, impl_->metadata_owner),
    .reuse_decision_count
    = static_cast<std::uint32_t>(impl_->reuse_decisions.size()),
    .virtual_page_count = static_cast<std::uint32_t>(
      impl_->frame_input->snapshot.page_table.size()),
    .physical_page_count = static_cast<std::uint32_t>(
      impl_->frame_input->snapshot.physical_pages.size()),
  };
  std::memcpy(
    impl_->reuse_constants_ptr, &reuse_constants, sizeof(reuse_constants));

  const auto pack_constants = VsmPackAvailablePagesPassConstants {
    .metadata_uav_index = impl_->metadata_uav,
    .available_pages_uav_index
    = impl_->EnsureBufferView(*available_pages_buffer,
      BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_UAV,
        available_pages_buffer->GetDescriptor().size_bytes,
        sizeof(std::uint32_t)),
      impl_->available_pages_uav_handle, impl_->available_pages_uav,
      impl_->available_pages_owner),
    .available_page_count_uav_index
    = impl_->EnsureBufferView(*impl_->available_page_count_buffer,
      BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_UAV,
        impl_->available_page_count_buffer->GetDescriptor().size_bytes,
        sizeof(std::uint32_t)),
      impl_->available_page_count_uav_handle, impl_->available_page_count_uav,
      impl_->available_count_owner),
    .physical_page_count = static_cast<std::uint32_t>(
      impl_->frame_input->snapshot.physical_pages.size()),
  };
  std::memcpy(
    impl_->pack_constants_ptr, &pack_constants, sizeof(pack_constants));

  const auto allocate_constants = VsmAllocateNewPagesPassConstants {
    .allocation_decision_buffer_index
    = impl_->EnsureBufferView(*impl_->allocation_decision_buffer,
      BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_SRV,
        impl_->allocation_decision_buffer->GetDescriptor().size_bytes,
        sizeof(VsmShaderPageAllocationDecision)),
      impl_->allocation_decision_srv_handle, impl_->allocation_decision_srv,
      impl_->allocation_decision_owner),
    .available_pages_srv_index
    = impl_->EnsureBufferView(*available_pages_buffer,
      BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_SRV,
        available_pages_buffer->GetDescriptor().size_bytes,
        sizeof(std::uint32_t)),
      impl_->available_pages_srv_handle, impl_->available_pages_srv,
      impl_->available_pages_owner),
    .available_page_count_srv_index
    = impl_->EnsureBufferView(*impl_->available_page_count_buffer,
      BuildBufferViewDesc(ResourceViewType::kStructuredBuffer_SRV,
        impl_->available_page_count_buffer->GetDescriptor().size_bytes,
        sizeof(std::uint32_t)),
      impl_->available_page_count_srv_handle, impl_->available_page_count_srv,
      impl_->available_count_owner),
    .metadata_seed_srv_index = reuse_constants.metadata_seed_srv_index,
    .page_table_uav_index = impl_->page_table_uav,
    .page_flags_uav_index = impl_->page_flags_uav,
    .metadata_uav_index = impl_->metadata_uav,
    .allocation_decision_count
    = static_cast<std::uint32_t>(impl_->allocation_decisions.size()),
    .virtual_page_count = static_cast<std::uint32_t>(
      impl_->frame_input->snapshot.page_table.size()),
  };
  std::memcpy(impl_->allocate_constants_ptr, &allocate_constants,
    sizeof(allocate_constants));

  if (!RequireValidIndex(
        reuse_constants.reuse_decision_buffer_index, "reuse-decision SRV")
    || (metadata_seed_buffer != nullptr
      && !RequireValidIndex(reuse_constants.metadata_seed_srv_index,
        "physical-page metadata seed SRV"))
    || !RequireValidIndex(
      reuse_constants.page_table_uav_index, "page-table UAV")
    || !RequireValidIndex(
      reuse_constants.page_flags_uav_index, "page-flags UAV")
    || !RequireValidIndex(
      reuse_constants.metadata_uav_index, "physical-page metadata UAV")) {
    co_return;
  }
  if (impl_->config->final_stage
      >= VsmPageManagementFinalStage::kPackAvailablePages
    && (!RequireValidIndex(
          pack_constants.available_pages_uav_index, "available-page stack UAV")
      || !RequireValidIndex(pack_constants.available_page_count_uav_index,
        "available-page-count UAV"))) {
    co_return;
  }
  if (impl_->config->final_stage
      >= VsmPageManagementFinalStage::kAllocateNewPages
    && !impl_->allocation_decisions.empty()
    && (!RequireValidIndex(allocate_constants.allocation_decision_buffer_index,
          "allocation-decision SRV")
      || !RequireValidIndex(allocate_constants.available_pages_srv_index,
        "available-page stack SRV")
      || !RequireValidIndex(allocate_constants.available_page_count_srv_index,
        "available-page-count SRV"))) {
    co_return;
  }

  recorder.BeginTrackingResourceState(
    *impl_->clear_buffer, ResourceStates::kGenericRead, true);
  recorder.BeginTrackingResourceState(
    *impl_->reuse_constants_buffer, ResourceStates::kGenericRead, true);
  recorder.BeginTrackingResourceState(
    *impl_->pack_constants_buffer, ResourceStates::kGenericRead, true);
  recorder.BeginTrackingResourceState(
    *impl_->allocate_constants_buffer, ResourceStates::kGenericRead, true);
  recorder.BeginTrackingResourceState(
    *page_table_buffer, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *page_flags_buffer, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *metadata_buffer, ResourceStates::kCommon, true);
  if (metadata_seed_buffer != nullptr) {
    recorder.BeginTrackingResourceState(
      *metadata_seed_buffer, ResourceStates::kCommon, true);
  }
  recorder.BeginTrackingResourceState(
    *available_pages_buffer, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *impl_->available_page_count_buffer, ResourceStates::kCommon, true);
  if (impl_->frame_input->dirty_flags_buffer != nullptr) {
    recorder.BeginTrackingResourceState(
      *std::const_pointer_cast<Buffer>(impl_->frame_input->dirty_flags_buffer),
      ResourceStates::kCommon, true);
  }
  if (impl_->frame_input->page_rect_bounds_buffer != nullptr) {
    recorder.BeginTrackingResourceState(
      *std::const_pointer_cast<Buffer>(
        impl_->frame_input->page_rect_bounds_buffer),
      ResourceStates::kCommon, true);
  }

  if (!impl_->reuse_decisions.empty()) {
    recorder.BeginTrackingResourceState(
      *impl_->reuse_decision_buffer, ResourceStates::kCommon, true);
    recorder.BeginTrackingResourceState(
      *impl_->reuse_decision_upload_buffer, ResourceStates::kGenericRead, true);
    recorder.RequireResourceState(
      *impl_->reuse_decision_upload_buffer, ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *impl_->reuse_decision_buffer, ResourceStates::kCopyDest);
  }
  if (!impl_->allocation_decisions.empty()) {
    recorder.BeginTrackingResourceState(
      *impl_->allocation_decision_buffer, ResourceStates::kCommon, true);
    recorder.BeginTrackingResourceState(
      *impl_->allocation_decision_upload_buffer, ResourceStates::kGenericRead,
      true);
    recorder.RequireResourceState(
      *impl_->allocation_decision_upload_buffer, ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *impl_->allocation_decision_buffer, ResourceStates::kCopyDest);
  }

  const auto require_clear = [&](const std::shared_ptr<const Buffer>& buffer) {
    if (buffer == nullptr) {
      return;
    }
    recorder.RequireResourceState(
      *std::const_pointer_cast<Buffer>(buffer), ResourceStates::kCopyDest);
  };
  require_clear(impl_->frame_input->physical_page_meta_buffer);
  require_clear(impl_->frame_input->page_table_buffer);
  require_clear(impl_->frame_input->page_flags_buffer);
  require_clear(impl_->frame_input->dirty_flags_buffer);
  require_clear(impl_->frame_input->physical_page_list_buffer);
  require_clear(impl_->frame_input->page_rect_bounds_buffer);
  recorder.RequireResourceState(
    *impl_->available_page_count_buffer, ResourceStates::kCopyDest);
  if (metadata_seed_buffer != nullptr) {
    recorder.RequireResourceState(
      *metadata_seed_buffer, ResourceStates::kShaderResource);
  }
  recorder.FlushBarriers();

  const auto clear_buffer_into
    = [&](const std::shared_ptr<const Buffer>& buffer,
        const std::uint64_t size_bytes) {
        if (buffer == nullptr || size_bytes == 0U) {
          return;
        }
        recorder.CopyBuffer(*std::const_pointer_cast<Buffer>(buffer), 0U,
          *impl_->clear_buffer, 0U, size_bytes);
      };
  clear_buffer_into(impl_->frame_input->physical_page_meta_buffer,
    impl_->frame_input->physical_page_meta_buffer->GetDescriptor().size_bytes);
  clear_buffer_into(impl_->frame_input->page_table_buffer,
    impl_->frame_input->page_table_buffer->GetDescriptor().size_bytes);
  clear_buffer_into(impl_->frame_input->page_flags_buffer,
    impl_->frame_input->page_flags_buffer->GetDescriptor().size_bytes);
  clear_buffer_into(impl_->frame_input->dirty_flags_buffer,
    impl_->frame_input->dirty_flags_buffer
      ? impl_->frame_input->dirty_flags_buffer->GetDescriptor().size_bytes
      : 0ULL);
  clear_buffer_into(impl_->frame_input->physical_page_list_buffer,
    impl_->frame_input->physical_page_list_buffer->GetDescriptor().size_bytes);
  clear_buffer_into(impl_->frame_input->page_rect_bounds_buffer,
    impl_->frame_input->page_rect_bounds_buffer
      ? impl_->frame_input->page_rect_bounds_buffer->GetDescriptor().size_bytes
      : 0ULL);
  recorder.CopyBuffer(*impl_->available_page_count_buffer, 0U,
    *impl_->clear_buffer, 0U, sizeof(std::uint32_t));

  if (!impl_->reuse_decisions.empty()) {
    recorder.CopyBuffer(*impl_->reuse_decision_buffer, 0U,
      *impl_->reuse_decision_upload_buffer, 0U,
      impl_->reuse_decisions.size() * sizeof(VsmShaderPageReuseDecision));
    recorder.RequireResourceState(
      *impl_->reuse_decision_buffer, ResourceStates::kShaderResource);
  }
  if (!impl_->allocation_decisions.empty()) {
    recorder.CopyBuffer(*impl_->allocation_decision_buffer, 0U,
      *impl_->allocation_decision_upload_buffer, 0U,
      impl_->allocation_decisions.size()
        * sizeof(VsmShaderPageAllocationDecision));
    recorder.RequireResourceState(
      *impl_->allocation_decision_buffer, ResourceStates::kShaderResource);
  }

  recorder.RequireResourceState(
    *page_table_buffer, ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *page_flags_buffer, ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *metadata_buffer, ResourceStates::kUnorderedAccess);
  if (impl_->config->final_stage
    >= VsmPageManagementFinalStage::kPackAvailablePages) {
    recorder.RequireResourceState(
      *available_pages_buffer, ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(
      *impl_->available_page_count_buffer, ResourceStates::kUnorderedAccess);
  }
  recorder.FlushBarriers();

  DLOG_F(2,
    "prepared VSM page-management pass generation={} reuse_decisions={} "
    "allocation_decisions={} final_stage={}",
    impl_->frame_input->snapshot.frame_generation,
    impl_->reuse_decisions.size(), impl_->allocation_decisions.size(),
    static_cast<std::uint32_t>(impl_->config->final_stage));
  impl_->resources_prepared = true;
  co_return;
}

auto VsmPageManagementPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  if (!impl_->resources_prepared || !impl_->frame_input.has_value()) {
    DLOG_F(2, "VSM page-management resources were not prepared, skipping");
    co_return;
  }

  const auto page_table_buffer
    = std::const_pointer_cast<Buffer>(impl_->frame_input->page_table_buffer);
  const auto page_flags_buffer
    = std::const_pointer_cast<Buffer>(impl_->frame_input->page_flags_buffer);
  const auto metadata_buffer = std::const_pointer_cast<Buffer>(
    impl_->frame_input->physical_page_meta_buffer);
  const auto available_pages_buffer = std::const_pointer_cast<Buffer>(
    impl_->frame_input->physical_page_list_buffer);

  if (!impl_->reuse_decisions.empty()) {
    BindComputeStage(
      recorder, *impl_->reuse_pso, impl_->reuse_constants_index, Context());
    recorder.Dispatch(MakeDecisionGroups(static_cast<std::uint32_t>(
                        impl_->reuse_decisions.size())),
      1U, 1U);
  }

  if (impl_->config->final_stage
    >= VsmPageManagementFinalStage::kPackAvailablePages) {
    // Stage 7 consumes the metadata UAV written by stage 6, so keep the
    // resource in UAV state and force an explicit memory barrier between the
    // two dispatches.
    recorder.RequireResourceState(
      *metadata_buffer, ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();

    BindComputeStage(
      recorder, *impl_->pack_pso, impl_->pack_constants_index, Context());
    recorder.Dispatch(1U, 1U, 1U);
  }

  if (impl_->config->final_stage
      >= VsmPageManagementFinalStage::kAllocateNewPages
    && !impl_->allocation_decisions.empty()) {
    recorder.RequireResourceState(
      *page_table_buffer, ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(
      *page_flags_buffer, ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(
      *metadata_buffer, ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(
      *available_pages_buffer, ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *impl_->available_page_count_buffer, ResourceStates::kShaderResource);
    recorder.FlushBarriers();

    BindComputeStage(recorder, *impl_->allocate_pso,
      impl_->allocate_constants_index, Context());
    recorder.Dispatch(MakeDecisionGroups(static_cast<std::uint32_t>(
                        impl_->allocation_decisions.size())),
      1U, 1U);
  }

  recorder.RequireResourceState(
    *page_table_buffer, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *page_flags_buffer, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *metadata_buffer, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *available_pages_buffer, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *impl_->available_page_count_buffer, ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  co_return;
}

} // namespace oxygen::engine
