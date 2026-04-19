//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
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
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>

namespace oxygen::vortex {

namespace {

namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

constexpr std::uint32_t kThreadGroupSize = 8U;
constexpr std::uint32_t kPassConstantsStride
  = packing::kConstantBufferAlignment;

struct alignas(packing::kShaderDataFieldAlignment) ScreenHzbBuildConstants {
  ShaderVisibleIndex source_closest_texture_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex source_furthest_texture_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex destination_closest_texture_uav_index {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex destination_furthest_texture_uav_index {
    kInvalidShaderVisibleIndex
  };
  std::uint32_t source_width { 0U };
  std::uint32_t source_height { 0U };
  std::uint32_t source_origin_x { 0U };
  std::uint32_t source_origin_y { 0U };
  std::uint32_t destination_width { 0U };
  std::uint32_t destination_height { 0U };
  std::uint32_t source_texel_step { 1U };
  std::uint32_t _pad0 { 0U };
};

static_assert(sizeof(ScreenHzbBuildConstants) == 48U);
static_assert(
  sizeof(ScreenHzbBuildConstants) % packing::kShaderDataFieldAlignment == 0U);

auto RangeTypeToViewType(const bindless_d3d12::RangeType type)
  -> graphics::ResourceViewType
{
  using graphics::ResourceViewType;
  switch (type) {
  case bindless_d3d12::RangeType::SRV:
    return ResourceViewType::kRawBuffer_SRV;
  case bindless_d3d12::RangeType::Sampler:
    return ResourceViewType::kSampler;
  case bindless_d3d12::RangeType::UAV:
    return ResourceViewType::kRawBuffer_UAV;
  default:
    return ResourceViewType::kNone;
  }
}

auto BuildVortexRootBindings() -> std::vector<graphics::RootBindingItem>
{
  std::vector<graphics::RootBindingItem> bindings;
  bindings.reserve(bindless_d3d12::kRootParamTableCount);

  for (std::uint32_t index = 0; index < bindless_d3d12::kRootParamTableCount;
    ++index) {
    const auto& desc = bindless_d3d12::kRootParamTable.at(index);
    graphics::RootBindingDesc binding {};
    binding.binding_slot_desc.register_index = desc.shader_register;
    binding.binding_slot_desc.register_space = desc.register_space;
    binding.visibility = graphics::ShaderStageFlags::kAll;

    switch (desc.kind) {
    case bindless_d3d12::RootParamKind::DescriptorTable: {
      graphics::DescriptorTableBinding table {};
      if (desc.ranges_count > 0U && desc.ranges.data() != nullptr) {
        const auto& range = desc.ranges.front();
        table.view_type = RangeTypeToViewType(
          static_cast<bindless_d3d12::RangeType>(range.range_type));
        table.base_index = range.base_register;
        table.count
          = range.num_descriptors == (std::numeric_limits<std::uint32_t>::max)()
          ? (std::numeric_limits<std::uint32_t>::max)()
          : range.num_descriptors;
      }
      binding.data = table;
      break;
    }
    case bindless_d3d12::RootParamKind::CBV:
      binding.data = graphics::DirectBufferBinding {};
      break;
    case bindless_d3d12::RootParamKind::RootConstants:
      binding.data
        = graphics::PushConstantsBinding { .size = desc.constants_count };
      break;
    }

    bindings.emplace_back(binding);
  }

  return bindings;
}

auto BuildPipelineDesc() -> graphics::ComputePipelineDesc
{
  auto root_bindings = BuildVortexRootBindings();
  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader({
      .stage = ShaderType::kCompute,
      .source_path = "Vortex/Stages/Hzb/ScreenHzbBuild.hlsl",
      .entry_point = "VortexScreenHzbBuildCS",
    })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size()))
    .SetDebugName("Vortex.Stage5.ScreenHzbBuild")
    .Build();
}

[[nodiscard]] auto ComputeHzbRootExtent(const std::uint32_t extent)
  -> std::uint32_t
{
  CHECK_F(extent != 0U, "Screen HZB requires non-zero source dimensions");
  return (std::max)(std::bit_ceil(extent) >> 1U, 1U);
}

[[nodiscard]] auto ComputeMipCount(
  const std::uint32_t width, const std::uint32_t height) -> std::uint32_t
{
  CHECK_F(width != 0U && height != 0U,
    "Screen HZB requires non-zero texture dimensions");
  return (std::max)(std::bit_width((std::max)(width, height)) - 1U, 1U);
}

[[nodiscard]] auto MipExtent(
  const std::uint32_t base_extent, const std::uint32_t mip_level)
  -> std::uint32_t
{
  return (std::max)(1U, base_extent >> mip_level);
}

[[nodiscard]] auto WholeTextureSrvDesc(const graphics::Texture& texture)
  -> graphics::TextureViewDescription
{
  return graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = texture.GetDescriptor().format,
    .dimension = texture.GetDescriptor().texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .is_read_only_dsv = false,
  };
}

[[nodiscard]] auto SingleMipSrvDesc() -> graphics::TextureViewDescription
{
  return graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
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

[[nodiscard]] auto SingleMipUavDesc() -> graphics::TextureViewDescription
{
  return graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_UAV,
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

auto TrackTextureFromKnownOrInitial(graphics::CommandRecorder& recorder,
  const graphics::Texture& texture, graphics::ResourceStates fallback_initial)
  -> void
{
  if (recorder.IsResourceTracked(texture) || recorder.AdoptKnownResourceState(texture)) {
    return;
  }

  auto initial = texture.GetDescriptor().initial_state;
  if (initial == graphics::ResourceStates::kUnknown
    || initial == graphics::ResourceStates::kUndefined) {
    initial = fallback_initial;
  }
  recorder.BeginTrackingResourceState(texture, initial, true);
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

[[nodiscard]] auto ComputeScreenPositionScaleBias(
  const std::uint32_t buffer_width, const std::uint32_t buffer_height,
  const std::uint32_t view_rect_min_x, const std::uint32_t view_rect_min_y,
  const std::uint32_t view_rect_width, const std::uint32_t view_rect_height)
  -> std::array<float, 4>
{
  CHECK_F(buffer_width != 0U && buffer_height != 0U,
    "Screen HZB requires non-zero scene-texture extent");
  const auto inv_buffer_width = 1.0F / static_cast<float>(buffer_width);
  const auto inv_buffer_height = 1.0F / static_cast<float>(buffer_height);
  return {
    static_cast<float>(view_rect_width) * inv_buffer_width / 2.0F,
    static_cast<float>(view_rect_height) * inv_buffer_height / -2.0F,
    (static_cast<float>(view_rect_height) / 2.0F
      + static_cast<float>(view_rect_min_y))
      * inv_buffer_height,
    (static_cast<float>(view_rect_width) / 2.0F
      + static_cast<float>(view_rect_min_x))
      * inv_buffer_width,
  };
}

} // namespace

struct ScreenHzbModule::Impl {
  struct PyramidResources {
    bool enabled { false };
    std::array<std::shared_ptr<graphics::Texture>, 2> history_textures {};
    std::array<ShaderVisibleIndex, 2> history_srv_indices {
      kInvalidShaderVisibleIndex,
      kInvalidShaderVisibleIndex,
    };
    std::array<std::shared_ptr<graphics::Texture>, 2> scratch_textures {};
    std::array<ShaderVisibleIndex, 2> scratch_srv_indices {
      kInvalidShaderVisibleIndex,
      kInvalidShaderVisibleIndex,
    };
    std::array<ShaderVisibleIndex, 2> scratch_uav_indices {
      kInvalidShaderVisibleIndex,
      kInvalidShaderVisibleIndex,
    };
  };

  struct ViewState {
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    std::uint32_t mip_count { 0U };
    std::uint32_t scene_texture_width { 0U };
    std::uint32_t scene_texture_height { 0U };
    std::uint32_t source_view_rect_min_x { 0U };
    std::uint32_t source_view_rect_min_y { 0U };
    std::uint32_t source_view_rect_width { 0U };
    std::uint32_t source_view_rect_height { 0U };
    PyramidResources closest {};
    PyramidResources furthest {};
    std::uint32_t current_history_slot { 0U };
    bool has_current_output { false };
    bool has_previous_output { false };
  };

  explicit Impl(Renderer& renderer_in)
    : renderer(renderer_in)
  {
  }

  ~Impl()
  {
    if (pass_constants_buffer && pass_constants_mapped_ptr != nullptr) {
      pass_constants_buffer->UnMap();
      pass_constants_mapped_ptr = nullptr;
    }

    auto gfx = renderer.GetGraphics();
    if (gfx == nullptr) {
      return;
    }

    auto& registry = gfx->GetResourceRegistry();
    if (pass_constants_buffer && registry.Contains(*pass_constants_buffer)) {
      registry.UnRegisterResource(*pass_constants_buffer);
    }

    for (auto& [view_id, state] : view_states) {
      static_cast<void>(view_id);
      ReleaseViewResources(*gfx, state);
    }
  }

  static auto ReleaseViewResources(Graphics& gfx, ViewState& state) -> void
  {
    auto& registry = gfx.GetResourceRegistry();
    const auto release_pyramid = [&](PyramidResources& pyramid) {
      for (auto& texture : pyramid.history_textures) {
        if (texture && registry.Contains(*texture)) {
          registry.UnRegisterResource(*texture);
        }
        gfx.RegisterDeferredRelease(std::move(texture));
      }
      for (auto& texture : pyramid.scratch_textures) {
        if (texture && registry.Contains(*texture)) {
          registry.UnRegisterResource(*texture);
        }
        gfx.RegisterDeferredRelease(std::move(texture));
      }
      pyramid.enabled = false;
      pyramid.history_srv_indices.fill(kInvalidShaderVisibleIndex);
      pyramid.scratch_srv_indices.fill(kInvalidShaderVisibleIndex);
      pyramid.scratch_uav_indices.fill(kInvalidShaderVisibleIndex);
    };

    release_pyramid(state.closest);
    release_pyramid(state.furthest);
    state.current_history_slot = 0U;
    state.has_current_output = false;
    state.has_previous_output = false;
    state.width = 0U;
    state.height = 0U;
    state.mip_count = 0U;
    state.scene_texture_width = 0U;
    state.scene_texture_height = 0U;
    state.source_view_rect_min_x = 0U;
    state.source_view_rect_min_y = 0U;
    state.source_view_rect_width = 0U;
    state.source_view_rect_height = 0U;
  }

  auto EnsurePassConstantsBuffer(const std::uint32_t slot_count) -> void
  {
    if (pass_constants_buffer && pass_constants_capacity >= slot_count) {
      return;
    }

    auto gfx = renderer.GetGraphics();
    CHECK_NOTNULL_F(gfx.get(), "Screen HZB requires Graphics");
    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    if (pass_constants_buffer && pass_constants_mapped_ptr != nullptr) {
      pass_constants_buffer->UnMap();
      pass_constants_mapped_ptr = nullptr;
    }
    if (pass_constants_buffer && registry.Contains(*pass_constants_buffer)) {
      registry.UnRegisterResource(*pass_constants_buffer);
    }

    const auto buffer_size
      = static_cast<std::uint64_t>(slot_count) * kPassConstantsStride;
    const graphics::BufferDesc desc {
      .size_bytes = buffer_size,
      .usage = graphics::BufferUsage::kConstant,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = "Vortex.Stage5.ScreenHzbBuild.PassConstants",
    };
    pass_constants_buffer = gfx->CreateBuffer(desc);
    CHECK_NOTNULL_F(
      pass_constants_buffer.get(), "Failed to create Screen HZB constants");
    registry.Register(pass_constants_buffer);

    pass_constants_mapped_ptr = pass_constants_buffer->Map(0U, desc.size_bytes);
    CHECK_NOTNULL_F(
      pass_constants_mapped_ptr, "Failed to map Screen HZB constants");

    pass_constants_indices.clear();
    pass_constants_indices.reserve(slot_count);
    for (std::uint32_t slot = 0U; slot < slot_count; ++slot) {
      auto handle
        = allocator.AllocateRaw(graphics::ResourceViewType::kConstantBuffer,
          graphics::DescriptorVisibility::kShaderVisible);
      CHECK_F(handle.IsValid(), "Failed to allocate Screen HZB constants CBV");

      graphics::BufferViewDescription view_desc;
      view_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
      view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      view_desc.range = {
        static_cast<std::uint64_t>(slot) * kPassConstantsStride,
        kPassConstantsStride,
      };

      pass_constants_indices.push_back(allocator.GetShaderVisibleIndex(handle));
      registry.RegisterView(
        *pass_constants_buffer, std::move(handle), view_desc);
    }

    pass_constants_capacity = slot_count;
  }

  auto WritePassConstants(const std::uint32_t slot,
    const ScreenHzbBuildConstants& constants) const -> void
  {
    CHECK_NOTNULL_F(
      pass_constants_mapped_ptr, "Screen HZB constants buffer is not mapped");
    CHECK_F(slot < pass_constants_indices.size(),
      "Screen HZB constants slot {} exceeds capacity {}", slot,
      pass_constants_indices.size());
    auto* destination = static_cast<std::byte*>(pass_constants_mapped_ptr)
      + static_cast<std::size_t>(slot) * kPassConstantsStride;
    std::memcpy(destination, &constants, sizeof(constants));
  }

  auto EnsureTextureSrv(const graphics::Texture& texture,
    const graphics::TextureViewDescription& view_desc,
    ShaderVisibleIndex& cached_index, const char* debug_label)
    -> ShaderVisibleIndex
  {
    auto gfx = renderer.GetGraphics();
    CHECK_NOTNULL_F(gfx.get(), "Screen HZB requires Graphics");
    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    if (const auto existing
      = registry.FindShaderVisibleIndex(texture, view_desc);
      existing.has_value()) {
      cached_index = *existing;
      return cached_index;
    }

    auto handle
      = allocator.AllocateRaw(graphics::ResourceViewType::kTexture_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!handle.IsValid()) {
      LOG_F(ERROR, "Screen HZB: failed to allocate {} SRV", debug_label);
      cached_index = kInvalidShaderVisibleIndex;
      return cached_index;
    }

    cached_index = allocator.GetShaderVisibleIndex(handle);
    const auto view = registry.RegisterView(
      const_cast<graphics::Texture&>(texture), std::move(handle), view_desc);
    if (!view->IsValid()) {
      LOG_F(ERROR, "Screen HZB: failed to register {} SRV", debug_label);
      cached_index = kInvalidShaderVisibleIndex;
    }
    return cached_index;
  }

  auto EnsureTextureUav(const graphics::Texture& texture,
    const graphics::TextureViewDescription& view_desc,
    ShaderVisibleIndex& cached_index, const char* debug_label)
    -> ShaderVisibleIndex
  {
    auto gfx = renderer.GetGraphics();
    CHECK_NOTNULL_F(gfx.get(), "Screen HZB requires Graphics");
    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    if (const auto existing
      = registry.FindShaderVisibleIndex(texture, view_desc);
      existing.has_value()) {
      cached_index = *existing;
      return cached_index;
    }

    auto handle
      = allocator.AllocateRaw(graphics::ResourceViewType::kTexture_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!handle.IsValid()) {
      LOG_F(ERROR, "Screen HZB: failed to allocate {} UAV", debug_label);
      cached_index = kInvalidShaderVisibleIndex;
      return cached_index;
    }

    cached_index = allocator.GetShaderVisibleIndex(handle);
    const auto view = registry.RegisterView(
      const_cast<graphics::Texture&>(texture), std::move(handle), view_desc);
    if (!view->IsValid()) {
      LOG_F(ERROR, "Screen HZB: failed to register {} UAV", debug_label);
      cached_index = kInvalidShaderVisibleIndex;
    }
    return cached_index;
  }

  auto EnsureViewResources(const ViewId view_id, const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t mip_count,
    const bool build_closest, const bool build_furthest) -> ViewState&
  {
    auto gfx = renderer.GetGraphics();
    CHECK_NOTNULL_F(gfx.get(), "Screen HZB requires Graphics");
    auto& state = view_states[view_id];

    const auto pyramid_missing_resource
      = [](const PyramidResources& pyramid) -> bool {
      if (!pyramid.enabled) {
        return false;
      }
      return pyramid.history_textures[0] == nullptr
        || pyramid.history_textures[1] == nullptr
        || pyramid.scratch_textures[0] == nullptr
        || pyramid.scratch_textures[1] == nullptr;
    };
    const bool needs_recreate = pyramid_missing_resource(state.closest)
      || pyramid_missing_resource(state.furthest)
      || state.closest.enabled != build_closest
      || state.furthest.enabled != build_furthest || state.width != width
      || state.height != height || state.mip_count != mip_count;
    if (!needs_recreate) {
      return state;
    }

    ReleaseViewResources(*gfx, state);
    const auto base_name
      = "Vortex.Stage5.ScreenHzbBuild.View" + std::to_string(view_id.get());

    const auto create_pyramid = [&](PyramidResources& pyramid,
                                  const char* semantic_label) {
      pyramid.enabled = true;
      for (std::uint32_t slot = 0U; slot < 2U; ++slot) {
        graphics::TextureDesc history_desc {};
        history_desc.width = width;
        history_desc.height = height;
        history_desc.mip_levels = mip_count;
        history_desc.format = oxygen::Format::kR32Float;
        history_desc.texture_type = oxygen::TextureType::kTexture2D;
        history_desc.is_shader_resource = true;
        history_desc.initial_state = graphics::ResourceStates::kCommon;
        history_desc.debug_name
          = base_name + "." + semantic_label + ".History" + std::to_string(slot);
        pyramid.history_textures[slot] = gfx->CreateTexture(history_desc);
        CHECK_NOTNULL_F(pyramid.history_textures[slot].get(),
          "Failed to create Screen HZB {} history texture", semantic_label);
        RegisterResourceIfNeeded(*gfx, pyramid.history_textures[slot]);

        graphics::TextureDesc scratch_desc {};
        scratch_desc.width = width;
        scratch_desc.height = height;
        scratch_desc.mip_levels = 1U;
        scratch_desc.format = oxygen::Format::kR32Float;
        scratch_desc.texture_type = oxygen::TextureType::kTexture2D;
        scratch_desc.is_shader_resource = true;
        scratch_desc.is_uav = true;
        scratch_desc.initial_state = graphics::ResourceStates::kCommon;
        scratch_desc.debug_name
          = base_name + "." + semantic_label + ".Scratch" + std::to_string(slot);
        pyramid.scratch_textures[slot] = gfx->CreateTexture(scratch_desc);
        CHECK_NOTNULL_F(pyramid.scratch_textures[slot].get(),
          "Failed to create Screen HZB {} scratch texture", semantic_label);
        RegisterResourceIfNeeded(*gfx, pyramid.scratch_textures[slot]);

        auto history_label = std::string(semantic_label) + "-history-"
          + std::to_string(slot);
        CHECK_F(
          EnsureTextureSrv(*pyramid.history_textures[slot],
            WholeTextureSrvDesc(*pyramid.history_textures[slot]),
            pyramid.history_srv_indices[slot], history_label.c_str())
            .IsValid(),
          "Screen HZB history SRV must be valid");

        auto scratch_label = std::string(semantic_label) + "-scratch-"
          + std::to_string(slot);
        CHECK_F(
          EnsureTextureSrv(*pyramid.scratch_textures[slot], SingleMipSrvDesc(),
            pyramid.scratch_srv_indices[slot], scratch_label.c_str())
            .IsValid(),
          "Screen HZB scratch SRV must be valid");
        CHECK_F(
          EnsureTextureUav(*pyramid.scratch_textures[slot], SingleMipUavDesc(),
            pyramid.scratch_uav_indices[slot], scratch_label.c_str())
            .IsValid(),
          "Screen HZB scratch UAV must be valid");
      }
    };

    if (build_closest) {
      create_pyramid(state.closest, "Closest");
    }
    if (build_furthest) {
      create_pyramid(state.furthest, "Furthest");
    }
    state.width = width;
    state.height = height;
    state.mip_count = mip_count;
    state.current_history_slot = 0U;
    state.has_current_output = false;
    state.has_previous_output = false;
    return state;
  }

  [[nodiscard]] static auto BuildOutput(
    const ViewState& state, const std::uint32_t slot, const bool available)
    -> ScreenHzbModule::Output
  {
    if (!available) {
      return {};
    }

    const auto closest_valid = state.closest.enabled
      && state.closest.history_textures[slot] != nullptr
      && state.closest.history_srv_indices[slot].IsValid();
    const auto furthest_valid = state.furthest.enabled
      && state.furthest.history_textures[slot] != nullptr
      && state.furthest.history_srv_indices[slot].IsValid();
    const auto requested_valid = (state.closest.enabled ? closest_valid : true)
      && (state.furthest.enabled ? furthest_valid : true)
      && (closest_valid || furthest_valid);
    if (!requested_valid) {
      return {};
    }

    const auto hzb_uv_factor_x
      = static_cast<float>(state.source_view_rect_width)
      / (2.0F * static_cast<float>(state.width));
    const auto hzb_uv_factor_y
      = static_cast<float>(state.source_view_rect_height)
      / (2.0F * static_cast<float>(state.height));
    const auto hzb_uv_inv_factor_x
      = hzb_uv_factor_x > 0.0F ? 1.0F / hzb_uv_factor_x : 0.0F;
    const auto hzb_uv_inv_factor_y
      = hzb_uv_factor_y > 0.0F ? 1.0F / hzb_uv_factor_y : 0.0F;
    const auto screen_position_scale_bias = ComputeScreenPositionScaleBias(
      state.scene_texture_width, state.scene_texture_height,
      state.source_view_rect_min_x, state.source_view_rect_min_y,
      state.source_view_rect_width, state.source_view_rect_height);

    auto bindings = ScreenHzbFrameBindings {
      .closest_srv = closest_valid ? state.closest.history_srv_indices[slot]
                                   : kInvalidShaderVisibleIndex,
      .furthest_srv = furthest_valid ? state.furthest.history_srv_indices[slot]
                                     : kInvalidShaderVisibleIndex,
      .width = state.width,
      .height = state.height,
      .mip_count = state.mip_count,
      .flags = kScreenHzbFrameBindingsFlagAvailable
        | (furthest_valid ? kScreenHzbFrameBindingsFlagFurthestValid : 0U)
        | (closest_valid ? kScreenHzbFrameBindingsFlagClosestValid : 0U),
      .hzb_size_x = static_cast<float>(state.width),
      .hzb_size_y = static_cast<float>(state.height),
      .hzb_view_size_x = static_cast<float>(state.source_view_rect_width),
      .hzb_view_size_y = static_cast<float>(state.source_view_rect_height),
      .hzb_view_rect_min_x = 0,
      .hzb_view_rect_min_y = 0,
      .hzb_view_rect_width
      = static_cast<std::int32_t>(state.source_view_rect_width),
      .hzb_view_rect_height
      = static_cast<std::int32_t>(state.source_view_rect_height),
      .viewport_uv_to_hzb_buffer_uv_x = hzb_uv_factor_x,
      .viewport_uv_to_hzb_buffer_uv_y = hzb_uv_factor_y,
      .hzb_uv_factor_x = hzb_uv_factor_x,
      .hzb_uv_factor_y = hzb_uv_factor_y,
      .hzb_uv_inv_factor_x = hzb_uv_inv_factor_x,
      .hzb_uv_inv_factor_y = hzb_uv_inv_factor_y,
      .hzb_uv_to_screen_uv_scale_x
      = hzb_uv_inv_factor_x * 2.0F * screen_position_scale_bias[0],
      .hzb_uv_to_screen_uv_scale_y
      = hzb_uv_inv_factor_y * -2.0F * screen_position_scale_bias[1],
      .hzb_uv_to_screen_uv_bias_x
      = -screen_position_scale_bias[0] + screen_position_scale_bias[3],
      .hzb_uv_to_screen_uv_bias_y
      = screen_position_scale_bias[1] + screen_position_scale_bias[2],
      .hzb_base_texel_size_x = 1.0F / static_cast<float>(state.width),
      .hzb_base_texel_size_y = 1.0F / static_cast<float>(state.height),
      .sample_pixel_to_hzb_uv_x = 0.5F / static_cast<float>(state.width),
      .sample_pixel_to_hzb_uv_y = 0.5F / static_cast<float>(state.height),
    };

    return ScreenHzbModule::Output {
      .closest_texture
      = closest_valid ? state.closest.history_textures[slot] : nullptr,
      .furthest_texture
      = furthest_valid ? state.furthest.history_textures[slot] : nullptr,
      .bindings = bindings,
      .available = true,
    };
  }

  Renderer& renderer;
  std::unordered_map<ViewId, ViewState> view_states {};
  std::shared_ptr<graphics::Buffer> pass_constants_buffer {};
  void* pass_constants_mapped_ptr { nullptr };
  std::vector<ShaderVisibleIndex> pass_constants_indices {};
  std::uint32_t pass_constants_capacity { 0U };
  std::optional<graphics::ComputePipelineDesc> pipeline_desc {};
};

ScreenHzbModule::ScreenHzbModule(
  Renderer& renderer, const SceneTexturesConfig& scene_textures_config)
  : impl_(std::make_unique<Impl>(renderer))
{
  static_cast<void>(scene_textures_config);
}

ScreenHzbModule::~ScreenHzbModule() = default;

void ScreenHzbModule::Execute(RenderContext& ctx, SceneTextures& scene_textures)
{
  current_output_ = {};
  previous_output_ = {};

  const auto view_id = ctx.current_view.view_id;
  if (view_id == kInvalidViewId) {
    return;
  }
  if (!ctx.current_view.screen_hzb_request.WantsCurrentHzb()) {
    return;
  }

  auto gfx = impl_->renderer.GetGraphics();
  if (gfx == nullptr) {
    return;
  }

  auto& scene_depth = scene_textures.GetSceneDepth();
  const auto scene_depth_width = scene_depth.GetDescriptor().width;
  const auto scene_depth_height = scene_depth.GetDescriptor().height;
  const auto build_closest = ctx.current_view.screen_hzb_request.current_closest;
  const auto build_furthest
    = ctx.current_view.screen_hzb_request.current_furthest;
  auto source_width = scene_depth_width;
  auto source_height = scene_depth_height;
  auto source_origin_x = 0U;
  auto source_origin_y = 0U;
  if (const auto* resolved_view = ctx.current_view.resolved_view.get();
    resolved_view != nullptr && resolved_view->Viewport().IsValid()) {
    const auto viewport = resolved_view->Viewport();
    source_origin_x = (std::min)(scene_depth_width - 1U,
      static_cast<std::uint32_t>(std::floor((std::max)(viewport.top_left_x, 0.0F))));
    source_origin_y = (std::min)(scene_depth_height - 1U,
      static_cast<std::uint32_t>(std::floor((std::max)(viewport.top_left_y, 0.0F))));
    source_width = (std::min)(scene_depth_width - source_origin_x,
      (std::max)(1U, static_cast<std::uint32_t>(std::ceil(viewport.width))));
    source_height = (std::min)(scene_depth_height - source_origin_y,
      (std::max)(1U, static_cast<std::uint32_t>(std::ceil(viewport.height))));
  }
  const auto width = ComputeHzbRootExtent(source_width);
  const auto height = ComputeHzbRootExtent(source_height);
  const auto mip_count = ComputeMipCount(width, height);
  auto scene_depth_srv = ShaderVisibleIndex { kInvalidShaderVisibleIndex };
  scene_depth_srv = impl_->EnsureTextureSrv(
    scene_depth, WholeTextureSrvDesc(scene_depth), scene_depth_srv, "scene-depth");
  if (!scene_depth_srv.IsValid()) {
    return;
  }

  auto& state = impl_->EnsureViewResources(
    view_id, width, height, mip_count, build_closest, build_furthest);
  state.scene_texture_width = scene_depth_width;
  state.scene_texture_height = scene_depth_height;
  state.source_view_rect_min_x = source_origin_x;
  state.source_view_rect_min_y = source_origin_y;
  state.source_view_rect_width = source_width;
  state.source_view_rect_height = source_height;
  impl_->EnsurePassConstantsBuffer(mip_count);
  if (!impl_->pipeline_desc.has_value()) {
    impl_->pipeline_desc = BuildPipelineDesc();
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key, "Vortex Stage5 ScreenHzb");
  if (!recorder) {
    return;
  }

  const auto write_slot
    = state.has_current_output ? (state.current_history_slot ^ 1U) : 0U;
  auto closest_write_texture = state.closest.history_textures[write_slot];
  auto furthest_write_texture = state.furthest.history_textures[write_slot];
  if (build_closest) {
    CHECK_NOTNULL_F(
      closest_write_texture.get(), "Screen HZB closest write texture is null");
  }
  if (build_furthest) {
    CHECK_NOTNULL_F(
      furthest_write_texture.get(), "Screen HZB furthest write texture is null");
  }

  TrackTextureFromKnownOrInitial(
    *recorder, scene_depth, graphics::ResourceStates::kDepthRead);
  if (build_closest) {
    TrackTextureFromKnownOrInitial(
      *recorder, *closest_write_texture, graphics::ResourceStates::kCommon);
    for (const auto& scratch_texture : state.closest.scratch_textures) {
      TrackTextureFromKnownOrInitial(
        *recorder, *scratch_texture, graphics::ResourceStates::kCommon);
    }
  }
  if (build_furthest) {
    TrackTextureFromKnownOrInitial(
      *recorder, *furthest_write_texture, graphics::ResourceStates::kCommon);
    for (const auto& scratch_texture : state.furthest.scratch_textures) {
      TrackTextureFromKnownOrInitial(
        *recorder, *scratch_texture, graphics::ResourceStates::kCommon);
    }
  }

  recorder->SetPipelineState(*impl_->pipeline_desc);
  if (ctx.view_constants != nullptr) {
    recorder->SetComputeRootConstantBufferView(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
      ctx.view_constants->GetGPUVirtualAddress());
  }

  const auto copy_scratch_to_history
    = [&](const Impl::PyramidResources& pyramid,
        const std::shared_ptr<graphics::Texture>& write_texture,
        const std::uint32_t scratch_slot, const std::uint32_t mip_level,
        const std::uint32_t destination_width,
        const std::uint32_t destination_height) {
        recorder->CopyTexture(*pyramid.scratch_textures[scratch_slot],
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
          *write_texture,
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
      };

  graphics::GpuEventScope pass_scope(*recorder, "Vortex.Stage5.ScreenHzbBuild",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);

  for (std::uint32_t mip_level = 0U; mip_level < state.mip_count; ++mip_level) {
    const auto destination_width = MipExtent(state.width, mip_level);
    const auto destination_height = MipExtent(state.height, mip_level);
    const auto source_mip_width = mip_level == 0U
      ? source_width
      : MipExtent(state.width, mip_level - 1U);
    const auto source_mip_height = mip_level == 0U
      ? source_height
      : MipExtent(state.height, mip_level - 1U);
    const auto scratch_slot = mip_level & 1U;

    auto closest_source_srv = ShaderVisibleIndex { kInvalidShaderVisibleIndex };
    auto furthest_source_srv = ShaderVisibleIndex { kInvalidShaderVisibleIndex };
    if (build_closest) {
      closest_source_srv = mip_level == 0U
        ? scene_depth_srv
        : state.closest.scratch_srv_indices[scratch_slot ^ 1U];
    }
    if (build_furthest) {
      furthest_source_srv = mip_level == 0U
        ? scene_depth_srv
        : state.furthest.scratch_srv_indices[scratch_slot ^ 1U];
    }
    const auto closest_destination_uav = build_closest
      ? state.closest.scratch_uav_indices[scratch_slot]
      : kInvalidShaderVisibleIndex;
    const auto furthest_destination_uav = build_furthest
      ? state.furthest.scratch_uav_indices[scratch_slot]
      : kInvalidShaderVisibleIndex;

    impl_->WritePassConstants(mip_level, ScreenHzbBuildConstants {
      .source_closest_texture_index = closest_source_srv,
      .source_furthest_texture_index = furthest_source_srv,
      .destination_closest_texture_uav_index = closest_destination_uav,
      .destination_furthest_texture_uav_index = furthest_destination_uav,
      .source_width = source_mip_width,
      .source_height = source_mip_height,
      .source_origin_x = mip_level == 0U ? source_origin_x : 0U,
      .source_origin_y = mip_level == 0U ? source_origin_y : 0U,
      .destination_width = destination_width,
      .destination_height = destination_height,
      .source_texel_step = 2U,
    });

    recorder->SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
      0U, 0U);
    recorder->SetComputeRoot32BitConstant(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
      impl_->pass_constants_indices[mip_level].get(), 1U);

    if (mip_level == 0U) {
      recorder->RequireResourceState(
        scene_depth, graphics::ResourceStates::kShaderResource);
    } else {
      if (build_closest) {
        recorder->RequireResourceState(
          *state.closest.scratch_textures[scratch_slot ^ 1U],
          graphics::ResourceStates::kShaderResource);
      }
      if (build_furthest) {
        recorder->RequireResourceState(
          *state.furthest.scratch_textures[scratch_slot ^ 1U],
          graphics::ResourceStates::kShaderResource);
      }
    }
    if (build_closest) {
      recorder->RequireResourceState(*state.closest.scratch_textures[scratch_slot],
        graphics::ResourceStates::kUnorderedAccess);
    }
    if (build_furthest) {
      recorder->RequireResourceState(*state.furthest.scratch_textures[scratch_slot],
        graphics::ResourceStates::kUnorderedAccess);
    }
    recorder->FlushBarriers();

    recorder->Dispatch(
      (destination_width + (kThreadGroupSize - 1U)) / kThreadGroupSize,
      (destination_height + (kThreadGroupSize - 1U)) / kThreadGroupSize, 1U);

    if (build_closest) {
      recorder->RequireResourceState(*state.closest.scratch_textures[scratch_slot],
        graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *closest_write_texture, graphics::ResourceStates::kCopyDest);
    }
    if (build_furthest) {
      recorder->RequireResourceState(*state.furthest.scratch_textures[scratch_slot],
        graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *furthest_write_texture, graphics::ResourceStates::kCopyDest);
    }
    recorder->FlushBarriers();

    if (build_closest) {
      copy_scratch_to_history(state.closest, closest_write_texture, scratch_slot,
        mip_level, destination_width, destination_height);
    }
    if (build_furthest) {
      copy_scratch_to_history(state.furthest, furthest_write_texture, scratch_slot,
        mip_level, destination_width, destination_height);
    }
  }

  recorder->RequireResourceStateFinal(
    scene_depth, graphics::ResourceStates::kDepthRead);
  if (build_closest) {
    recorder->RequireResourceStateFinal(
      *state.closest.scratch_textures[0], graphics::ResourceStates::kCommon);
    recorder->RequireResourceStateFinal(
      *state.closest.scratch_textures[1], graphics::ResourceStates::kCommon);
    recorder->RequireResourceStateFinal(
      *closest_write_texture, graphics::ResourceStates::kShaderResource);
  }
  if (build_furthest) {
    recorder->RequireResourceStateFinal(
      *state.furthest.scratch_textures[0], graphics::ResourceStates::kCommon);
    recorder->RequireResourceStateFinal(
      *state.furthest.scratch_textures[1], graphics::ResourceStates::kCommon);
    recorder->RequireResourceStateFinal(
      *furthest_write_texture, graphics::ResourceStates::kShaderResource);
  }

  const auto had_previous = state.has_current_output;
  state.current_history_slot = write_slot;
  current_output_ = Impl::BuildOutput(state, state.current_history_slot, true);
  state.has_current_output = current_output_.available;
  state.has_previous_output = had_previous && state.has_current_output;
  previous_output_ = Impl::BuildOutput(
    state, state.current_history_slot ^ 1U, state.has_previous_output);

  if (current_output_.available) {
    LOG_F(INFO, "screen_hzb_published=true width={} height={} mips={}",
      current_output_.bindings.width, current_output_.bindings.height,
      current_output_.bindings.mip_count);
  }
}

auto ScreenHzbModule::GetCurrentOutput() const -> const Output&
{
  return current_output_;
}

auto ScreenHzbModule::GetPreviousOutput() const -> const Output&
{
  return previous_output_;
}

} // namespace oxygen::vortex
