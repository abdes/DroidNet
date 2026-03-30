//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/Vsm/VsmInvalidationPass.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

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
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/RenderContext.h>

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
using oxygen::renderer::vsm::VsmCacheInvalidationScope;
using oxygen::renderer::vsm::VsmInvalidationWorkItem;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmShaderInvalidationWorkItem;
using oxygen::renderer::vsm::VsmShaderPageTableEntry;

namespace oxygen::engine {

namespace {

  constexpr std::uint32_t kThreadGroupSize = 64U;

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmInvalidationPassConstants {
    ShaderVisibleIndex projection_records_srv_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex work_items_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_table_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex physical_meta_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t projection_record_count { 0U };
    std::uint32_t work_item_count { 0U };
    std::uint32_t page_table_entry_count { 0U };
    std::uint32_t physical_page_count { 0U };
  };
  static_assert(
    sizeof(VsmInvalidationPassConstants) % packing::kShaderDataFieldAlignment
    == 0U);

  auto MakeDispatchGroups(const std::uint32_t count) noexcept -> std::uint32_t
  {
    return count == 0U ? 0U
                       : (count + kThreadGroupSize - 1U) / kThreadGroupSize;
  }

  auto MakeStructuredViewDesc(const ResourceViewType view_type,
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

  auto MakeRawViewDesc(const ResourceViewType view_type,
    const std::uint64_t size_bytes) -> graphics::BufferViewDescription
  {
    return graphics::BufferViewDescription {
      .view_type = view_type,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { 0U, size_bytes },
      .stride = 0U,
    };
  }

  auto RequireValidIndex(const ShaderVisibleIndex index,
    const std::string_view label) noexcept -> bool
  {
    if (index.IsValid()) {
      return true;
    }

    LOG_F(
      ERROR, "VSM invalidation pass skipped because {} is unavailable", label);
    return false;
  }

} // namespace

struct VsmInvalidationPass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;

  std::optional<VsmInvalidationPassInput> input {};
  bool resources_prepared { false };
  bool output_available { false };

  std::vector<VsmShaderInvalidationWorkItem> shader_work_items {};

  std::shared_ptr<Buffer> projection_records_buffer {};
  std::shared_ptr<Buffer> projection_records_upload_buffer {};
  void* projection_records_upload_ptr { nullptr };
  std::uint32_t projection_record_capacity { 0U };

  std::shared_ptr<Buffer> page_table_buffer {};
  std::shared_ptr<Buffer> page_table_upload_buffer {};
  void* page_table_upload_ptr { nullptr };
  std::uint32_t page_table_entry_capacity { 0U };

  std::shared_ptr<Buffer> physical_meta_buffer {};
  std::shared_ptr<Buffer> physical_meta_upload_buffer {};
  void* physical_meta_upload_ptr { nullptr };
  std::uint32_t physical_page_capacity { 0U };

  std::shared_ptr<Buffer> work_item_buffer {};
  std::shared_ptr<Buffer> work_item_upload_buffer {};
  void* work_item_upload_ptr { nullptr };
  std::uint32_t work_item_capacity { 0U };

  std::shared_ptr<Buffer> constants_buffer {};
  void* constants_ptr { nullptr };

  DescriptorHandle projection_records_srv_handle {};
  DescriptorHandle page_table_srv_handle {};
  DescriptorHandle physical_meta_uav_handle {};
  DescriptorHandle work_item_srv_handle {};
  DescriptorHandle constants_cbv_handle {};

  ShaderVisibleIndex projection_records_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_table_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_meta_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex work_item_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex constants_index { kInvalidShaderVisibleIndex };

  const Buffer* projection_records_owner { nullptr };
  const Buffer* page_table_owner { nullptr };
  const Buffer* physical_meta_owner { nullptr };
  const Buffer* work_item_owner { nullptr };
  const Buffer* constants_owner { nullptr };

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl()
  {
    const auto unmap_if_needed
      = [](const std::shared_ptr<Buffer>& buffer, void*& ptr) {
          if (buffer != nullptr && ptr != nullptr) {
            buffer->UnMap();
            ptr = nullptr;
          }
        };

    unmap_if_needed(
      projection_records_upload_buffer, projection_records_upload_ptr);
    unmap_if_needed(page_table_upload_buffer, page_table_upload_ptr);
    unmap_if_needed(physical_meta_upload_buffer, physical_meta_upload_ptr);
    unmap_if_needed(work_item_upload_buffer, work_item_upload_ptr);
    unmap_if_needed(constants_buffer, constants_ptr);

    if (gfx != nullptr) {
      auto& registry = gfx->GetResourceRegistry();
      const auto unregister_if_present
        = [&](const std::shared_ptr<Buffer>& buffer) {
            if (buffer != nullptr && registry.Contains(*buffer)) {
              registry.UnRegisterResource(*buffer);
            }
          };

      unregister_if_present(constants_buffer);
      unregister_if_present(work_item_upload_buffer);
      unregister_if_present(work_item_buffer);
      unregister_if_present(physical_meta_upload_buffer);
      unregister_if_present(physical_meta_buffer);
      unregister_if_present(page_table_upload_buffer);
      unregister_if_present(page_table_buffer);
      unregister_if_present(projection_records_upload_buffer);
      unregister_if_present(projection_records_buffer);
    }
  }

  auto EnsureMappedUploadBuffer(std::shared_ptr<Buffer>& device_buffer,
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

    capacity = (std::max)(required_count, 1U);
    const auto size_bytes = static_cast<std::uint64_t>(capacity) * stride;
    auto& registry = gfx->GetResourceRegistry();

    device_buffer = gfx->CreateBuffer(BufferDesc {
      .size_bytes = size_bytes,
      .usage = BufferUsage::kStorage,
      .memory = BufferMemory::kDeviceLocal,
      .debug_name = base_name,
    });
    CHECK_NOTNULL_F(device_buffer.get(), "Failed to create {}", base_name);
    registry.Register(device_buffer);

    upload_buffer = gfx->CreateBuffer(BufferDesc {
      .size_bytes = size_bytes,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = base_name + ".Upload",
    });
    CHECK_NOTNULL_F(
      upload_buffer.get(), "Failed to create {} upload", base_name);
    registry.Register(upload_buffer);
    mapped_ptr = upload_buffer->Map(0U, size_bytes);
    CHECK_NOTNULL_F(mapped_ptr, "Failed to map {} upload", base_name);
  }

  auto EnsureConstantsBuffer() -> void
  {
    if (constants_buffer != nullptr && constants_ptr != nullptr) {
      return;
    }

    auto& registry = gfx->GetResourceRegistry();
    constants_buffer = gfx->CreateBuffer(BufferDesc {
      .size_bytes = 256U,
      .usage = BufferUsage::kConstant,
      .memory = BufferMemory::kUpload,
      .debug_name = config->debug_name + ".Constants",
    });
    CHECK_NOTNULL_F(
      constants_buffer.get(), "Failed to create VSM invalidation constants");
    registry.Register(constants_buffer);
    constants_ptr = constants_buffer->Map(0U, 256U);
    CHECK_NOTNULL_F(
      constants_ptr, "Failed to map VSM invalidation constants buffer");
  }

  auto EnsureBufferView(Buffer& buffer,
    const graphics::BufferViewDescription& desc, DescriptorHandle& handle,
    ShaderVisibleIndex& index, const Buffer*& owner) -> ShaderVisibleIndex
  {
    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    if (owner == &buffer && index.IsValid()) {
      return index;
    }

    if (const auto existing = registry.FindShaderVisibleIndex(buffer, desc);
      existing.has_value()) {
      owner = &buffer;
      index = *existing;
      return index;
    }

    handle = allocator.Allocate(
      desc.view_type, graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(
      handle.IsValid(), "Failed to allocate VSM invalidation buffer view");
    index = allocator.GetShaderVisibleIndex(handle);
    const auto view = registry.RegisterView(buffer, std::move(handle), desc);
    CHECK_F(view->IsValid(), "Failed to register VSM invalidation buffer view");
    owner = &buffer;
    return index;
  }

  auto BuildShaderWorkItems() -> void
  {
    shader_work_items.clear();
    if (!input.has_value()) {
      return;
    }

    shader_work_items.reserve(input->invalidation_work_items.size());
    for (const auto& item : input->invalidation_work_items) {
      shader_work_items.push_back(VsmShaderInvalidationWorkItem {
        .primitive = item.primitive,
        .world_bounding_sphere = item.world_bounding_sphere,
        .projection_index = item.projection_index,
        .scope = static_cast<std::uint32_t>(item.scope),
        .matched_static_feedback = static_cast<std::uint32_t>(
          static_cast<bool>(item.matched_static_feedback)),
      });
    }
  }
};

VsmInvalidationPass::VsmInvalidationPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass("VsmInvalidationPass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
}

VsmInvalidationPass::~VsmInvalidationPass() = default;

auto VsmInvalidationPass::SetInput(VsmInvalidationPassInput input) -> void
{
  impl_->input = std::move(input);
  impl_->resources_prepared = false;
  impl_->output_available = false;
}

auto VsmInvalidationPass::ResetInput() noexcept -> void
{
  impl_->input.reset();
  impl_->shader_work_items.clear();
  impl_->resources_prepared = false;
  impl_->output_available = false;
}

auto VsmInvalidationPass::GetCurrentOutputPhysicalMetadataBuffer()
  const noexcept -> std::shared_ptr<const Buffer>
{
  return impl_->output_available ? impl_->physical_meta_buffer
                                 : std::shared_ptr<const Buffer> {};
}

auto VsmInvalidationPass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  if (!impl_->input.has_value()) {
    impl_->output_available = false;
    co_return;
  }

  impl_->BuildShaderWorkItems();

  const auto projection_count = static_cast<std::uint32_t>(
    impl_->input->previous_projection_records.size());
  const auto page_table_count = static_cast<std::uint32_t>(
    impl_->input->previous_page_table_entries.size());
  const auto physical_page_count = static_cast<std::uint32_t>(
    impl_->input->previous_physical_page_metadata.size());
  const auto work_item_count
    = static_cast<std::uint32_t>(impl_->shader_work_items.size());

  impl_->EnsureMappedUploadBuffer(impl_->projection_records_buffer,
    impl_->projection_records_upload_buffer,
    impl_->projection_records_upload_ptr, impl_->projection_record_capacity,
    projection_count, sizeof(VsmPageRequestProjection),
    impl_->config->debug_name + ".Projections");
  impl_->EnsureMappedUploadBuffer(impl_->page_table_buffer,
    impl_->page_table_upload_buffer, impl_->page_table_upload_ptr,
    impl_->page_table_entry_capacity, page_table_count,
    sizeof(VsmShaderPageTableEntry), impl_->config->debug_name + ".PageTable");
  impl_->EnsureMappedUploadBuffer(impl_->physical_meta_buffer,
    impl_->physical_meta_upload_buffer, impl_->physical_meta_upload_ptr,
    impl_->physical_page_capacity, physical_page_count,
    sizeof(VsmPhysicalPageMeta), impl_->config->debug_name + ".PhysicalMeta");
  impl_->EnsureMappedUploadBuffer(impl_->work_item_buffer,
    impl_->work_item_upload_buffer, impl_->work_item_upload_ptr,
    impl_->work_item_capacity, work_item_count,
    sizeof(VsmShaderInvalidationWorkItem),
    impl_->config->debug_name + ".WorkItems");
  impl_->EnsureConstantsBuffer();

  if (projection_count > 0U) {
    std::memcpy(impl_->projection_records_upload_ptr,
      impl_->input->previous_projection_records.data(),
      static_cast<std::size_t>(projection_count)
        * sizeof(VsmPageRequestProjection));
  }
  if (page_table_count > 0U) {
    std::memcpy(impl_->page_table_upload_ptr,
      impl_->input->previous_page_table_entries.data(),
      static_cast<std::size_t>(page_table_count)
        * sizeof(VsmShaderPageTableEntry));
  }
  if (physical_page_count > 0U) {
    std::memcpy(impl_->physical_meta_upload_ptr,
      impl_->input->previous_physical_page_metadata.data(),
      static_cast<std::size_t>(physical_page_count)
        * sizeof(VsmPhysicalPageMeta));
  }
  if (work_item_count > 0U) {
    std::memcpy(impl_->work_item_upload_ptr, impl_->shader_work_items.data(),
      static_cast<std::size_t>(work_item_count)
        * sizeof(VsmShaderInvalidationWorkItem));
  }

  const auto projection_size_bytes
    = std::uint64_t { impl_->projection_record_capacity }
    * sizeof(VsmPageRequestProjection);
  const auto page_table_size_bytes
    = std::uint64_t { impl_->page_table_entry_capacity }
    * sizeof(VsmShaderPageTableEntry);
  const auto physical_meta_size_bytes
    = std::uint64_t { impl_->physical_page_capacity }
    * sizeof(VsmPhysicalPageMeta);
  const auto work_item_size_bytes = std::uint64_t { impl_->work_item_capacity }
    * sizeof(VsmShaderInvalidationWorkItem);

  impl_->EnsureBufferView(*impl_->projection_records_buffer,
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      projection_size_bytes, sizeof(VsmPageRequestProjection)),
    impl_->projection_records_srv_handle, impl_->projection_records_srv,
    impl_->projection_records_owner);
  impl_->EnsureBufferView(*impl_->page_table_buffer,
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      page_table_size_bytes, sizeof(VsmShaderPageTableEntry)),
    impl_->page_table_srv_handle, impl_->page_table_srv,
    impl_->page_table_owner);
  impl_->EnsureBufferView(*impl_->physical_meta_buffer,
    MakeRawViewDesc(ResourceViewType::kRawBuffer_UAV, physical_meta_size_bytes),
    impl_->physical_meta_uav_handle, impl_->physical_meta_uav,
    impl_->physical_meta_owner);
  impl_->EnsureBufferView(*impl_->work_item_buffer,
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      work_item_size_bytes, sizeof(VsmShaderInvalidationWorkItem)),
    impl_->work_item_srv_handle, impl_->work_item_srv, impl_->work_item_owner);
  impl_->EnsureBufferView(*impl_->constants_buffer,
    graphics::BufferViewDescription {
      .view_type = ResourceViewType::kConstantBuffer,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { 0U, 256U },
      .stride = 0U,
    },
    impl_->constants_cbv_handle, impl_->constants_index,
    impl_->constants_owner);

  auto constants = VsmInvalidationPassConstants {
    .projection_records_srv_index = impl_->projection_records_srv,
    .work_items_srv_index = impl_->work_item_srv,
    .page_table_srv_index = impl_->page_table_srv,
    .physical_meta_uav_index = impl_->physical_meta_uav,
    .projection_record_count = projection_count,
    .work_item_count = work_item_count,
    .page_table_entry_count = page_table_count,
    .physical_page_count = physical_page_count,
  };
  std::memcpy(impl_->constants_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(impl_->constants_index);

  const auto upload_buffer = [&](const std::shared_ptr<Buffer>& upload,
                               const std::shared_ptr<Buffer>& device,
                               const std::uint64_t size_bytes) -> void {
    if (size_bytes == 0U) {
      return;
    }
    recorder.BeginTrackingResourceState(
      *upload, ResourceStates::kGenericRead, true);
    recorder.BeginTrackingResourceState(*device, ResourceStates::kCommon, true);
    recorder.RequireResourceState(*upload, ResourceStates::kCopySource);
    recorder.RequireResourceState(*device, ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyBuffer(*device, 0U, *upload, 0U, size_bytes);
  };

  upload_buffer(impl_->projection_records_upload_buffer,
    impl_->projection_records_buffer,
    static_cast<std::uint64_t>(projection_count)
      * sizeof(VsmPageRequestProjection));
  upload_buffer(impl_->page_table_upload_buffer, impl_->page_table_buffer,
    static_cast<std::uint64_t>(page_table_count)
      * sizeof(VsmShaderPageTableEntry));
  upload_buffer(impl_->physical_meta_upload_buffer, impl_->physical_meta_buffer,
    static_cast<std::uint64_t>(physical_page_count)
      * sizeof(VsmPhysicalPageMeta));
  upload_buffer(impl_->work_item_upload_buffer, impl_->work_item_buffer,
    static_cast<std::uint64_t>(work_item_count)
      * sizeof(VsmShaderInvalidationWorkItem));

  recorder.RequireResourceState(
    *impl_->projection_records_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *impl_->page_table_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *impl_->work_item_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *impl_->physical_meta_buffer, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  impl_->resources_prepared = true;
  impl_->output_available = true;
  co_return;
}

auto VsmInvalidationPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  if (!impl_->input.has_value() || !impl_->resources_prepared
    || impl_->shader_work_items.empty()) {
    impl_->output_available = false;
    co_return;
  }

  if (!RequireValidIndex(impl_->projection_records_srv, "projection-record SRV")
    || !RequireValidIndex(impl_->work_item_srv, "work-item SRV")
    || !RequireValidIndex(impl_->page_table_srv, "page-table SRV")
    || !RequireValidIndex(impl_->physical_meta_uav, "physical-meta UAV")
    || !RequireValidIndex(impl_->constants_index, "pass constants")) {
    impl_->output_available = false;
    co_return;
  }

  const auto dispatch_groups = MakeDispatchGroups(
    static_cast<std::uint32_t>(impl_->shader_work_items.size()));
  if (dispatch_groups == 0U) {
    impl_->output_available = false;
    co_return;
  }

  recorder.RequireResourceState(
    *impl_->projection_records_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *impl_->work_item_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *impl_->page_table_buffer, ResourceStates::kGenericRead);
  recorder.RequireResourceState(
    *impl_->physical_meta_buffer, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  recorder.Dispatch(dispatch_groups, 1U, 1U);

  co_return;
}

auto VsmInvalidationPass::ValidateConfig() -> void
{
  if (impl_->gfx == nullptr) {
    throw std::runtime_error("VsmInvalidationPass requires Graphics");
  }
}

auto VsmInvalidationPass::CreatePipelineStateDesc() -> ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();
  graphics::ShaderRequest shader_request {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Renderer/Vsm/VsmInvalidation.hlsl",
    .entry_point = "CS",
  };

  return ComputePipelineDesc::Builder()
    .SetComputeShader(std::move(shader_request))
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VsmInvalidation_PSO")
    .Build();
}

auto VsmInvalidationPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
