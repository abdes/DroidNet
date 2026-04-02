//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/ScreenHzbBuildPass.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

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
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/RenderContext.h>

namespace oxygen::engine {

namespace {

  constexpr std::uint32_t kThreadGroupSize = 8U;
  constexpr std::uint32_t kPassConstantsStride
    = packing::kConstantBufferAlignment;

  struct alignas(packing::kShaderDataFieldAlignment)
    ScreenHzbBuildPassConstants {
    ShaderVisibleIndex source_closest_texture_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex source_furthest_texture_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex destination_closest_texture_uav_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex destination_furthest_texture_uav_index {
      kInvalidShaderVisibleIndex
    };
    std::uint32_t source_width { 0U };
    std::uint32_t source_height { 0U };
    std::uint32_t destination_width { 0U };
    std::uint32_t destination_height { 0U };
    std::uint32_t source_texel_step { 1U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };
  };

  static_assert(sizeof(ScreenHzbBuildPassConstants) == 48U);
  static_assert(
    sizeof(ScreenHzbBuildPassConstants) % packing::kShaderDataFieldAlignment
    == 0U);

  [[nodiscard]] auto ComputeMipCount(
    const std::uint32_t width, const std::uint32_t height) -> std::uint32_t
  {
    CHECK_F(width != 0U && height != 0U,
      "Screen HZB requires non-zero texture dimensions");
    return std::bit_width((std::max)(width, height));
  }

  [[nodiscard]] auto MipExtent(const std::uint32_t base_extent,
    const std::uint32_t mip_level) -> std::uint32_t
  {
    return (std::max)(1U, base_extent >> mip_level);
  }

  [[nodiscard]] auto WholeTextureSrvDesc() -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
      .view_type = graphics::ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = oxygen::Format::kR32Float,
      .dimension = oxygen::TextureType::kTexture2D,
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

  [[nodiscard]] auto DepthTextureInitialState(const graphics::Texture& texture)
    -> graphics::ResourceStates
  {
    const auto initial_state = texture.GetDescriptor().initial_state;
    if (initial_state != graphics::ResourceStates::kUnknown
      && initial_state != graphics::ResourceStates::kUndefined) {
      return initial_state;
    }
    switch (texture.GetDescriptor().format) {
    case oxygen::Format::kDepth16:
    case oxygen::Format::kDepth24Stencil8:
    case oxygen::Format::kDepth32:
    case oxygen::Format::kDepth32Stencil8:
      return graphics::ResourceStates::kDepthWrite;
    default:
      return graphics::ResourceStates::kCommon;
    }
  }

  auto BindDispatchConstants(graphics::CommandRecorder& recorder,
    const RenderContext& context, const ShaderVisibleIndex pass_constants_index)
    -> void
  {
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

} // namespace

struct ScreenHzbBuildPass::Impl {
  struct PyramidResources {
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
    PyramidResources closest {};
    PyramidResources furthest {};
    std::uint32_t current_history_slot { 0U };
    bool has_current_output { false };
    bool has_previous_output { false };
  };

  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;
  std::unordered_map<ViewId, ViewState> view_states {};

  std::shared_ptr<graphics::Buffer> pass_constants_buffer;
  void* pass_constants_mapped_ptr { nullptr };
  std::vector<ShaderVisibleIndex> pass_constants_indices {};
  std::uint32_t pass_constants_capacity { 0U };

  const graphics::Texture* active_depth_texture { nullptr };
  ShaderVisibleIndex active_depth_srv { kInvalidShaderVisibleIndex };
  ViewState* active_view_state { nullptr };
  ViewId active_view_id {};
  std::uint32_t active_write_slot { 0U };
  bool resources_prepared { false };

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl()
  {
    if (pass_constants_buffer && pass_constants_mapped_ptr != nullptr) {
      pass_constants_buffer->UnMap();
      pass_constants_mapped_ptr = nullptr;
    }

    if (gfx == nullptr) {
      return;
    }

    auto& registry = gfx->GetResourceRegistry();
    if (pass_constants_buffer && registry.Contains(*pass_constants_buffer)) {
      registry.UnRegisterResource(*pass_constants_buffer);
    }

    for (auto& [view_id, state] : view_states) {
      (void)view_id;
      ReleaseViewResources(registry, state);
    }
  }

  static auto ReleaseViewResources(
    graphics::ResourceRegistry& registry, ViewState& state) -> void
  {
    const auto release_pyramid = [&](PyramidResources& pyramid) {
      for (auto& texture : pyramid.history_textures) {
        if (texture && registry.Contains(*texture)) {
          registry.UnRegisterResource(*texture);
        }
        texture.reset();
      }
      for (auto& texture : pyramid.scratch_textures) {
        if (texture && registry.Contains(*texture)) {
          registry.UnRegisterResource(*texture);
        }
        texture.reset();
      }
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
  }

  auto EnsurePassConstantsBuffer(const std::uint32_t slot_count) -> void
  {
    if (pass_constants_buffer && pass_constants_capacity >= slot_count) {
      return;
    }

    CHECK_NOTNULL_F(gfx.get(), "Screen HZB pass requires Graphics");
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
      .debug_name = config->debug_name + "_PassConstants",
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
    const ScreenHzbBuildPassConstants& constants) const -> void
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
    const std::uint32_t height, const std::uint32_t mip_count) -> ViewState&
  {
    auto& state = view_states[view_id];
    auto& registry = gfx->GetResourceRegistry();

    const auto pyramid_missing_resource
      = [](const PyramidResources& pyramid) -> bool {
      return pyramid.history_textures[0] == nullptr
        || pyramid.history_textures[1] == nullptr
        || pyramid.scratch_textures[0] == nullptr
        || pyramid.scratch_textures[1] == nullptr;
    };
    const auto pyramid_has_any_resource
      = [](const PyramidResources& pyramid) -> bool {
      return pyramid.history_textures[0] != nullptr
        || pyramid.history_textures[1] != nullptr
        || pyramid.scratch_textures[0] != nullptr
        || pyramid.scratch_textures[1] != nullptr;
    };

    const bool needs_recreate = pyramid_missing_resource(state.closest)
      || pyramid_missing_resource(state.furthest) || state.width != width
      || state.height != height || state.mip_count != mip_count;
    if (!needs_recreate) {
      return state;
    }

    if (pyramid_has_any_resource(state.closest)
      || pyramid_has_any_resource(state.furthest)) {
      LOG_F(INFO,
        "Screen HZB: recreating per-view resources for view {} (old={}x{} "
        "mips={} new={}x{} mips={}); previous-frame history becomes "
        "unavailable",
        view_id.get(), state.width, state.height, state.mip_count, width,
        height, mip_count);
      ReleaseViewResources(registry, state);
    }

    const auto base_name
      = config->debug_name + "_View" + std::to_string(view_id.get());

    const auto create_pyramid = [&](PyramidResources& pyramid,
                                  const char* semantic_label) {
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
          = base_name + "_" + semantic_label + "History" + std::to_string(slot);
        pyramid.history_textures[slot] = gfx->CreateTexture(history_desc);
        CHECK_NOTNULL_F(pyramid.history_textures[slot].get(),
          "Failed to create Screen HZB {} history texture", semantic_label);
        registry.Register(pyramid.history_textures[slot]);

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
          = base_name + "_" + semantic_label + "Scratch" + std::to_string(slot);
        pyramid.scratch_textures[slot] = gfx->CreateTexture(scratch_desc);
        CHECK_NOTNULL_F(pyramid.scratch_textures[slot].get(),
          "Failed to create Screen HZB {} scratch texture", semantic_label);
        registry.Register(pyramid.scratch_textures[slot]);

        const auto history_label = slot == 0U
          ? std::string(semantic_label) + "-history-0"
          : std::string(semantic_label) + "-history-1";
        const auto history_srv = EnsureTextureSrv(
          *pyramid.history_textures[slot], WholeTextureSrvDesc(),
          pyramid.history_srv_indices[slot], history_label.c_str());
        CHECK_F(history_srv.IsValid(), "Screen HZB history SRV must be valid");

        const auto scratch_label = slot == 0U
          ? std::string(semantic_label) + "-scratch-0"
          : std::string(semantic_label) + "-scratch-1";
        const auto scratch_srv = EnsureTextureSrv(
          *pyramid.scratch_textures[slot], SingleMipSrvDesc(),
          pyramid.scratch_srv_indices[slot], scratch_label.c_str());
        CHECK_F(scratch_srv.IsValid(), "Screen HZB scratch SRV must be valid");

        const auto scratch_uav = EnsureTextureUav(
          *pyramid.scratch_textures[slot], SingleMipUavDesc(),
          pyramid.scratch_uav_indices[slot], scratch_label.c_str());
        CHECK_F(scratch_uav.IsValid(), "Screen HZB scratch UAV must be valid");
      }
    };

    create_pyramid(state.closest, "Closest");
    create_pyramid(state.furthest, "Furthest");

    state.width = width;
    state.height = height;
    state.mip_count = mip_count;
    state.current_history_slot = 0U;
    state.has_current_output = false;
    state.has_previous_output = false;
    return state;
  }

  [[nodiscard]] auto GetHistoryInitialState(const ViewState& state,
    const std::uint32_t slot) const -> graphics::ResourceStates
  {
    if (state.has_current_output && slot == state.current_history_slot) {
      return graphics::ResourceStates::kShaderResource;
    }
    if (state.has_previous_output && slot != state.current_history_slot) {
      return graphics::ResourceStates::kShaderResource;
    }
    return graphics::ResourceStates::kCommon;
  }

  [[nodiscard]] static auto BuildOutput(
    const ViewState& state, const std::uint32_t slot, const bool available)
    -> ScreenHzbBuildPass::Output
  {
    if (!available) {
      return {};
    }
    return ScreenHzbBuildPass::Output {
      .closest_texture = state.closest.history_textures[slot],
      .furthest_texture = state.furthest.history_textures[slot],
      .closest_srv_index = state.closest.history_srv_indices[slot],
      .furthest_srv_index = state.furthest.history_srv_indices[slot],
      .width = state.width,
      .height = state.height,
      .mip_count = state.mip_count,
      .available = state.closest.history_textures[slot] != nullptr
        && state.furthest.history_textures[slot] != nullptr
        && state.closest.history_srv_indices[slot].IsValid()
        && state.furthest.history_srv_indices[slot].IsValid(),
    };
  }
};

ScreenHzbBuildPass::ScreenHzbBuildPass(
  observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "ScreenHzbBuildPass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
  DCHECK_NOTNULL_F(gfx.get());
  DCHECK_NOTNULL_F(impl_->config.get());
}

ScreenHzbBuildPass::~ScreenHzbBuildPass() = default;

auto ScreenHzbBuildPass::GetCurrentOutput(const ViewId view_id) const -> Output
{
  const auto it = impl_->view_states.find(view_id);
  if (it == impl_->view_states.end()) {
    return {};
  }
  return Impl::BuildOutput(
    it->second, it->second.current_history_slot, it->second.has_current_output);
}

auto ScreenHzbBuildPass::GetPreviousFrameOutput(const ViewId view_id) const
  -> Output
{
  const auto it = impl_->view_states.find(view_id);
  if (it == impl_->view_states.end() || !it->second.has_previous_output) {
    return {};
  }
  const auto previous_slot = it->second.current_history_slot ^ 1U;
  return Impl::BuildOutput(it->second, previous_slot, true);
}

auto ScreenHzbBuildPass::ValidateConfig() -> void
{
  if (!impl_->config) {
    throw std::runtime_error("ScreenHzbBuildPass requires configuration");
  }
}

auto ScreenHzbBuildPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

auto ScreenHzbBuildPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto root_bindings = BuildRootBindings();
  const auto bindings = std::span<const graphics::RootBindingItem>(
    root_bindings.data(), root_bindings.size());

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader({ .stage = oxygen::ShaderType::kCompute,
      .source_path = "Renderer/ScreenHzbBuild.hlsl",
      .entry_point = "CS" })
    .SetRootBindings(bindings)
    .SetDebugName("ScreenHzbBuild")
    .Build();
}

auto ScreenHzbBuildPass::DoPrepareResources(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  impl_->resources_prepared = false;
  impl_->active_depth_texture = nullptr;
  impl_->active_view_state = nullptr;
  impl_->active_depth_srv = kInvalidShaderVisibleIndex;

  const auto view_id = Context().current_view.view_id;
  if (view_id == kInvalidViewId) {
    LOG_F(WARNING,
      "Screen HZB pass skipped because current_view.view_id is invalid");
    co_return;
  }

  if (Context().current_view.depth_prepass_completeness
    == renderer::DepthPrePassCompleteness::kDisabled) {
    DLOG_F(2,
      "Screen HZB pass skipped because DepthPrePass mode is disabled for "
      "view {}",
      view_id.get());
    co_return;
  }

  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  if (depth_pass == nullptr) {
    const auto status = Context().current_view.depth_prepass_completeness;
    const auto level = status == renderer::DepthPrePassCompleteness::kComplete
      ? loguru::Verbosity_ERROR
      : loguru::Verbosity_WARNING;
    VLOG_F(level,
      "Screen HZB pass skipped because DepthPrePass is unavailable for "
      "view {} (early depth status={})",
      view_id.get(), to_string(status));
    co_return;
  }

  const auto depth_output = depth_pass->GetOutput();
  if (!depth_output.is_complete || depth_output.depth_texture == nullptr
    || !depth_output.has_canonical_srv) {
    LOG_F(WARNING,
      "Screen HZB pass skipped because DepthPrePass output is incomplete");
    co_return;
  }

  const auto& depth_texture = *depth_output.depth_texture;
  const auto mip_count
    = ComputeMipCount(depth_output.width, depth_output.height);
  auto& state = impl_->EnsureViewResources(
    view_id, depth_output.width, depth_output.height, mip_count);

  impl_->EnsurePassConstantsBuffer(mip_count);

  impl_->active_view_state = &state;
  impl_->active_view_id = view_id;
  impl_->active_depth_texture = &depth_texture;
  impl_->active_depth_srv = depth_output.canonical_srv_index;
  impl_->active_write_slot
    = state.has_current_output ? (state.current_history_slot ^ 1U) : 0U;

  auto* closest_write_texture
    = state.closest.history_textures[impl_->active_write_slot].get();
  auto* furthest_write_texture
    = state.furthest.history_textures[impl_->active_write_slot].get();
  CHECK_NOTNULL_F(
    closest_write_texture, "Screen HZB closest write texture is unavailable");
  CHECK_NOTNULL_F(
    furthest_write_texture, "Screen HZB furthest write texture is unavailable");

  if (!recorder.IsResourceTracked(depth_texture)) {
    recorder.BeginTrackingResourceState(
      depth_texture, DepthTextureInitialState(depth_texture), true);
  }
  if (!recorder.IsResourceTracked(*closest_write_texture)) {
    recorder.BeginTrackingResourceState(*closest_write_texture,
      impl_->GetHistoryInitialState(state, impl_->active_write_slot), true);
  }
  if (!recorder.IsResourceTracked(*furthest_write_texture)) {
    recorder.BeginTrackingResourceState(*furthest_write_texture,
      impl_->GetHistoryInitialState(state, impl_->active_write_slot), true);
  }
  const auto track_scratch_resources
    = [&](const Impl::PyramidResources& pyramid, const char* label) {
        for (const auto& scratch_texture : pyramid.scratch_textures) {
          CHECK_NOTNULL_F(scratch_texture.get(),
            "Screen HZB {} scratch texture is unavailable", label);
          if (!recorder.IsResourceTracked(*scratch_texture)) {
            recorder.BeginTrackingResourceState(
              *scratch_texture, graphics::ResourceStates::kCommon, true);
          }
        }
      };
  track_scratch_resources(state.closest, "closest");
  track_scratch_resources(state.furthest, "furthest");
  impl_->resources_prepared = true;

  co_return;
}

auto ScreenHzbBuildPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  if (!impl_->resources_prepared || impl_->active_view_state == nullptr
    || impl_->active_depth_texture == nullptr) {
    DLOG_F(2, "Screen HZB pass had no prepared work, skipping");
    co_return;
  }

  auto& state = *impl_->active_view_state;
  const auto had_previous = state.has_current_output;
  auto closest_write_texture
    = state.closest.history_textures[impl_->active_write_slot];
  auto furthest_write_texture
    = state.furthest.history_textures[impl_->active_write_slot];
  CHECK_NOTNULL_F(
    closest_write_texture.get(), "Screen HZB closest write texture is null");
  CHECK_NOTNULL_F(
    furthest_write_texture.get(), "Screen HZB furthest write texture is null");

  const auto copy_scratch_to_history
    = [&](const Impl::PyramidResources& pyramid,
        const std::shared_ptr<graphics::Texture>& write_texture,
        const std::uint32_t scratch_slot, const std::uint32_t mip_level,
        const std::uint32_t destination_width,
        const std::uint32_t destination_height) {
        recorder.CopyTexture(*pyramid.scratch_textures[scratch_slot],
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

  for (std::uint32_t mip_level = 0U; mip_level < state.mip_count; ++mip_level) {
    const auto destination_width = MipExtent(state.width, mip_level);
    const auto destination_height = MipExtent(state.height, mip_level);
    const auto source_width
      = mip_level == 0U ? state.width : MipExtent(state.width, mip_level - 1U);
    const auto source_height = mip_level == 0U
      ? state.height
      : MipExtent(state.height, mip_level - 1U);
    const auto scratch_slot = mip_level & 1U;

    ShaderVisibleIndex closest_source_srv = impl_->active_depth_srv;
    ShaderVisibleIndex furthest_source_srv = impl_->active_depth_srv;
    if (mip_level > 0U) {
      closest_source_srv = state.closest.scratch_srv_indices[scratch_slot ^ 1U];
      furthest_source_srv
        = state.furthest.scratch_srv_indices[scratch_slot ^ 1U];
    }
    const auto closest_destination_uav
      = state.closest.scratch_uav_indices[scratch_slot];
    const auto furthest_destination_uav
      = state.furthest.scratch_uav_indices[scratch_slot];

    CHECK_F(closest_source_srv.IsValid(),
      "Screen HZB closest source SRV is invalid for mip {}", mip_level);
    CHECK_F(furthest_source_srv.IsValid(),
      "Screen HZB furthest source SRV is invalid for mip {}", mip_level);
    CHECK_F(closest_destination_uav.IsValid(),
      "Screen HZB closest destination UAV is invalid for mip {}", mip_level);
    CHECK_F(furthest_destination_uav.IsValid(),
      "Screen HZB furthest destination UAV is invalid for mip {}", mip_level);

    const auto constants = ScreenHzbBuildPassConstants {
      .source_closest_texture_index = closest_source_srv,
      .source_furthest_texture_index = furthest_source_srv,
      .destination_closest_texture_uav_index = closest_destination_uav,
      .destination_furthest_texture_uav_index = furthest_destination_uav,
      .source_width = source_width,
      .source_height = source_height,
      .destination_width = destination_width,
      .destination_height = destination_height,
      .source_texel_step = mip_level == 0U ? 1U : 2U,
    };
    impl_->WritePassConstants(mip_level, constants);
    SetPassConstantsIndex(impl_->pass_constants_indices[mip_level]);
    BindDispatchConstants(
      recorder, Context(), impl_->pass_constants_indices[mip_level]);

    if (mip_level == 0U) {
      recorder.RequireResourceState(*impl_->active_depth_texture,
        graphics::ResourceStates::kShaderResource);
    } else {
      recorder.RequireResourceState(
        *state.closest.scratch_textures[scratch_slot ^ 1U],
        graphics::ResourceStates::kShaderResource);
      recorder.RequireResourceState(
        *state.furthest.scratch_textures[scratch_slot ^ 1U],
        graphics::ResourceStates::kShaderResource);
    }
    recorder.RequireResourceState(*state.closest.scratch_textures[scratch_slot],
      graphics::ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(
      *state.furthest.scratch_textures[scratch_slot],
      graphics::ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();

    recorder.Dispatch(
      (destination_width + (kThreadGroupSize - 1U)) / kThreadGroupSize,
      (destination_height + (kThreadGroupSize - 1U)) / kThreadGroupSize, 1U);

    recorder.RequireResourceState(*state.closest.scratch_textures[scratch_slot],
      graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *state.furthest.scratch_textures[scratch_slot],
      graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *closest_write_texture, graphics::ResourceStates::kCopyDest);
    recorder.RequireResourceState(
      *furthest_write_texture, graphics::ResourceStates::kCopyDest);
    recorder.FlushBarriers();

    copy_scratch_to_history(state.closest, closest_write_texture, scratch_slot,
      mip_level, destination_width, destination_height);
    copy_scratch_to_history(state.furthest, furthest_write_texture,
      scratch_slot, mip_level, destination_width, destination_height);
  }

  recorder.RequireResourceState(
    *state.closest.scratch_textures[0], graphics::ResourceStates::kCommon);
  recorder.RequireResourceState(
    *state.closest.scratch_textures[1], graphics::ResourceStates::kCommon);
  recorder.RequireResourceState(
    *state.furthest.scratch_textures[0], graphics::ResourceStates::kCommon);
  recorder.RequireResourceState(
    *state.furthest.scratch_textures[1], graphics::ResourceStates::kCommon);
  recorder.RequireResourceState(
    *closest_write_texture, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *furthest_write_texture, graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  state.current_history_slot = impl_->active_write_slot;
  state.has_previous_output = had_previous;
  state.has_current_output = true;

  DLOG_F(2, "Screen HZB built for view {}: {}x{} mips={} previous_available={}",
    impl_->active_view_id.get(), state.width, state.height, state.mip_count,
    state.has_previous_output);

  Context().RegisterPass(this);
  co_return;
}

} // namespace oxygen::engine
