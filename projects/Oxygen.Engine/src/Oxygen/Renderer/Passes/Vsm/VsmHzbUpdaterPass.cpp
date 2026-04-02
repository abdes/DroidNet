//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/Vsm/VsmHzbUpdaterPass.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
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
using oxygen::graphics::ResourceStates;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;

namespace oxygen::engine {

namespace {

  constexpr std::uint32_t kSelectPagesThreadGroupSize = 64U;
  constexpr std::uint32_t kBuildHzbThreadGroupSize = 8U;

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmHzbSelectPagesPassConstants {
    ShaderVisibleIndex dirty_flags_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex physical_meta_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex selected_pages_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex selected_page_count_uav_index {
      kInvalidShaderVisibleIndex
    };
    std::uint32_t physical_page_count { 0U };
    std::uint32_t force_rebuild_all_allocated_pages { 0U };
    std::uint32_t tiles_per_axis { 0U };
    std::uint32_t dynamic_slice_index { 0U };
  };
  static_assert(
    sizeof(VsmHzbSelectPagesPassConstants) % packing::kShaderDataFieldAlignment
    == 0U);

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmHzbScratchInitPassConstants {
    ShaderVisibleIndex destination_hzb_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t destination_width { 0U };
    std::uint32_t destination_height { 0U };
    float clear_depth { 0.0F };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };
  };
  static_assert(
    sizeof(VsmHzbScratchInitPassConstants) % packing::kShaderDataFieldAlignment
    == 0U);

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmHzbPerPageBuildPassConstants {
    ShaderVisibleIndex shadow_depth_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex source_hzb_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex destination_hzb_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex selected_pages_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex selected_page_count_srv_index {
      kInvalidShaderVisibleIndex
    };
    std::uint32_t page_size_texels { 0U };
    std::uint32_t tiles_per_axis { 0U };
    std::uint32_t destination_page_extent_texels { 0U };
    std::uint32_t source_is_shadow_depth { 0U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };
  };
  static_assert(
    sizeof(VsmHzbPerPageBuildPassConstants) % packing::kShaderDataFieldAlignment
    == 0U);

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmHzbPrepareDispatchArgsPassConstants {
    ShaderVisibleIndex selected_page_count_srv_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex dispatch_args_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t thread_group_count_x { 0U };
    std::uint32_t thread_group_count_y { 0U };
  };
  static_assert(sizeof(VsmHzbPrepareDispatchArgsPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmHzbTopBuildPassConstants {
    ShaderVisibleIndex source_hzb_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex destination_hzb_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t destination_width { 0U };
    std::uint32_t destination_height { 0U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };
    std::uint32_t _pad3 { 0U };
  };
  static_assert(
    sizeof(VsmHzbTopBuildPassConstants) % packing::kShaderDataFieldAlignment
    == 0U);

  [[nodiscard]] auto MakeDispatchGroups(const std::uint32_t count,
    const std::uint32_t group_size) noexcept -> std::uint32_t;
  [[nodiscard]] auto MipExtent(std::uint32_t base_extent,
    std::uint32_t mip_level) noexcept -> std::uint32_t;
  [[nodiscard]] auto ComputePageLocalMipCount(
    std::uint32_t page_size_texels) noexcept -> std::uint32_t;
  [[nodiscard]] auto FindSliceIndex(
    const renderer::vsm::VsmPhysicalPoolSnapshot& pool,
    VsmPhysicalPoolSliceRole role) noexcept -> std::optional<std::uint32_t>;
  [[nodiscard]] auto MakeStructuredViewDesc(
    ResourceViewType view_type, std::uint64_t size_bytes, std::uint32_t stride)
    -> graphics::BufferViewDescription;
  [[nodiscard]] auto WholeTextureSrvDesc(oxygen::Format format)
    -> graphics::TextureViewDescription;
  [[nodiscard]] auto WholeTextureUavDesc(oxygen::Format format)
    -> graphics::TextureViewDescription;
  [[nodiscard]] auto SingleMipTextureSrvDesc(oxygen::Format format,
    std::uint32_t mip_level) -> graphics::TextureViewDescription;
  [[nodiscard]] auto SingleMipTextureUavDesc(oxygen::Format format,
    std::uint32_t mip_level) -> graphics::TextureViewDescription;
  [[nodiscard]] auto ShadowSliceSrvDesc(oxygen::Format depth_format,
    std::uint32_t slice) -> graphics::TextureViewDescription;
  auto BindComputeStage(CommandRecorder& recorder,
    const graphics::ComputePipelineDesc& pso_desc,
    ShaderVisibleIndex pass_constants_index, const RenderContext& context)
    -> void;
  auto RequireValidIndex(ShaderVisibleIndex index, std::string_view label)
    -> bool;

  template <typename Resource>
  auto RegisterResourceIfNeeded(
    Graphics& gfx, const std::shared_ptr<Resource>& resource) -> void;

  template <typename Resource>
  auto UnregisterResourceIfPresent(
    Graphics& gfx, const std::shared_ptr<Resource>& resource) -> void;

} // namespace

struct VsmHzbUpdaterPass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;

  std::optional<VsmHzbUpdaterPassInput> input {};
  bool resources_prepared { false };
  bool pipelines_ready { false };

  std::shared_ptr<Buffer> pass_constants_buffer {};
  void* pass_constants_mapped_ptr { nullptr };
  std::vector<ShaderVisibleIndex> pass_constants_indices {};
  std::uint32_t pass_constants_slot_count { 0U };
  std::uint32_t next_pass_constants_slot { 0U };

  std::shared_ptr<Buffer> zero_fill_buffer {};
  void* zero_fill_ptr { nullptr };
  std::uint64_t zero_fill_capacity { 0U };

  std::shared_ptr<Buffer> selected_page_index_buffer {};
  std::shared_ptr<Buffer> selected_page_count_buffer {};
  std::shared_ptr<Buffer> dispatch_args_buffer {};
  std::uint32_t selected_page_capacity { 0U };

  std::array<std::shared_ptr<Texture>, 2> scratch_textures {};
  std::array<ShaderVisibleIndex, 2> scratch_srv_indices {
    kInvalidShaderVisibleIndex,
    kInvalidShaderVisibleIndex,
  };
  std::array<ShaderVisibleIndex, 2> scratch_uav_indices {
    kInvalidShaderVisibleIndex,
    kInvalidShaderVisibleIndex,
  };
  std::uint32_t scratch_extent { 0U };
  oxygen::Format scratch_format { oxygen::Format::kUnknown };

  ShaderVisibleIndex shadow_depth_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex dirty_flags_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_meta_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex selected_pages_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex selected_pages_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex selected_page_count_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex selected_page_count_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex dispatch_args_uav { kInvalidShaderVisibleIndex };
  std::vector<ShaderVisibleIndex> hzb_mip_srv_indices {};
  std::vector<ShaderVisibleIndex> hzb_mip_uav_indices {};

  std::optional<std::uint32_t> dynamic_slice_index {};
  std::optional<graphics::ComputePipelineDesc> select_pages_pso {};
  std::optional<graphics::ComputePipelineDesc> prepare_dispatch_args_pso {};
  std::optional<graphics::ComputePipelineDesc> clear_scratch_rect_pso {};
  std::optional<graphics::ComputePipelineDesc> build_per_page_pso {};
  std::optional<graphics::ComputePipelineDesc> build_top_levels_pso {};

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_);
  ~Impl();

  auto EnsurePassConstantsBuffer(std::uint32_t required_slot_count) -> void;
  auto EnsureZeroFillBuffer(std::uint64_t required_size) -> void;
  auto EnsureSelectionBuffers(std::uint32_t physical_page_count) -> void;
  auto EnsureDispatchArgsBuffer() -> void;
  auto EnsureScratchTextures(std::uint32_t extent, oxygen::Format format)
    -> void;
  auto EnsureBufferViewIndex(Buffer& buffer,
    const graphics::BufferViewDescription& desc) -> ShaderVisibleIndex;
  auto EnsureTextureViewIndex(Texture& texture,
    const graphics::TextureViewDescription& desc) -> ShaderVisibleIndex;
  auto PrepareScratchViewIndices() -> void;
  auto PrepareHzbMipViewIndices(Texture& hzb_texture, std::uint32_t mip_count,
    oxygen::Format format) -> void;

  template <typename T>
  auto WritePassConstants(const T& constants) -> ShaderVisibleIndex;
};

namespace {

  auto MakeDispatchGroups(const std::uint32_t count,
    const std::uint32_t group_size) noexcept -> std::uint32_t
  {
    return count == 0U ? 0U : (count + group_size - 1U) / group_size;
  }

  auto MipExtent(const std::uint32_t base_extent,
    const std::uint32_t mip_level) noexcept -> std::uint32_t
  {
    return (std::max)(1U, base_extent >> mip_level);
  }

  auto ComputePageLocalMipCount(const std::uint32_t page_size_texels) noexcept
    -> std::uint32_t
  {
    return page_size_texels <= 1U ? 0U : std::bit_width(page_size_texels) - 1U;
  }

  auto FindSliceIndex(const renderer::vsm::VsmPhysicalPoolSnapshot& pool,
    const VsmPhysicalPoolSliceRole role) noexcept
    -> std::optional<std::uint32_t>
  {
    for (std::uint32_t i = 0; i < pool.slice_roles.size(); ++i) {
      if (pool.slice_roles[i] == role) {
        return i;
      }
    }
    return std::nullopt;
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

  auto WholeTextureSrvDesc(const oxygen::Format format)
    -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
      .view_type = ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = format,
      .dimension = oxygen::TextureType::kTexture2D,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      .is_read_only_dsv = false,
    };
  }

  auto WholeTextureUavDesc(const oxygen::Format format)
    -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
    .view_type = ResourceViewType::kTexture_UAV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = format,
    .dimension = oxygen::TextureType::kTexture2D,
    .sub_resources = {
      .base_mip_level = 0U,
      .num_mip_levels = 1U,
      .base_array_slice = 0U,
      .num_array_slices = 1U,
    },
    .is_read_only_dsv = false,
  };
  }

  auto SingleMipTextureSrvDesc(const oxygen::Format format,
    const std::uint32_t mip_level) -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
    .view_type = ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = format,
    .dimension = oxygen::TextureType::kTexture2D,
    .sub_resources = {
      .base_mip_level = mip_level,
      .num_mip_levels = 1U,
      .base_array_slice = 0U,
      .num_array_slices = 1U,
    },
    .is_read_only_dsv = false,
  };
  }

  auto SingleMipTextureUavDesc(const oxygen::Format format,
    const std::uint32_t mip_level) -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
    .view_type = ResourceViewType::kTexture_UAV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = format,
    .dimension = oxygen::TextureType::kTexture2D,
    .sub_resources = {
      .base_mip_level = mip_level,
      .num_mip_levels = 1U,
      .base_array_slice = 0U,
      .num_array_slices = 1U,
    },
    .is_read_only_dsv = false,
  };
  }

  auto ShadowSliceSrvDesc(const oxygen::Format depth_format,
    const std::uint32_t slice) -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
    .view_type = ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = depth_format == oxygen::Format::kDepth32
      ? oxygen::Format::kR32Float
      : depth_format,
    .dimension = oxygen::TextureType::kTexture2DArray,
    .sub_resources = {
      .base_mip_level = 0U,
      .num_mip_levels = 1U,
      .base_array_slice = slice,
      .num_array_slices = 1U,
    },
    .is_read_only_dsv = false,
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
      static_cast<std::uint32_t>(
        oxygen::bindless::generated::d3d12::RootParam::kViewConstants),
      context.view_constants->GetGPUVirtualAddress());
    recorder.SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(
        oxygen::bindless::generated::d3d12::RootParam::kRootConstants),
      0U, 0U);
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

    LOG_F(ERROR, "VSM HZB updater skipped because {} is unavailable", label);
    return false;
  }

  template <typename Resource>
  auto RegisterResourceIfNeeded(
    Graphics& gfx, const std::shared_ptr<Resource>& resource) -> void
  {
    if (resource == nullptr) {
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
    if (resource == nullptr) {
      return;
    }

    auto& registry = gfx.GetResourceRegistry();
    if (registry.Contains(*resource)) {
      registry.UnRegisterResource(*resource);
    }
  }

} // namespace

VsmHzbUpdaterPass::Impl::Impl(
  observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
  : gfx(gfx_)
  , config(std::move(config_))
{
}

VsmHzbUpdaterPass::Impl::~Impl()
{
  if (pass_constants_buffer != nullptr
    && pass_constants_mapped_ptr != nullptr) {
    pass_constants_buffer->UnMap();
    pass_constants_mapped_ptr = nullptr;
  }
  if (zero_fill_buffer != nullptr && zero_fill_ptr != nullptr) {
    zero_fill_buffer->UnMap();
    zero_fill_ptr = nullptr;
  }

  if (gfx == nullptr) {
    return;
  }

  UnregisterResourceIfPresent(*gfx, pass_constants_buffer);
  UnregisterResourceIfPresent(*gfx, zero_fill_buffer);
  UnregisterResourceIfPresent(*gfx, selected_page_index_buffer);
  UnregisterResourceIfPresent(*gfx, selected_page_count_buffer);
  UnregisterResourceIfPresent(*gfx, dispatch_args_buffer);
  for (auto& scratch_texture : scratch_textures) {
    UnregisterResourceIfPresent(*gfx, scratch_texture);
  }
}

auto VsmHzbUpdaterPass::Impl::EnsurePassConstantsBuffer(
  const std::uint32_t required_slot_count) -> void
{
  CHECK_GT_F(required_slot_count, 0U,
    "VSM HZB updater requires at least one pass-constants slot");

  if (pass_constants_buffer != nullptr && pass_constants_mapped_ptr != nullptr
    && pass_constants_slot_count >= required_slot_count
    && pass_constants_indices.size() >= required_slot_count) {
    return;
  }

  if (pass_constants_buffer != nullptr
    && pass_constants_mapped_ptr != nullptr) {
    pass_constants_buffer->UnMap();
    pass_constants_mapped_ptr = nullptr;
  }

  UnregisterResourceIfPresent(*gfx, pass_constants_buffer);

  pass_constants_slot_count = required_slot_count;
  const auto pass_constants_stride
    = static_cast<std::uint64_t>(packing::kConstantBufferAlignment);
  pass_constants_indices.assign(
    pass_constants_slot_count, kInvalidShaderVisibleIndex);

  pass_constants_buffer = gfx->CreateBuffer(BufferDesc {
    .size_bytes = pass_constants_stride * pass_constants_slot_count,
    .usage = BufferUsage::kConstant,
    .memory = BufferMemory::kUpload,
    .debug_name = config->debug_name + ".PassConstants",
  });
  CHECK_NOTNULL_F(pass_constants_buffer.get(),
    "Failed to create VSM HZB pass-constants buffer");
  RegisterResourceIfNeeded(*gfx, pass_constants_buffer);
  pass_constants_mapped_ptr = pass_constants_buffer->Map(
    0U, pass_constants_stride * pass_constants_slot_count);
  CHECK_NOTNULL_F(
    pass_constants_mapped_ptr, "Failed to map VSM HZB pass-constants buffer");

  auto& allocator = gfx->GetDescriptorAllocator();
  auto& registry = gfx->GetResourceRegistry();
  for (std::uint32_t slot = 0U; slot < pass_constants_slot_count; ++slot) {
    auto handle = allocator.AllocateRaw(ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(),
      "Failed to allocate VSM HZB pass-constants CBV descriptor");
    pass_constants_indices[slot] = allocator.GetShaderVisibleIndex(handle);

    const auto offset
      = static_cast<std::uint64_t>(slot) * pass_constants_stride;
    const auto desc = graphics::BufferViewDescription {
      .view_type = ResourceViewType::kConstantBuffer,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { offset, pass_constants_stride },
      .stride = 0U,
    };
    const auto view
      = registry.RegisterView(*pass_constants_buffer, std::move(handle), desc);
    CHECK_F(
      view->IsValid(), "Failed to register VSM HZB pass-constants CBV view");
  }
}

auto VsmHzbUpdaterPass::Impl::EnsureZeroFillBuffer(
  const std::uint64_t required_size) -> void
{
  if (zero_fill_buffer != nullptr && zero_fill_ptr != nullptr
    && zero_fill_capacity >= required_size) {
    return;
  }

  if (zero_fill_buffer != nullptr && zero_fill_ptr != nullptr) {
    zero_fill_buffer->UnMap();
    zero_fill_ptr = nullptr;
  }

  zero_fill_capacity = (std::max)(required_size, 4ULL);
  zero_fill_buffer = gfx->CreateBuffer(BufferDesc {
    .size_bytes = zero_fill_capacity,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = config->debug_name + ".ZeroFill",
  });
  CHECK_NOTNULL_F(
    zero_fill_buffer.get(), "Failed to create VSM HZB zero-fill buffer");
  RegisterResourceIfNeeded(*gfx, zero_fill_buffer);
  zero_fill_ptr = zero_fill_buffer->Map(0U, zero_fill_capacity);
  CHECK_NOTNULL_F(zero_fill_ptr, "Failed to map VSM HZB zero-fill buffer");
  std::memset(zero_fill_ptr, 0, zero_fill_capacity);
}

auto VsmHzbUpdaterPass::Impl::EnsureSelectionBuffers(
  const std::uint32_t physical_page_count) -> void
{
  if (selected_page_index_buffer != nullptr
    && selected_page_count_buffer != nullptr
    && selected_page_capacity >= physical_page_count) {
    return;
  }

  selected_page_capacity = (std::max)(physical_page_count, 1U);
  const auto list_size_bytes
    = static_cast<std::uint64_t>(selected_page_capacity)
    * sizeof(std::uint32_t);

  selected_page_index_buffer = gfx->CreateBuffer(BufferDesc {
    .size_bytes = list_size_bytes,
    .usage = BufferUsage::kStorage,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = config->debug_name + ".SelectedPages",
  });
  CHECK_NOTNULL_F(selected_page_index_buffer.get(),
    "Failed to create VSM HZB selected-pages buffer");
  RegisterResourceIfNeeded(*gfx, selected_page_index_buffer);

  selected_page_count_buffer = gfx->CreateBuffer(BufferDesc {
    .size_bytes = sizeof(std::uint32_t),
    .usage = BufferUsage::kStorage,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = config->debug_name + ".SelectedPageCount",
  });
  CHECK_NOTNULL_F(selected_page_count_buffer.get(),
    "Failed to create VSM HZB selected-page-count buffer");
  RegisterResourceIfNeeded(*gfx, selected_page_count_buffer);

  selected_pages_srv = kInvalidShaderVisibleIndex;
  selected_pages_uav = kInvalidShaderVisibleIndex;
  selected_page_count_srv = kInvalidShaderVisibleIndex;
  selected_page_count_uav = kInvalidShaderVisibleIndex;
}

auto VsmHzbUpdaterPass::Impl::EnsureDispatchArgsBuffer() -> void
{
  if (dispatch_args_buffer != nullptr) {
    return;
  }

  dispatch_args_buffer = gfx->CreateBuffer(BufferDesc {
    .size_bytes = sizeof(std::uint32_t) * 3U,
    .usage = BufferUsage::kStorage | BufferUsage::kIndirect,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = config->debug_name + ".PerPageDispatchArgs",
  });
  CHECK_NOTNULL_F(dispatch_args_buffer.get(),
    "Failed to create VSM HZB dispatch-args buffer");
  RegisterResourceIfNeeded(*gfx, dispatch_args_buffer);
  dispatch_args_uav = kInvalidShaderVisibleIndex;
}

auto VsmHzbUpdaterPass::Impl::EnsureScratchTextures(
  const std::uint32_t extent, const oxygen::Format format) -> void
{
  if (scratch_textures[0] != nullptr && scratch_textures[1] != nullptr
    && scratch_extent == extent && scratch_format == format) {
    return;
  }

  for (auto& scratch_texture : scratch_textures) {
    UnregisterResourceIfPresent(*gfx, scratch_texture);
    scratch_texture.reset();
  }

  for (std::uint32_t slot = 0U; slot < scratch_textures.size(); ++slot) {
    auto desc = graphics::TextureDesc {};
    desc.width = extent;
    desc.height = extent;
    desc.array_size = 1U;
    desc.mip_levels = 1U;
    desc.format = format;
    desc.texture_type = oxygen::TextureType::kTexture2D;
    desc.is_shader_resource = true;
    desc.is_uav = true;
    desc.initial_state = ResourceStates::kCommon;
    desc.debug_name = config->debug_name + ".Scratch" + std::to_string(slot);

    scratch_textures[slot] = gfx->CreateTexture(desc);
    CHECK_NOTNULL_F(scratch_textures[slot].get(),
      "Failed to create VSM HZB scratch texture {}", slot);
    RegisterResourceIfNeeded(*gfx, scratch_textures[slot]);
  }

  scratch_extent = extent;
  scratch_format = format;
  scratch_srv_indices.fill(kInvalidShaderVisibleIndex);
  scratch_uav_indices.fill(kInvalidShaderVisibleIndex);
}

auto VsmHzbUpdaterPass::Impl::EnsureBufferViewIndex(Buffer& buffer,
  const graphics::BufferViewDescription& desc) -> ShaderVisibleIndex
{
  auto& registry = gfx->GetResourceRegistry();
  if (const auto existing = registry.FindShaderVisibleIndex(buffer, desc);
    existing.has_value()) {
    return *existing;
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(desc.view_type, desc.visibility);
  CHECK_F(handle.IsValid(), "Failed to allocate VSM HZB buffer view");
  const auto index = allocator.GetShaderVisibleIndex(handle);
  const auto view = registry.RegisterView(buffer, std::move(handle), desc);
  CHECK_F(view->IsValid(), "Failed to register VSM HZB buffer view");
  return index;
}

auto VsmHzbUpdaterPass::Impl::EnsureTextureViewIndex(Texture& texture,
  const graphics::TextureViewDescription& desc) -> ShaderVisibleIndex
{
  auto& registry = gfx->GetResourceRegistry();
  if (const auto existing = registry.FindShaderVisibleIndex(texture, desc);
    existing.has_value()) {
    return *existing;
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(desc.view_type, desc.visibility);
  CHECK_F(handle.IsValid(), "Failed to allocate VSM HZB texture view");
  const auto index = allocator.GetShaderVisibleIndex(handle);
  const auto view = registry.RegisterView(texture, std::move(handle), desc);
  CHECK_F(view->IsValid(), "Failed to register VSM HZB texture view");
  return index;
}

auto VsmHzbUpdaterPass::Impl::PrepareScratchViewIndices() -> void
{
  for (std::uint32_t slot = 0U; slot < scratch_textures.size(); ++slot) {
    scratch_srv_indices[slot] = EnsureTextureViewIndex(
      *scratch_textures[slot], WholeTextureSrvDesc(scratch_format));
    scratch_uav_indices[slot] = EnsureTextureViewIndex(
      *scratch_textures[slot], WholeTextureUavDesc(scratch_format));
  }
}

auto VsmHzbUpdaterPass::Impl::PrepareHzbMipViewIndices(Texture& hzb_texture,
  const std::uint32_t mip_count, const oxygen::Format format) -> void
{
  hzb_mip_srv_indices.resize(mip_count, kInvalidShaderVisibleIndex);
  hzb_mip_uav_indices.resize(mip_count, kInvalidShaderVisibleIndex);
  for (std::uint32_t mip_level = 0U; mip_level < mip_count; ++mip_level) {
    hzb_mip_srv_indices[mip_level] = EnsureTextureViewIndex(
      hzb_texture, SingleMipTextureSrvDesc(format, mip_level));
    hzb_mip_uav_indices[mip_level] = EnsureTextureViewIndex(
      hzb_texture, SingleMipTextureUavDesc(format, mip_level));
  }
}

template <typename T>
auto VsmHzbUpdaterPass::Impl::WritePassConstants(const T& constants)
  -> ShaderVisibleIndex
{
  CHECK_NOTNULL_F(pass_constants_mapped_ptr,
    "VSM HZB updater pass constants must be mapped before writing");
  CHECK_LT_F(next_pass_constants_slot, pass_constants_slot_count,
    "VSM HZB updater exhausted its pass-constants slots");
  const auto slot = next_pass_constants_slot++;
  const auto pass_constants_stride
    = static_cast<std::size_t>(packing::kConstantBufferAlignment);
  auto* destination = static_cast<std::byte*>(pass_constants_mapped_ptr)
    + static_cast<std::size_t>(slot) * pass_constants_stride;
  std::memcpy(destination, &constants, sizeof(constants));
  return pass_constants_indices.at(slot);
}

VsmHzbUpdaterPass::VsmHzbUpdaterPass(
  observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "VsmHzbUpdaterPass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
  CHECK_NOTNULL_F(
    impl_->config.get(), "VsmHzbUpdaterPass requires a non-null config");
  ValidateConfig();
}

VsmHzbUpdaterPass::~VsmHzbUpdaterPass() = default;

auto VsmHzbUpdaterPass::SetInput(VsmHzbUpdaterPassInput input) -> void
{
  impl_->input = std::move(input);
}

auto VsmHzbUpdaterPass::ResetInput() noexcept -> void
{
  impl_->input.reset();
  impl_->resources_prepared = false;
}

auto VsmHzbUpdaterPass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  impl_->resources_prepared = false;
  impl_->dynamic_slice_index.reset();

  if (!impl_->input.has_value()) {
    DLOG_F(2, "VSM HZB updater skipped because no input is set");
    co_return;
  }

  const auto& input = *impl_->input;
  if (!input.frame.is_ready) {
    LOG_F(WARNING, "VSM HZB updater skipped because frame input is not ready");
    co_return;
  }
  if (!input.physical_pool.is_available
    || input.physical_pool.shadow_texture == nullptr) {
    LOG_F(WARNING,
      "VSM HZB updater skipped because the physical shadow pool is "
      "unavailable");
    co_return;
  }
  if (!input.hzb_pool.is_available || input.hzb_pool.texture == nullptr) {
    LOG_F(
      WARNING, "VSM HZB updater skipped because the HZB pool is unavailable");
    co_return;
  }
  if (input.frame.dirty_flags_buffer == nullptr
    || input.frame.physical_page_meta_buffer == nullptr) {
    LOG_F(ERROR,
      "VSM HZB updater skipped because dirty flags or physical metadata are "
      "unavailable");
    co_return;
  }
  if (input.physical_pool.page_size_texels < 2U) {
    LOG_F(ERROR, "VSM HZB updater requires page_size_texels >= 2 (got {})",
      input.physical_pool.page_size_texels);
    co_return;
  }

  impl_->dynamic_slice_index = FindSliceIndex(
    input.physical_pool, VsmPhysicalPoolSliceRole::kDynamicDepth);
  CHECK_F(impl_->dynamic_slice_index.has_value(),
    "VSM HZB updater requires a dynamic shadow slice");

  auto shadow_texture
    = std::const_pointer_cast<Texture>(input.physical_pool.shadow_texture);
  auto hzb_texture = std::const_pointer_cast<Texture>(input.hzb_pool.texture);
  auto dirty_flags_buffer
    = std::const_pointer_cast<Buffer>(input.frame.dirty_flags_buffer);
  auto physical_meta_buffer
    = std::const_pointer_cast<Buffer>(input.frame.physical_page_meta_buffer);

  CHECK_NOTNULL_F(
    shadow_texture.get(), "VSM HZB updater requires a shadow texture");
  CHECK_NOTNULL_F(hzb_texture.get(), "VSM HZB updater requires an HZB texture");
  CHECK_NOTNULL_F(
    dirty_flags_buffer.get(), "VSM HZB updater requires a dirty-flags buffer");
  CHECK_NOTNULL_F(physical_meta_buffer.get(),
    "VSM HZB updater requires a physical-metadata buffer");

  impl_->EnsurePassConstantsBuffer(1U + 3U * input.hzb_pool.mip_count);
  impl_->EnsureZeroFillBuffer(sizeof(std::uint32_t));
  impl_->EnsureSelectionBuffers(input.physical_pool.tile_capacity);
  impl_->EnsureDispatchArgsBuffer();
  impl_->EnsureScratchTextures(input.hzb_pool.width, input.hzb_pool.format);
  impl_->PrepareScratchViewIndices();
  impl_->PrepareHzbMipViewIndices(
    *hzb_texture, input.hzb_pool.mip_count, input.hzb_pool.format);

  impl_->shadow_depth_srv = impl_->EnsureTextureViewIndex(*shadow_texture,
    ShadowSliceSrvDesc(
      input.physical_pool.depth_format, *impl_->dynamic_slice_index));
  impl_->dirty_flags_uav = impl_->EnsureBufferViewIndex(*dirty_flags_buffer,
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
      dirty_flags_buffer->GetDescriptor().size_bytes, sizeof(std::uint32_t)));
  impl_->physical_meta_uav = impl_->EnsureBufferViewIndex(*physical_meta_buffer,
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
      physical_meta_buffer->GetDescriptor().size_bytes,
      sizeof(renderer::vsm::VsmPhysicalPageMeta)));
  impl_->selected_pages_srv
    = impl_->EnsureBufferViewIndex(*impl_->selected_page_index_buffer,
      MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
        impl_->selected_page_index_buffer->GetDescriptor().size_bytes,
        sizeof(std::uint32_t)));
  impl_->selected_pages_uav
    = impl_->EnsureBufferViewIndex(*impl_->selected_page_index_buffer,
      MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
        impl_->selected_page_index_buffer->GetDescriptor().size_bytes,
        sizeof(std::uint32_t)));
  impl_->selected_page_count_srv
    = impl_->EnsureBufferViewIndex(*impl_->selected_page_count_buffer,
      MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
        impl_->selected_page_count_buffer->GetDescriptor().size_bytes,
        sizeof(std::uint32_t)));
  impl_->selected_page_count_uav
    = impl_->EnsureBufferViewIndex(*impl_->selected_page_count_buffer,
      MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
        impl_->selected_page_count_buffer->GetDescriptor().size_bytes,
        sizeof(std::uint32_t)));
  impl_->dispatch_args_uav
    = impl_->EnsureBufferViewIndex(*impl_->dispatch_args_buffer,
      MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_UAV,
        impl_->dispatch_args_buffer->GetDescriptor().size_bytes,
        sizeof(std::uint32_t)));

  if (impl_->pass_constants_indices.empty()
    || !RequireValidIndex(
      impl_->pass_constants_indices.front(), "pass-constants CBV")
    || !RequireValidIndex(impl_->shadow_depth_srv, "dynamic shadow SRV")
    || !RequireValidIndex(impl_->dirty_flags_uav, "dirty-flags UAV")
    || !RequireValidIndex(impl_->physical_meta_uav, "physical-metadata UAV")
    || !RequireValidIndex(impl_->selected_pages_srv, "selected-pages SRV")
    || !RequireValidIndex(impl_->selected_pages_uav, "selected-pages UAV")
    || !RequireValidIndex(
      impl_->selected_page_count_srv, "selected-page-count SRV")
    || !RequireValidIndex(
      impl_->selected_page_count_uav, "selected-page-count UAV")
    || !RequireValidIndex(impl_->dispatch_args_uav, "dispatch-args UAV")) {
    co_return;
  }
  for (std::uint32_t slot = 0U; slot < impl_->scratch_textures.size(); ++slot) {
    if (!RequireValidIndex(
          impl_->scratch_srv_indices[slot], "scratch texture SRV")
      || !RequireValidIndex(
        impl_->scratch_uav_indices[slot], "scratch texture UAV")) {
      co_return;
    }
  }

  DLOG_F(2,
    "VSM HZB updater bindings pass_constants_slots={} first_pass_constants={} "
    "shadow_depth_srv={} dirty_flags_uav={} "
    "physical_meta_uav={} selected_pages_srv={} selected_pages_uav={} "
    "selected_page_count_srv={} selected_page_count_uav={} "
    "dispatch_args_uav={} "
    "dynamic_slice={}",
    impl_->pass_constants_slot_count,
    impl_->pass_constants_indices.front().get(), impl_->shadow_depth_srv.get(),
    impl_->dirty_flags_uav.get(), impl_->physical_meta_uav.get(),
    impl_->selected_pages_srv.get(), impl_->selected_pages_uav.get(),
    impl_->selected_page_count_srv.get(), impl_->selected_page_count_uav.get(),
    impl_->dispatch_args_uav.get(), *impl_->dynamic_slice_index);
  for (std::uint32_t mip_level = 0U; mip_level < input.hzb_pool.mip_count;
    ++mip_level) {
    if (!RequireValidIndex(impl_->hzb_mip_srv_indices[mip_level], "HZB mip SRV")
      || !RequireValidIndex(
        impl_->hzb_mip_uav_indices[mip_level], "HZB mip UAV")) {
      co_return;
    }
  }

  recorder.BeginTrackingResourceState(
    *shadow_texture, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *hzb_texture, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *dirty_flags_buffer, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *physical_meta_buffer, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *impl_->selected_page_index_buffer, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *impl_->selected_page_count_buffer, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *impl_->dispatch_args_buffer, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *impl_->pass_constants_buffer, ResourceStates::kGenericRead, true);
  recorder.BeginTrackingResourceState(
    *impl_->zero_fill_buffer, ResourceStates::kGenericRead, true);
  for (const auto& scratch_texture : impl_->scratch_textures) {
    recorder.BeginTrackingResourceState(
      *scratch_texture, ResourceStates::kCommon, true);
  }

  recorder.RequireResourceState(
    *impl_->selected_page_count_buffer, ResourceStates::kCopyDest);
  recorder.FlushBarriers();
  recorder.CopyBuffer(*impl_->selected_page_count_buffer, 0U,
    *impl_->zero_fill_buffer, 0U, sizeof(std::uint32_t));

  recorder.RequireResourceState(
    *dirty_flags_buffer, ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *physical_meta_buffer, ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *impl_->selected_page_index_buffer, ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *impl_->selected_page_count_buffer, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  impl_->resources_prepared = true;
  DLOG_F(2,
    "prepared VSM HZB updater generation={} page_size={} physical_pages={} "
    "hzb_extent={} hzb_mips={} preserve_existing={} force_all_pages={}",
    input.frame.snapshot.frame_generation, input.physical_pool.page_size_texels,
    input.physical_pool.tile_capacity, input.hzb_pool.width,
    input.hzb_pool.mip_count, input.can_preserve_existing_hzb_contents,
    input.force_rebuild_all_allocated_pages);
  co_return;
}

auto VsmHzbUpdaterPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  if (!impl_->resources_prepared || !impl_->input.has_value()
    || !impl_->dynamic_slice_index.has_value() || !impl_->select_pages_pso
    || !impl_->prepare_dispatch_args_pso || !impl_->clear_scratch_rect_pso
    || !impl_->build_per_page_pso || !impl_->build_top_levels_pso) {
    DLOG_F(2, "VSM HZB updater skipped execute");
    co_return;
  }

  const auto& input = *impl_->input;
  auto shadow_texture
    = std::const_pointer_cast<Texture>(input.physical_pool.shadow_texture);
  auto hzb_texture = std::const_pointer_cast<Texture>(input.hzb_pool.texture);
  auto dirty_flags_buffer
    = std::const_pointer_cast<Buffer>(input.frame.dirty_flags_buffer);
  auto physical_meta_buffer
    = std::const_pointer_cast<Buffer>(input.frame.physical_page_meta_buffer);

  CHECK_NOTNULL_F(
    shadow_texture.get(), "VSM HZB updater requires a shadow texture");
  CHECK_NOTNULL_F(hzb_texture.get(), "VSM HZB updater requires an HZB texture");
  CHECK_NOTNULL_F(
    dirty_flags_buffer.get(), "VSM HZB updater requires a dirty-flags buffer");
  CHECK_NOTNULL_F(physical_meta_buffer.get(),
    "VSM HZB updater requires a physical-metadata buffer");

  const auto page_local_mip_count
    = ComputePageLocalMipCount(input.physical_pool.page_size_texels);
  const auto low_mip_count
    = (std::min)(page_local_mip_count, input.hzb_pool.mip_count);
  const auto physical_page_count = input.physical_pool.tile_capacity;

  impl_->next_pass_constants_slot = 0U;

  const auto select_pages_constants_index
    = impl_->WritePassConstants(VsmHzbSelectPagesPassConstants {
      .dirty_flags_uav_index = impl_->dirty_flags_uav,
      .physical_meta_uav_index = impl_->physical_meta_uav,
      .selected_pages_uav_index = impl_->selected_pages_uav,
      .selected_page_count_uav_index = impl_->selected_page_count_uav,
      .physical_page_count = physical_page_count,
      .force_rebuild_all_allocated_pages
      = input.force_rebuild_all_allocated_pages ? 1U : 0U,
      .tiles_per_axis = input.physical_pool.tiles_per_axis,
      .dynamic_slice_index = *impl_->dynamic_slice_index,
    });
  SetPassConstantsIndex(select_pages_constants_index);
  BindComputeStage(recorder, *impl_->select_pages_pso,
    select_pages_constants_index, Context());
  recorder.Dispatch(
    MakeDispatchGroups(physical_page_count, kSelectPagesThreadGroupSize), 1U,
    1U);

  recorder.RequireResourceState(
    *impl_->selected_page_index_buffer, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *impl_->selected_page_count_buffer, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *shadow_texture, ResourceStates::kShaderResource);
  recorder.RequireResourceState(*dirty_flags_buffer, ResourceStates::kCommon);
  recorder.RequireResourceState(*physical_meta_buffer, ResourceStates::kCommon);
  recorder.FlushBarriers();

  std::uint32_t source_slot = 0U;
  for (std::uint32_t mip_level = 0U; mip_level < low_mip_count; ++mip_level) {
    const auto destination_width = MipExtent(input.hzb_pool.width, mip_level);
    const auto destination_height = MipExtent(input.hzb_pool.height, mip_level);
    const auto destination_page_extent
      = MipExtent(input.physical_pool.page_size_texels >> 1U, mip_level);
    const auto destination_slot = mip_level & 1U;

    if (input.can_preserve_existing_hzb_contents) {
      recorder.RequireResourceState(*hzb_texture, ResourceStates::kCopySource);
      recorder.RequireResourceState(
        *impl_->scratch_textures[destination_slot], ResourceStates::kCopyDest);
      recorder.FlushBarriers();
      recorder.CopyTexture(*hzb_texture,
        graphics::TextureSlice {
          .x = 0U,
          .y = 0U,
          .z = 0U,
          .width = destination_width,
          .height = destination_height,
          .depth = 1U,
          .mip_level = mip_level,
          .array_slice = 0U,
        },
        graphics::TextureSubResourceSet {
          .base_mip_level = mip_level,
          .num_mip_levels = 1U,
          .base_array_slice = 0U,
          .num_array_slices = 1U,
        },
        *impl_->scratch_textures[destination_slot],
        graphics::TextureSlice {
          .x = 0U,
          .y = 0U,
          .z = 0U,
          .width = destination_width,
          .height = destination_height,
          .depth = 1U,
          .mip_level = 0U,
          .array_slice = 0U,
        },
        graphics::TextureSubResourceSet {
          .base_mip_level = 0U,
          .num_mip_levels = 1U,
          .base_array_slice = 0U,
          .num_array_slices = 1U,
        });
    } else {
      const auto scratch_init_constants_index
        = impl_->WritePassConstants(VsmHzbScratchInitPassConstants {
          .destination_hzb_uav_index
          = impl_->scratch_uav_indices[destination_slot],
          .destination_width = destination_width,
          .destination_height = destination_height,
          .clear_depth = 0.0F,
        });
      SetPassConstantsIndex(scratch_init_constants_index);
      BindComputeStage(recorder, *impl_->clear_scratch_rect_pso,
        scratch_init_constants_index, Context());
      recorder.RequireResourceState(*impl_->scratch_textures[destination_slot],
        ResourceStates::kUnorderedAccess);
      recorder.FlushBarriers();
      recorder.Dispatch(
        MakeDispatchGroups(destination_width, kBuildHzbThreadGroupSize),
        MakeDispatchGroups(destination_height, kBuildHzbThreadGroupSize), 1U);
    }

    const auto per_page_build_constants_index
      = impl_->WritePassConstants(VsmHzbPerPageBuildPassConstants {
        .shadow_depth_srv_index = impl_->shadow_depth_srv,
        .source_hzb_srv_index = mip_level == 0U
          ? kInvalidShaderVisibleIndex
          : impl_->scratch_srv_indices[source_slot],
        .destination_hzb_uav_index
        = impl_->scratch_uav_indices[destination_slot],
        .selected_pages_srv_index = impl_->selected_pages_srv,
        .selected_page_count_srv_index = impl_->selected_page_count_srv,
        .page_size_texels = input.physical_pool.page_size_texels,
        .tiles_per_axis = input.physical_pool.tiles_per_axis,
        .destination_page_extent_texels = destination_page_extent,
        .source_is_shadow_depth = mip_level == 0U ? 1U : 0U,
      });
    const auto prepare_dispatch_args_constants_index
      = impl_->WritePassConstants(VsmHzbPrepareDispatchArgsPassConstants {
        .selected_page_count_srv_index = impl_->selected_page_count_srv,
        .dispatch_args_uav_index = impl_->dispatch_args_uav,
        .thread_group_count_x
        = MakeDispatchGroups(destination_page_extent, kBuildHzbThreadGroupSize),
        .thread_group_count_y
        = MakeDispatchGroups(destination_page_extent, kBuildHzbThreadGroupSize),
      });
    SetPassConstantsIndex(prepare_dispatch_args_constants_index);
    BindComputeStage(recorder, *impl_->prepare_dispatch_args_pso,
      prepare_dispatch_args_constants_index, Context());
    recorder.RequireResourceState(
      *impl_->selected_page_count_buffer, ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *impl_->dispatch_args_buffer, ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();
    recorder.Dispatch(1U, 1U, 1U);

    SetPassConstantsIndex(per_page_build_constants_index);
    BindComputeStage(recorder, *impl_->build_per_page_pso,
      per_page_build_constants_index, Context());
    if (mip_level > 0U) {
      recorder.RequireResourceState(
        *impl_->scratch_textures[source_slot], ResourceStates::kShaderResource);
    }
    recorder.RequireResourceState(*impl_->scratch_textures[destination_slot],
      ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(
      *impl_->dispatch_args_buffer, ResourceStates::kIndirectArgument);
    recorder.FlushBarriers();
    recorder.SetMarker("VsmHzbUpdaterPass.ExecuteIndirect.BuildPerPage");
    recorder.ExecuteIndirect(*impl_->dispatch_args_buffer, 0U, 1U,
      CommandRecorder::IndirectCommandLayout::kDispatch);

    recorder.RequireResourceState(
      *impl_->scratch_textures[destination_slot], ResourceStates::kCopySource);
    recorder.RequireResourceState(*hzb_texture, ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyTexture(*impl_->scratch_textures[destination_slot],
      graphics::TextureSlice {
        .x = 0U,
        .y = 0U,
        .z = 0U,
        .width = destination_width,
        .height = destination_height,
        .depth = 1U,
        .mip_level = 0U,
        .array_slice = 0U,
      },
      graphics::TextureSubResourceSet {
        .base_mip_level = 0U,
        .num_mip_levels = 1U,
        .base_array_slice = 0U,
        .num_array_slices = 1U,
      },
      *hzb_texture,
      graphics::TextureSlice {
        .x = 0U,
        .y = 0U,
        .z = 0U,
        .width = destination_width,
        .height = destination_height,
        .depth = 1U,
        .mip_level = mip_level,
        .array_slice = 0U,
      },
      graphics::TextureSubResourceSet {
        .base_mip_level = mip_level,
        .num_mip_levels = 1U,
        .base_array_slice = 0U,
        .num_array_slices = 1U,
      });

    source_slot = destination_slot;
  }

  for (std::uint32_t mip_level = low_mip_count;
    mip_level < input.hzb_pool.mip_count; ++mip_level) {
    const auto destination_width = MipExtent(input.hzb_pool.width, mip_level);
    const auto destination_height = MipExtent(input.hzb_pool.height, mip_level);
    const auto destination_slot = source_slot ^ 1U;

    const auto top_build_constants_index
      = impl_->WritePassConstants(VsmHzbTopBuildPassConstants {
        .source_hzb_srv_index = impl_->scratch_srv_indices[source_slot],
        .destination_hzb_uav_index
        = impl_->scratch_uav_indices[destination_slot],
        .destination_width = destination_width,
        .destination_height = destination_height,
      });
    SetPassConstantsIndex(top_build_constants_index);
    BindComputeStage(recorder, *impl_->build_top_levels_pso,
      top_build_constants_index, Context());
    recorder.RequireResourceState(
      *impl_->scratch_textures[source_slot], ResourceStates::kShaderResource);
    recorder.RequireResourceState(*impl_->scratch_textures[destination_slot],
      ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();
    recorder.Dispatch(
      MakeDispatchGroups(destination_width, kBuildHzbThreadGroupSize),
      MakeDispatchGroups(destination_height, kBuildHzbThreadGroupSize), 1U);

    recorder.RequireResourceState(
      *impl_->scratch_textures[destination_slot], ResourceStates::kCopySource);
    recorder.RequireResourceState(*hzb_texture, ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyTexture(*impl_->scratch_textures[destination_slot],
      graphics::TextureSlice {
        .x = 0U,
        .y = 0U,
        .z = 0U,
        .width = destination_width,
        .height = destination_height,
        .depth = 1U,
        .mip_level = 0U,
        .array_slice = 0U,
      },
      graphics::TextureSubResourceSet {
        .base_mip_level = 0U,
        .num_mip_levels = 1U,
        .base_array_slice = 0U,
        .num_array_slices = 1U,
      },
      *hzb_texture,
      graphics::TextureSlice {
        .x = 0U,
        .y = 0U,
        .z = 0U,
        .width = destination_width,
        .height = destination_height,
        .depth = 1U,
        .mip_level = mip_level,
        .array_slice = 0U,
      },
      graphics::TextureSubResourceSet {
        .base_mip_level = mip_level,
        .num_mip_levels = 1U,
        .base_array_slice = 0U,
        .num_array_slices = 1U,
      });

    source_slot = destination_slot;
  }

  recorder.RequireResourceState(*hzb_texture, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *shadow_texture, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *impl_->selected_page_index_buffer, ResourceStates::kCommon);
  recorder.RequireResourceState(
    *impl_->selected_page_count_buffer, ResourceStates::kCommon);
  recorder.RequireResourceState(
    *impl_->dispatch_args_buffer, ResourceStates::kCommon);
  for (const auto& scratch_texture : impl_->scratch_textures) {
    recorder.RequireResourceState(*scratch_texture, ResourceStates::kCommon);
  }
  recorder.FlushBarriers();

  DLOG_F(2,
    "executed VSM HZB updater generation={} page_size={} low_mips={} "
    "total_mips={} preserve_existing={} force_all_pages={}",
    input.frame.snapshot.frame_generation, input.physical_pool.page_size_texels,
    low_mip_count, input.hzb_pool.mip_count,
    input.can_preserve_existing_hzb_contents,
    input.force_rebuild_all_allocated_pages);
  co_return;
}

auto VsmHzbUpdaterPass::ValidateConfig() -> void
{
  if (impl_->gfx == nullptr) {
    throw std::runtime_error("VsmHzbUpdaterPass requires Graphics");
  }
}

auto VsmHzbUpdaterPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();
  const auto build_pso = [&](const char* entry_point, const char* debug_name) {
    graphics::ShaderRequest shader_request {
      .stage = oxygen::ShaderType::kCompute,
      .source_path = "Renderer/Vsm/VsmHzbBuild.hlsl",
      .entry_point = entry_point,
    };

    return ComputePipelineDesc::Builder()
      .SetComputeShader(std::move(shader_request))
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        generated_bindings.data(), generated_bindings.size()))
      .SetDebugName(debug_name)
      .Build();
  };

  impl_->select_pages_pso
    = build_pso("CS_SelectPages", "VsmHzbSelectPages_PSO");
  impl_->prepare_dispatch_args_pso
    = build_pso("CS_PrepareDispatchArgs", "VsmHzbPrepareDispatchArgs_PSO");
  impl_->clear_scratch_rect_pso
    = build_pso("CS_ClearScratchRect", "VsmHzbClearScratchRect_PSO");
  impl_->build_per_page_pso
    = build_pso("CS_BuildPerPage", "VsmHzbBuildPerPage_PSO");
  impl_->build_top_levels_pso
    = build_pso("CS_BuildTopLevels", "VsmHzbBuildTopLevels_PSO");
  impl_->pipelines_ready = true;
  return *impl_->select_pages_pso;
}

auto VsmHzbUpdaterPass::NeedRebuildPipelineState() const -> bool
{
  return !impl_->pipelines_ready;
}

} // namespace oxygen::engine
