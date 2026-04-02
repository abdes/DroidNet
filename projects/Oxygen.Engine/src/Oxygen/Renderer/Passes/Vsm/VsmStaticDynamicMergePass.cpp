//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/Vsm/VsmStaticDynamicMergePass.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

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
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>

using oxygen::Graphics;
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

  constexpr std::uint32_t kMergeThreadGroupSize = 8U;
  constexpr std::uint32_t kPassConstantsSlotCount = 1U;

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmStaticDynamicMergePassConstants {
    ShaderVisibleIndex static_shadow_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex dynamic_shadow_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex dirty_flags_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex physical_meta_srv_index { kInvalidShaderVisibleIndex };
    std::uint32_t page_size_texels { 0U };
    std::uint32_t tiles_per_axis { 0U };
    std::uint32_t logical_page_index { 0U };
    std::uint32_t logical_page_count { 0U };
    std::uint32_t dynamic_slice_index { 0U };
    std::uint32_t static_slice_index { 0U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
  };
  static_assert(sizeof(VsmStaticDynamicMergePassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

  [[nodiscard]] auto MakeDispatchGroups(const std::uint32_t count,
    const std::uint32_t group_size) noexcept -> std::uint32_t
  {
    return count == 0U ? 0U : (count + group_size - 1U) / group_size;
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

  [[nodiscard]] auto ShadowSliceSrvDesc(const oxygen::Format depth_format,
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

  [[nodiscard]] auto ScratchUavDesc() -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
      .view_type = ResourceViewType::kTexture_UAV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = oxygen::Format::kR32Float,
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

  [[nodiscard]] auto ResolveMergeScratchFormat(
    const oxygen::Format depth_format) noexcept -> oxygen::Format
  {
    switch (depth_format) {
    case oxygen::Format::kDepth32:
      return oxygen::Format::kR32Float;
    default:
      return oxygen::Format::kUnknown;
    }
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

    LOG_F(ERROR,
      "VSM static/dynamic merge pass skipped because {} is unavailable", label);
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

  [[nodiscard]] auto BuildMergeCandidateLogicalPages(
    const std::span<const std::uint32_t> raw_candidates,
    const std::uint32_t logical_page_count) -> std::vector<std::uint32_t>
  {
    auto unique_candidates = std::vector<std::uint32_t> {};
    if (logical_page_count == 0U || raw_candidates.empty()) {
      return unique_candidates;
    }

    auto seen = std::vector<bool>(logical_page_count, false);
    unique_candidates.reserve(raw_candidates.size());
    for (const auto logical_page_index : raw_candidates) {
      if (logical_page_index >= logical_page_count) {
        continue;
      }
      if (seen[logical_page_index]) {
        continue;
      }
      seen[logical_page_index] = true;
      unique_candidates.push_back(logical_page_index);
    }
    return unique_candidates;
  }

} // namespace

struct VsmStaticDynamicMergePass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;

  std::optional<VsmStaticDynamicMergePassInput> input {};
  bool resources_prepared { false };
  bool pipelines_ready { false };

  std::shared_ptr<Buffer> pass_constants_buffer {};
  void* pass_constants_mapped_ptr { nullptr };
  std::vector<ShaderVisibleIndex> pass_constants_indices {};
  std::uint32_t pass_constants_slot_count { 0U };
  std::uint32_t next_pass_constants_slot { 0U };

  std::vector<std::uint32_t> merge_candidate_logical_pages {};

  std::shared_ptr<Texture> merge_scratch_texture {};
  std::uint32_t merge_scratch_extent { 0U };
  oxygen::Format merge_scratch_format { oxygen::Format::kUnknown };

  ShaderVisibleIndex static_shadow_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex merge_scratch_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex dirty_flags_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_meta_srv { kInvalidShaderVisibleIndex };

  std::optional<std::uint32_t> dynamic_slice_index {};
  std::optional<std::uint32_t> static_slice_index {};
  std::optional<graphics::ComputePipelineDesc> merge_page_pso {};

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl()
  {
    if (pass_constants_buffer != nullptr
      && pass_constants_mapped_ptr != nullptr) {
      pass_constants_buffer->UnMap();
      pass_constants_mapped_ptr = nullptr;
    }

    if (gfx == nullptr) {
      return;
    }

    UnregisterResourceIfPresent(*gfx, pass_constants_buffer);
    UnregisterResourceIfPresent(*gfx, merge_scratch_texture);
  }

  auto EnsurePassConstantsBuffer(const std::uint32_t required_slot_count)
    -> void
  {
    CHECK_GT_F(required_slot_count, 0U,
      "VSM static/dynamic merge requires at least one pass-constants slot");

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
    pass_constants_indices.assign(
      pass_constants_slot_count, kInvalidShaderVisibleIndex);
    const auto pass_constants_stride
      = static_cast<std::uint64_t>(packing::kConstantBufferAlignment);

    pass_constants_buffer = gfx->CreateBuffer(BufferDesc {
      .size_bytes = pass_constants_stride * pass_constants_slot_count,
      .usage = BufferUsage::kConstant,
      .memory = BufferMemory::kUpload,
      .debug_name = config->debug_name + ".PassConstants",
    });
    CHECK_NOTNULL_F(pass_constants_buffer.get(),
      "Failed to create VSM merge pass-constants buffer");
    RegisterResourceIfNeeded(*gfx, pass_constants_buffer);
    pass_constants_mapped_ptr = pass_constants_buffer->Map(
      0U, pass_constants_stride * pass_constants_slot_count);
    CHECK_NOTNULL_F(pass_constants_mapped_ptr,
      "Failed to map VSM merge pass-constants buffer");

    auto& allocator = gfx->GetDescriptorAllocator();
    auto& registry = gfx->GetResourceRegistry();
    for (std::uint32_t slot = 0U; slot < pass_constants_slot_count; ++slot) {
      auto handle = allocator.AllocateRaw(ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
      CHECK_F(handle.IsValid(),
        "Failed to allocate VSM merge pass-constants CBV descriptor");
      pass_constants_indices[slot] = allocator.GetShaderVisibleIndex(handle);

      const auto offset
        = static_cast<std::uint64_t>(slot) * pass_constants_stride;
      const auto desc = graphics::BufferViewDescription {
        .view_type = ResourceViewType::kConstantBuffer,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .range = { offset, pass_constants_stride },
        .stride = 0U,
      };
      const auto view = registry.RegisterView(
        *pass_constants_buffer, std::move(handle), desc);
      CHECK_F(view->IsValid(),
        "Failed to register VSM merge pass-constants CBV view");
    }
  }

  auto EnsureMergeScratchTexture(const std::uint32_t page_size_texels,
    const oxygen::Format depth_format) -> void
  {
    const auto scratch_format = ResolveMergeScratchFormat(depth_format);
    CHECK_F(scratch_format != oxygen::Format::kUnknown,
      "VSM static/dynamic merge requires a supported depth format "
      "(depth_format={})",
      static_cast<int>(depth_format));

    if (merge_scratch_texture != nullptr
      && merge_scratch_extent == page_size_texels
      && merge_scratch_format == scratch_format) {
      return;
    }

    UnregisterResourceIfPresent(*gfx, merge_scratch_texture);
    merge_scratch_texture.reset();

    auto desc = graphics::TextureDesc {};
    desc.width = page_size_texels;
    desc.height = page_size_texels;
    desc.array_size = 1U;
    desc.mip_levels = 1U;
    desc.format = scratch_format;
    desc.texture_type = oxygen::TextureType::kTexture2D;
    desc.is_uav = true;
    desc.is_shader_resource = false;
    desc.initial_state = ResourceStates::kCommon;
    desc.debug_name = config->debug_name + ".MergeScratch";

    merge_scratch_texture = gfx->CreateTexture(desc);
    CHECK_NOTNULL_F(merge_scratch_texture.get(),
      "Failed to create VSM merge scratch texture");
    RegisterResourceIfNeeded(*gfx, merge_scratch_texture);

    merge_scratch_extent = page_size_texels;
    merge_scratch_format = scratch_format;
    merge_scratch_uav = kInvalidShaderVisibleIndex;
  }

  auto EnsureBufferViewIndex(Buffer& buffer,
    const graphics::BufferViewDescription& desc) -> ShaderVisibleIndex
  {
    auto& registry = gfx->GetResourceRegistry();
    if (const auto existing = registry.FindShaderVisibleIndex(buffer, desc);
      existing.has_value()) {
      return *existing;
    }

    auto& allocator = gfx->GetDescriptorAllocator();
    auto handle = allocator.AllocateRaw(desc.view_type, desc.visibility);
    CHECK_F(handle.IsValid(), "Failed to allocate VSM merge buffer view");
    const auto index = allocator.GetShaderVisibleIndex(handle);
    const auto view = registry.RegisterView(buffer, std::move(handle), desc);
    CHECK_F(view->IsValid(), "Failed to register VSM merge buffer view");
    return index;
  }

  auto EnsureTextureViewIndex(Texture& texture,
    const graphics::TextureViewDescription& desc) -> ShaderVisibleIndex
  {
    auto& registry = gfx->GetResourceRegistry();
    if (const auto existing = registry.FindShaderVisibleIndex(texture, desc);
      existing.has_value()) {
      return *existing;
    }

    auto& allocator = gfx->GetDescriptorAllocator();
    auto handle = allocator.AllocateRaw(desc.view_type, desc.visibility);
    CHECK_F(handle.IsValid(), "Failed to allocate VSM merge texture view");
    const auto index = allocator.GetShaderVisibleIndex(handle);
    const auto view = registry.RegisterView(texture, std::move(handle), desc);
    CHECK_F(view->IsValid(), "Failed to register VSM merge texture view");
    return index;
  }

  template <typename T>
  auto WritePassConstants(const T& constants) -> ShaderVisibleIndex
  {
    CHECK_NOTNULL_F(pass_constants_mapped_ptr,
      "VSM merge pass constants must be mapped before writing");
    CHECK_LT_F(next_pass_constants_slot, pass_constants_slot_count,
      "VSM merge exhausted its pass-constants slots");
    const auto slot = next_pass_constants_slot++;
    const auto pass_constants_stride
      = static_cast<std::size_t>(packing::kConstantBufferAlignment);
    auto* destination = static_cast<std::byte*>(pass_constants_mapped_ptr)
      + static_cast<std::size_t>(slot) * pass_constants_stride;
    std::memcpy(destination, &constants, sizeof(constants));
    return pass_constants_indices.at(slot);
  }
};

VsmStaticDynamicMergePass::VsmStaticDynamicMergePass(
  observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "VsmStaticDynamicMergePass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
  CHECK_NOTNULL_F(impl_->config.get(),
    "VsmStaticDynamicMergePass requires a non-null config");
  ValidateConfig();
}

VsmStaticDynamicMergePass::~VsmStaticDynamicMergePass() = default;

auto VsmStaticDynamicMergePass::SetInput(VsmStaticDynamicMergePassInput input)
  -> void
{
  impl_->input = std::move(input);
}

auto VsmStaticDynamicMergePass::ResetInput() noexcept -> void
{
  impl_->input.reset();
  impl_->resources_prepared = false;
  impl_->merge_candidate_logical_pages.clear();
}

auto VsmStaticDynamicMergePass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  impl_->resources_prepared = false;
  impl_->dynamic_slice_index.reset();
  impl_->static_slice_index.reset();
  impl_->merge_candidate_logical_pages.clear();

  if (!impl_->input.has_value()) {
    DLOG_F(2, "VSM static/dynamic merge pass skipped because no input is set");
    co_return;
  }

  const auto& input = *impl_->input;
  if (!input.frame.is_ready) {
    LOG_F(WARNING,
      "VSM static/dynamic merge pass skipped because frame input is not ready");
    co_return;
  }
  if (!input.physical_pool.is_available
    || input.physical_pool.shadow_texture == nullptr) {
    LOG_F(WARNING,
      "VSM static/dynamic merge pass skipped because the physical shadow pool "
      "is unavailable");
    co_return;
  }
  if (input.frame.dirty_flags_buffer == nullptr
    || input.frame.physical_page_meta_buffer == nullptr) {
    LOG_F(ERROR,
      "VSM static/dynamic merge pass skipped because dirty flags or physical "
      "metadata are unavailable");
    co_return;
  }

  impl_->dynamic_slice_index = FindSliceIndex(
    input.physical_pool, VsmPhysicalPoolSliceRole::kDynamicDepth);
  CHECK_F(impl_->dynamic_slice_index.has_value(),
    "VSM static/dynamic merge requires a dynamic shadow slice");
  impl_->static_slice_index = FindSliceIndex(
    input.physical_pool, VsmPhysicalPoolSliceRole::kStaticDepth);
  if (!impl_->static_slice_index.has_value()) {
    DLOG_F(2,
      "VSM static/dynamic merge pass bypassed because static caching is "
      "disabled");
    co_return;
  }

  const auto logical_page_count
    = input.physical_pool.tiles_per_axis * input.physical_pool.tiles_per_axis;
  CHECK_GT_F(logical_page_count, 0U,
    "VSM static/dynamic merge requires at least one logical page slot");

  impl_->merge_candidate_logical_pages = BuildMergeCandidateLogicalPages(
    input.merge_candidate_logical_pages, logical_page_count);
  if (impl_->merge_candidate_logical_pages.empty()) {
    DLOG_F(2,
      "VSM static/dynamic merge pass skipped because no static merge "
      "candidates were provided");
    co_return;
  }

  auto shadow_texture
    = std::const_pointer_cast<Texture>(input.physical_pool.shadow_texture);
  auto dirty_flags_buffer
    = std::const_pointer_cast<Buffer>(input.frame.dirty_flags_buffer);
  auto physical_meta_buffer
    = std::const_pointer_cast<Buffer>(input.frame.physical_page_meta_buffer);

  CHECK_NOTNULL_F(
    shadow_texture.get(), "VSM static/dynamic merge requires a shadow texture");
  CHECK_NOTNULL_F(dirty_flags_buffer.get(),
    "VSM static/dynamic merge requires a dirty-flags buffer");
  CHECK_NOTNULL_F(physical_meta_buffer.get(),
    "VSM static/dynamic merge requires a physical-metadata buffer");

  RegisterResourceIfNeeded(*impl_->gfx, shadow_texture);
  RegisterResourceIfNeeded(*impl_->gfx, dirty_flags_buffer);
  RegisterResourceIfNeeded(*impl_->gfx, physical_meta_buffer);

  impl_->EnsurePassConstantsBuffer(kPassConstantsSlotCount);
  impl_->EnsureMergeScratchTexture(
    input.physical_pool.page_size_texels, input.physical_pool.depth_format);
  CHECK_NOTNULL_F(impl_->merge_scratch_texture.get(),
    "VSM static/dynamic merge requires a scratch texture");

  impl_->static_shadow_srv = impl_->EnsureTextureViewIndex(*shadow_texture,
    ShadowSliceSrvDesc(
      input.physical_pool.depth_format, *impl_->static_slice_index));
  impl_->merge_scratch_uav = impl_->EnsureTextureViewIndex(
    *impl_->merge_scratch_texture, ScratchUavDesc());
  impl_->dirty_flags_srv = impl_->EnsureBufferViewIndex(*dirty_flags_buffer,
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      dirty_flags_buffer->GetDescriptor().size_bytes, sizeof(std::uint32_t)));
  impl_->physical_meta_srv = impl_->EnsureBufferViewIndex(*physical_meta_buffer,
    MakeStructuredViewDesc(ResourceViewType::kStructuredBuffer_SRV,
      physical_meta_buffer->GetDescriptor().size_bytes,
      sizeof(renderer::vsm::VsmPhysicalPageMeta)));

  if (!RequireValidIndex(impl_->static_shadow_srv, "static-slice SRV")
    || !RequireValidIndex(impl_->merge_scratch_uav, "merge scratch UAV")
    || !RequireValidIndex(impl_->dirty_flags_srv, "dirty-flags SRV")
    || !RequireValidIndex(impl_->physical_meta_srv, "physical-metadata SRV")) {
    co_return;
  }

  if (!recorder.IsResourceTracked(*shadow_texture)) {
    recorder.BeginTrackingResourceState(
      *shadow_texture, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*dirty_flags_buffer)) {
    recorder.BeginTrackingResourceState(
      *dirty_flags_buffer, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*physical_meta_buffer)) {
    recorder.BeginTrackingResourceState(
      *physical_meta_buffer, ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*impl_->pass_constants_buffer)) {
    recorder.BeginTrackingResourceState(
      *impl_->pass_constants_buffer, ResourceStates::kGenericRead, true);
  }
  if (!recorder.IsResourceTracked(*impl_->merge_scratch_texture)) {
    recorder.BeginTrackingResourceState(
      *impl_->merge_scratch_texture, ResourceStates::kCommon, true);
  }

  impl_->resources_prepared = true;
  DLOG_F(2,
    "prepared VSM static/dynamic merge generation={} page_size={} "
    "logical_pages={} physical_pages={} dynamic_slice={} static_slice={} "
    "merge_candidates={}",
    input.frame.snapshot.frame_generation, input.physical_pool.page_size_texels,
    logical_page_count, input.physical_pool.tile_capacity,
    *impl_->dynamic_slice_index, *impl_->static_slice_index,
    impl_->merge_candidate_logical_pages.size());
  co_return;
}

auto VsmStaticDynamicMergePass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  if (!impl_->resources_prepared || !impl_->input.has_value()
    || !impl_->static_slice_index.has_value()
    || !impl_->dynamic_slice_index.has_value() || !impl_->merge_page_pso
    || impl_->merge_candidate_logical_pages.empty()) {
    DLOG_F(2, "VSM static/dynamic merge pass skipped execute");
    co_return;
  }

  const auto& input = *impl_->input;
  const auto page_size = input.physical_pool.page_size_texels;
  const auto tiles_per_axis = input.physical_pool.tiles_per_axis;
  const auto logical_page_count = tiles_per_axis * tiles_per_axis;
  if (page_size == 0U || logical_page_count == 0U) {
    co_return;
  }

  auto shadow_texture
    = std::const_pointer_cast<Texture>(input.physical_pool.shadow_texture);
  auto dirty_flags_buffer
    = std::const_pointer_cast<Buffer>(input.frame.dirty_flags_buffer);
  auto physical_meta_buffer
    = std::const_pointer_cast<Buffer>(input.frame.physical_page_meta_buffer);

  CHECK_NOTNULL_F(
    shadow_texture.get(), "VSM static/dynamic merge requires a shadow texture");
  CHECK_NOTNULL_F(dirty_flags_buffer.get(),
    "VSM static/dynamic merge requires a dirty-flags buffer");
  CHECK_NOTNULL_F(physical_meta_buffer.get(),
    "VSM static/dynamic merge requires a physical-metadata buffer");
  CHECK_NOTNULL_F(impl_->merge_scratch_texture.get(),
    "VSM static/dynamic merge requires a scratch texture");

  const auto scratch_slice = graphics::TextureSlice {
    .x = 0U,
    .y = 0U,
    .z = 0U,
    .width = page_size,
    .height = page_size,
    .depth = 1U,
    .mip_level = 0U,
    .array_slice = 0U,
  };
  const auto scratch_subresources = graphics::TextureSubResourceSet {
    .base_mip_level = 0U,
    .num_mip_levels = 1U,
    .base_array_slice = 0U,
    .num_array_slices = 1U,
  };
  const auto dispatch_groups
    = MakeDispatchGroups(page_size, kMergeThreadGroupSize);

  for (const auto logical_page_index : impl_->merge_candidate_logical_pages) {
    const auto tile_x = logical_page_index % tiles_per_axis;
    const auto tile_y = logical_page_index / tiles_per_axis;
    const auto dynamic_page_slice = graphics::TextureSlice {
      .x = tile_x * page_size,
      .y = tile_y * page_size,
      .z = 0U,
      .width = page_size,
      .height = page_size,
      .depth = 1U,
      .mip_level = 0U,
      .array_slice = *impl_->dynamic_slice_index,
    };
    const auto dynamic_page_subresources = graphics::TextureSubResourceSet {
      .base_mip_level = 0U,
      .num_mip_levels = 1U,
      .base_array_slice = *impl_->dynamic_slice_index,
      .num_array_slices = 1U,
    };

    recorder.RequireResourceState(*shadow_texture, ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *impl_->merge_scratch_texture, ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyTexture(*shadow_texture, dynamic_page_slice,
      dynamic_page_subresources, *impl_->merge_scratch_texture, scratch_slice,
      scratch_subresources);

    impl_->next_pass_constants_slot = 0U;
    const auto pass_constants_index
      = impl_->WritePassConstants(VsmStaticDynamicMergePassConstants {
        .static_shadow_srv_index = impl_->static_shadow_srv,
        .dynamic_shadow_uav_index = impl_->merge_scratch_uav,
        .dirty_flags_srv_index = impl_->dirty_flags_srv,
        .physical_meta_srv_index = impl_->physical_meta_srv,
        .page_size_texels = page_size,
        .tiles_per_axis = tiles_per_axis,
        .logical_page_index = logical_page_index,
        .logical_page_count = logical_page_count,
        .dynamic_slice_index = *impl_->dynamic_slice_index,
        .static_slice_index = *impl_->static_slice_index,
      });
    SetPassConstantsIndex(pass_constants_index);
    BindComputeStage(
      recorder, *impl_->merge_page_pso, pass_constants_index, Context());
    recorder.RequireResourceState(
      *shadow_texture, ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *dirty_flags_buffer, ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *physical_meta_buffer, ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *impl_->merge_scratch_texture, ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();
    recorder.Dispatch(dispatch_groups, dispatch_groups, 1U);

    recorder.RequireResourceState(
      *impl_->merge_scratch_texture, ResourceStates::kCopySource);
    recorder.RequireResourceState(*shadow_texture, ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyTexture(*impl_->merge_scratch_texture, scratch_slice,
      scratch_subresources, *shadow_texture, dynamic_page_slice,
      dynamic_page_subresources);
  }

  recorder.RequireResourceState(
    *shadow_texture, ResourceStates::kShaderResource);
  recorder.RequireResourceState(*dirty_flags_buffer, ResourceStates::kCommon);
  recorder.RequireResourceState(*physical_meta_buffer, ResourceStates::kCommon);
  recorder.RequireResourceState(
    *impl_->merge_scratch_texture, ResourceStates::kCommon);
  recorder.FlushBarriers();

  DLOG_F(2,
    "executed VSM static/dynamic merge generation={} page_size={} "
    "logical_pages={} physical_pages={} static_slice={} dynamic_slice={} "
    "merge_candidates={}",
    input.frame.snapshot.frame_generation, page_size, logical_page_count,
    input.physical_pool.tile_capacity, *impl_->static_slice_index,
    *impl_->dynamic_slice_index, impl_->merge_candidate_logical_pages.size());
  co_return;
}

auto VsmStaticDynamicMergePass::ValidateConfig() -> void
{
  if (impl_->gfx == nullptr) {
    throw std::runtime_error("VsmStaticDynamicMergePass requires Graphics");
  }
}

auto VsmStaticDynamicMergePass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();
  graphics::ShaderRequest shader_request {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Renderer/Vsm/VsmStaticDynamicMerge.hlsl",
    .entry_point = "CS",
  };

  impl_->merge_page_pso
    = ComputePipelineDesc::Builder()
        .SetComputeShader(std::move(shader_request))
        .SetRootBindings(std::span<const graphics::RootBindingItem>(
          generated_bindings.data(), generated_bindings.size()))
        .SetDebugName("VsmStaticDynamicMerge_PSO")
        .Build();
  impl_->pipelines_ready = true;
  return *impl_->merge_page_pso;
}

auto VsmStaticDynamicMergePass::NeedRebuildPipelineState() const -> bool
{
  return !impl_->pipelines_ready;
}

} // namespace oxygen::engine
