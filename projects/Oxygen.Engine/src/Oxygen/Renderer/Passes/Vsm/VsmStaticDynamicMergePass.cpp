//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/Vsm/VsmStaticDynamicMergePass.h>

#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>

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
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>

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
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;

namespace oxygen::engine {

namespace {

  constexpr std::uint32_t kThreadGroupSize = 8U;

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmStaticDynamicMergePassConstants {
    ShaderVisibleIndex static_shadow_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex dynamic_shadow_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex dirty_flags_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex physical_meta_srv_index { kInvalidShaderVisibleIndex };
    std::uint32_t page_size_texels { 0U };
    std::uint32_t tiles_per_axis { 0U };
    std::uint32_t physical_page_count { 0U };
    std::uint32_t _pad0 { 0U };
  };
  static_assert(sizeof(VsmStaticDynamicMergePassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

  auto MakeDispatchGroups(const std::uint32_t texel_count) noexcept
    -> std::uint32_t
  {
    return texel_count == 0U
      ? 0U
      : (texel_count + kThreadGroupSize - 1U) / kThreadGroupSize;
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

  auto MakeShadowSliceSrvDesc(const std::uint32_t slice)
    -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
    .view_type = ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = oxygen::Format::kR32Float,
    .dimension = oxygen::TextureType::kTexture2DArray,
    .sub_resources
    = {
        .base_mip_level = 0U,
        .num_mip_levels = 1U,
        .base_array_slice = slice,
        .num_array_slices = 1U,
      },
    .is_read_only_dsv = false,
  };
  }

  auto MakeScratchUavDesc() -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
    .view_type = ResourceViewType::kTexture_UAV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = oxygen::Format::kR32Float,
    .dimension = oxygen::TextureType::kTexture2D,
    .sub_resources
    = {
        .base_mip_level = 0U,
        .num_mip_levels = 1U,
        .base_array_slice = 0U,
        .num_array_slices = 1U,
      },
    .is_read_only_dsv = false,
  };
  }

  auto ResolveMergeScratchFormat(const oxygen::Format depth_format) noexcept
    -> oxygen::Format
  {
    switch (depth_format) {
    case oxygen::Format::kDepth32:
      return oxygen::Format::kR32Float;
    default:
      return oxygen::Format::kUnknown;
    }
  }

  auto MakeStructuredSrvDesc(const std::uint64_t size_bytes,
    const std::uint32_t stride) -> graphics::BufferViewDescription
  {
    return graphics::BufferViewDescription {
      .view_type = ResourceViewType::kStructuredBuffer_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { 0U, size_bytes },
      .stride = stride,
    };
  }

  auto RequireValidIndex(
    const ShaderVisibleIndex index, const char* label) noexcept -> bool
  {
    if (index.IsValid()) {
      return true;
    }

    LOG_F(ERROR,
      "VSM static/dynamic merge pass skipped because {} is unavailable", label);
    return false;
  }

} // namespace

struct VsmStaticDynamicMergePass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;

  std::optional<VsmStaticDynamicMergePassInput> input {};
  bool resources_prepared { false };

  std::shared_ptr<Buffer> constants_buffer {};
  void* constants_ptr { nullptr };

  DescriptorHandle constants_cbv_handle {};
  DescriptorHandle static_shadow_srv_handle {};
  DescriptorHandle dynamic_shadow_uav_handle {};
  DescriptorHandle dirty_flags_srv_handle {};
  DescriptorHandle physical_meta_srv_handle {};

  ShaderVisibleIndex constants_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex static_shadow_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex dynamic_shadow_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex dirty_flags_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex physical_meta_srv { kInvalidShaderVisibleIndex };

  const graphics::Texture* shadow_texture_owner { nullptr };
  std::shared_ptr<graphics::Texture> merge_scratch_texture {};
  const graphics::Texture* merge_scratch_owner { nullptr };
  const Buffer* dirty_flags_owner { nullptr };
  const Buffer* physical_meta_owner { nullptr };
  std::uint32_t merge_scratch_extent { 0U };
  oxygen::Format merge_scratch_format { oxygen::Format::kUnknown };

  std::optional<std::uint32_t> dynamic_slice_index {};
  std::optional<std::uint32_t> static_slice_index {};

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl()
  {
    if (constants_buffer != nullptr && constants_ptr != nullptr) {
      constants_buffer->UnMap();
      constants_ptr = nullptr;
    }
    if (gfx != nullptr && constants_buffer != nullptr) {
      auto& registry = gfx->GetResourceRegistry();
      if (registry.Contains(*constants_buffer)) {
        registry.UnRegisterResource(*constants_buffer);
      }
    }
    if (gfx != nullptr && merge_scratch_texture != nullptr) {
      auto& registry = gfx->GetResourceRegistry();
      if (registry.Contains(*merge_scratch_texture)) {
        registry.UnRegisterResource(*merge_scratch_texture);
      }
    }
  }

  auto EnsureConstantsBuffer() -> void
  {
    if (constants_buffer != nullptr && constants_ptr != nullptr) {
      return;
    }

    constants_buffer = gfx->CreateBuffer(BufferDesc {
      .size_bytes = 256U,
      .usage = BufferUsage::kConstant,
      .memory = BufferMemory::kUpload,
      .debug_name = config->debug_name + ".Constants",
    });
    CHECK_NOTNULL_F(
      constants_buffer.get(), "Failed to create VSM merge constants buffer");
    gfx->GetResourceRegistry().Register(constants_buffer);
    constants_ptr = constants_buffer->Map(0U, 256U);
    CHECK_NOTNULL_F(constants_ptr, "Failed to map VSM merge constants buffer");
  }

  auto EnsureConstantBufferView() -> ShaderVisibleIndex
  {
    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    const auto view_desc = graphics::BufferViewDescription {
      .view_type = ResourceViewType::kConstantBuffer,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { 0U, 256U },
      .stride = 0U,
    };

    if (const auto existing
      = registry.FindShaderVisibleIndex(*constants_buffer, view_desc);
      existing.has_value()) {
      constants_index = *existing;
      return constants_index;
    }

    constants_cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(constants_cbv_handle.IsValid(),
      "Failed to allocate VSM merge constants CBV");
    constants_index = allocator.GetShaderVisibleIndex(constants_cbv_handle);
    const auto view = registry.RegisterView(
      *constants_buffer, std::move(constants_cbv_handle), view_desc);
    CHECK_F(view->IsValid(), "Failed to register VSM merge constants CBV");
    return constants_index;
  }

  auto EnsureBufferView(Buffer& buffer,
    const graphics::BufferViewDescription& desc, DescriptorHandle& handle,
    ShaderVisibleIndex& index, const Buffer*& owner) -> ShaderVisibleIndex
  {
    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    if (!registry.Contains(buffer)) {
      registry.Register(std::shared_ptr<Buffer>(&buffer, [](Buffer*) { }));
    }

    if (const auto existing = registry.FindShaderVisibleIndex(buffer, desc);
      existing.has_value()) {
      index = *existing;
      owner = &buffer;
      return index;
    }

    handle = allocator.Allocate(
      desc.view_type, graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(), "Failed to allocate VSM merge buffer view");
    index = allocator.GetShaderVisibleIndex(handle);
    const auto view = registry.RegisterView(buffer, std::move(handle), desc);
    CHECK_F(view->IsValid(), "Failed to register VSM merge buffer view");
    owner = &buffer;
    return index;
  }

  auto EnsureTextureView(graphics::Texture& texture,
    const graphics::TextureViewDescription& desc, DescriptorHandle& handle,
    ShaderVisibleIndex& index, const graphics::Texture*& owner)
    -> ShaderVisibleIndex
  {
    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    if (!registry.Contains(texture)) {
      registry.Register(std::shared_ptr<graphics::Texture>(
        &texture, [](graphics::Texture*) { }));
    }

    if (const auto existing = registry.FindShaderVisibleIndex(texture, desc);
      existing.has_value()) {
      index = *existing;
      owner = &texture;
      return index;
    }

    handle = allocator.Allocate(
      desc.view_type, graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(), "Failed to allocate VSM merge texture view");
    index = allocator.GetShaderVisibleIndex(handle);
    const auto view = registry.RegisterView(texture, std::move(handle), desc);
    CHECK_F(view->IsValid(), "Failed to register VSM merge texture view");
    owner = &texture;
    return index;
  }

  auto EnsureMergeScratchTexture(
    const renderer::vsm::VsmPhysicalPoolSnapshot& pool) -> bool
  {
    const auto scratch_format = ResolveMergeScratchFormat(pool.depth_format);
    if (scratch_format == oxygen::Format::kUnknown) {
      LOG_F(ERROR,
        "VSM static/dynamic merge requires a supported depth format "
        "(depth_format={})",
        static_cast<int>(pool.depth_format));
      merge_scratch_texture.reset();
      merge_scratch_extent = 0U;
      merge_scratch_format = oxygen::Format::kUnknown;
      return false;
    }

    const auto scratch_extent = pool.page_size_texels * pool.tiles_per_axis;
    if (merge_scratch_texture != nullptr
      && merge_scratch_extent == scratch_extent
      && merge_scratch_format == scratch_format) {
      return true;
    }

    if (merge_scratch_texture != nullptr) {
      auto& registry = gfx->GetResourceRegistry();
      if (registry.Contains(*merge_scratch_texture)) {
        registry.UnRegisterResource(*merge_scratch_texture);
      }
      merge_scratch_texture.reset();
      merge_scratch_owner = nullptr;
      merge_scratch_extent = 0U;
      merge_scratch_format = oxygen::Format::kUnknown;
      dynamic_shadow_uav = kInvalidShaderVisibleIndex;
    }

    auto desc = graphics::TextureDesc {};
    desc.width = scratch_extent;
    desc.height = scratch_extent;
    desc.array_size = 1U;
    desc.mip_levels = 1U;
    desc.format = scratch_format;
    desc.texture_type = oxygen::TextureType::kTexture2D;
    desc.is_uav = true;
    desc.is_shader_resource = false;
    desc.initial_state = ResourceStates::kCommon;
    desc.debug_name = config != nullptr && !config->debug_name.empty()
      ? config->debug_name + ".MergeScratch"
      : "VsmStaticDynamicMergePass.MergeScratch";

    merge_scratch_texture = gfx->CreateTexture(desc);
    if (merge_scratch_texture == nullptr) {
      LOG_F(ERROR,
        "VSM static/dynamic merge failed to create scratch texture "
        "extent={} format={}",
        scratch_extent, static_cast<int>(scratch_format));
      return false;
    }

    auto& registry = gfx->GetResourceRegistry();
    if (!registry.Contains(*merge_scratch_texture)) {
      registry.Register(merge_scratch_texture);
    }

    merge_scratch_extent = scratch_extent;
    merge_scratch_format = scratch_format;
    return true;
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
}

auto VsmStaticDynamicMergePass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  impl_->resources_prepared = false;
  impl_->dynamic_slice_index.reset();
  impl_->static_slice_index.reset();

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

  auto shadow_texture = std::const_pointer_cast<graphics::Texture>(
    input.physical_pool.shadow_texture);
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
  if (!impl_->EnsureMergeScratchTexture(input.physical_pool)) {
    co_return;
  }
  CHECK_NOTNULL_F(impl_->merge_scratch_texture.get(),
    "VSM static/dynamic merge requires a scratch texture");

  impl_->EnsureConstantsBuffer();
  static_cast<void>(impl_->EnsureConstantBufferView());
  static_cast<void>(impl_->EnsureTextureView(*shadow_texture,
    MakeShadowSliceSrvDesc(*impl_->static_slice_index),
    impl_->static_shadow_srv_handle, impl_->static_shadow_srv,
    impl_->shadow_texture_owner));
  static_cast<void>(impl_->EnsureTextureView(*impl_->merge_scratch_texture,
    MakeScratchUavDesc(), impl_->dynamic_shadow_uav_handle,
    impl_->dynamic_shadow_uav, impl_->merge_scratch_owner));
  static_cast<void>(impl_->EnsureBufferView(*dirty_flags_buffer,
    MakeStructuredSrvDesc(
      dirty_flags_buffer->GetDescriptor().size_bytes, sizeof(std::uint32_t)),
    impl_->dirty_flags_srv_handle, impl_->dirty_flags_srv,
    impl_->dirty_flags_owner));
  static_cast<void>(impl_->EnsureBufferView(*physical_meta_buffer,
    MakeStructuredSrvDesc(physical_meta_buffer->GetDescriptor().size_bytes,
      sizeof(renderer::vsm::VsmPhysicalPageMeta)),
    impl_->physical_meta_srv_handle, impl_->physical_meta_srv,
    impl_->physical_meta_owner));

  if (!RequireValidIndex(impl_->constants_index, "constants CBV")
    || !RequireValidIndex(impl_->static_shadow_srv, "static-slice SRV")
    || !RequireValidIndex(impl_->dynamic_shadow_uav, "dynamic-slice UAV")
    || !RequireValidIndex(impl_->dirty_flags_srv, "dirty-flags SRV")
    || !RequireValidIndex(impl_->physical_meta_srv, "physical-metadata SRV")) {
    co_return;
  }

  auto constants = VsmStaticDynamicMergePassConstants {
    .static_shadow_srv_index = impl_->static_shadow_srv,
    .dynamic_shadow_uav_index = impl_->dynamic_shadow_uav,
    .dirty_flags_srv_index = impl_->dirty_flags_srv,
    .physical_meta_srv_index = impl_->physical_meta_srv,
    .page_size_texels = input.physical_pool.page_size_texels,
    .tiles_per_axis = input.physical_pool.tiles_per_axis,
    .physical_page_count = input.physical_pool.tile_capacity,
  };
  std::memcpy(impl_->constants_ptr, &constants, sizeof(constants));

  SetPassConstantsIndex(impl_->constants_index);

  recorder.BeginTrackingResourceState(
    *shadow_texture, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *dirty_flags_buffer, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *physical_meta_buffer, ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *impl_->constants_buffer, ResourceStates::kGenericRead, true);
  recorder.BeginTrackingResourceState(
    *impl_->merge_scratch_texture, ResourceStates::kCommon, true);

  recorder.RequireResourceState(
    *shadow_texture, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *dirty_flags_buffer, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *physical_meta_buffer, ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  impl_->resources_prepared = true;
  co_return;
}

auto VsmStaticDynamicMergePass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  if (!impl_->resources_prepared || !impl_->input.has_value()
    || !impl_->static_slice_index.has_value()
    || !impl_->dynamic_slice_index.has_value()) {
    DLOG_F(2, "VSM static/dynamic merge pass skipped execute");
    co_return;
  }

  const auto page_size = impl_->input->physical_pool.page_size_texels;
  const auto physical_page_count = impl_->input->physical_pool.tile_capacity;
  if (page_size == 0U || physical_page_count == 0U) {
    co_return;
  }

  auto shadow_texture = std::const_pointer_cast<graphics::Texture>(
    impl_->input->physical_pool.shadow_texture);
  CHECK_NOTNULL_F(
    shadow_texture.get(), "VSM static/dynamic merge requires a shadow texture");
  CHECK_NOTNULL_F(impl_->merge_scratch_texture.get(),
    "VSM static/dynamic merge requires a scratch texture");

  const auto slice_extent
    = page_size * impl_->input->physical_pool.tiles_per_axis;
  const auto scratch_slice = graphics::TextureSlice {
    .x = 0U,
    .y = 0U,
    .z = 0U,
    .width = slice_extent,
    .height = slice_extent,
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
  const auto dynamic_slice = graphics::TextureSlice {
    .x = 0U,
    .y = 0U,
    .z = 0U,
    .width = slice_extent,
    .height = slice_extent,
    .depth = 1U,
    .mip_level = 0U,
    .array_slice = *impl_->dynamic_slice_index,
  };
  const auto dynamic_subresources = graphics::TextureSubResourceSet {
    .base_mip_level = 0U,
    .num_mip_levels = 1U,
    .base_array_slice = *impl_->dynamic_slice_index,
    .num_array_slices = 1U,
  };

  recorder.RequireResourceState(*shadow_texture, ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *impl_->merge_scratch_texture, ResourceStates::kCopyDest);
  recorder.FlushBarriers();

  recorder.CopyTexture(*shadow_texture, dynamic_slice, dynamic_subresources,
    *impl_->merge_scratch_texture, scratch_slice, scratch_subresources);

  recorder.RequireResourceState(
    *shadow_texture, ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *impl_->merge_scratch_texture, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  recorder.Dispatch(MakeDispatchGroups(page_size),
    MakeDispatchGroups(page_size), physical_page_count);

  recorder.RequireResourceState(
    *impl_->merge_scratch_texture, ResourceStates::kCopySource);
  recorder.RequireResourceState(*shadow_texture, ResourceStates::kCopyDest);
  recorder.FlushBarriers();

  recorder.CopyTexture(*impl_->merge_scratch_texture, scratch_slice,
    scratch_subresources, *shadow_texture, dynamic_slice, dynamic_subresources);

  recorder.RequireResourceState(
    *shadow_texture, ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  DLOG_F(2,
    "executed VSM static/dynamic merge pass generation={} physical_pages={} "
    "page_size={} static_slice={} dynamic_slice={}",
    impl_->input->frame.snapshot.frame_generation, physical_page_count,
    page_size, *impl_->static_slice_index, *impl_->dynamic_slice_index);
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

  return ComputePipelineDesc::Builder()
    .SetComputeShader(std::move(shader_request))
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VsmStaticDynamicMerge_PSO")
    .Build();
}

auto VsmStaticDynamicMergePass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
