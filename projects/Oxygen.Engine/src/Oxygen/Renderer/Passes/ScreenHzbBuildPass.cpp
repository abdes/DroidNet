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
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
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
    ShaderVisibleIndex source_texture_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex destination_texture_uav_index {
      kInvalidShaderVisibleIndex
    };
    std::uint32_t source_width { 0U };
    std::uint32_t source_height { 0U };
    std::uint32_t destination_width { 0U };
    std::uint32_t destination_height { 0U };
    std::uint32_t source_texel_step { 1U };
    std::uint32_t _pad { 0U };
  };

  static_assert(sizeof(ScreenHzbBuildPassConstants) == 32U);
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

  [[nodiscard]] auto DepthSrvDesc(const graphics::Texture& texture)
    -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
      .view_type = graphics::ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = texture.GetDescriptor().format == oxygen::Format::kDepth32
        ? oxygen::Format::kR32Float
        : texture.GetDescriptor().format,
      .dimension = texture.GetDescriptor().texture_type,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      .is_read_only_dsv = false,
    };
  }

  auto BindDispatchConstants(graphics::CommandRecorder& recorder,
    const RenderContext& context, const ShaderVisibleIndex pass_constants_index)
    -> void
  {
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

} // namespace

struct ScreenHzbBuildPass::Impl {
  struct ViewState {
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    std::uint32_t mip_count { 0U };
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
  const graphics::Texture* depth_texture_owner { nullptr };
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
    for (auto& texture : state.history_textures) {
      if (texture && registry.Contains(*texture)) {
        registry.UnRegisterResource(*texture);
      }
      texture.reset();
    }
    for (auto& texture : state.scratch_textures) {
      if (texture && registry.Contains(*texture)) {
        registry.UnRegisterResource(*texture);
      }
      texture.reset();
    }
    state.history_srv_indices.fill(kInvalidShaderVisibleIndex);
    state.scratch_srv_indices.fill(kInvalidShaderVisibleIndex);
    state.scratch_uav_indices.fill(kInvalidShaderVisibleIndex);
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
        = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
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

    auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
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

    auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_UAV,
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

  auto EnsureDepthSrv(const graphics::Texture& depth_texture)
    -> ShaderVisibleIndex
  {
    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();
    const auto view_desc = DepthSrvDesc(depth_texture);

    if (const auto existing
      = registry.FindShaderVisibleIndex(depth_texture, view_desc);
      existing.has_value()) {
      active_depth_srv = *existing;
      depth_texture_owner = &depth_texture;
      return active_depth_srv;
    }

    if (active_depth_srv.IsValid() && depth_texture_owner == &depth_texture
      && registry.Contains(depth_texture, view_desc)) {
      return active_depth_srv;
    }

    auto register_new_view = [&]() -> ShaderVisibleIndex {
      auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
      if (!handle.IsValid()) {
        LOG_F(ERROR, "Screen HZB: failed to allocate depth SRV");
        active_depth_srv = kInvalidShaderVisibleIndex;
        return active_depth_srv;
      }

      active_depth_srv = allocator.GetShaderVisibleIndex(handle);
      const auto view
        = registry.RegisterView(const_cast<graphics::Texture&>(depth_texture),
          std::move(handle), view_desc);
      if (!view->IsValid()) {
        LOG_F(ERROR, "Screen HZB: failed to register depth SRV");
        active_depth_srv = kInvalidShaderVisibleIndex;
        return active_depth_srv;
      }

      depth_texture_owner = &depth_texture;
      return active_depth_srv;
    };

    if (!active_depth_srv.IsValid()) {
      return register_new_view();
    }

    const auto updated
      = registry.UpdateView(const_cast<graphics::Texture&>(depth_texture),
        bindless::HeapIndex { active_depth_srv.get() }, view_desc);
    if (!updated) {
      LOG_F(
        INFO, "Screen HZB: depth SRV update failed, re-registering descriptor");
      active_depth_srv = kInvalidShaderVisibleIndex;
      depth_texture_owner = nullptr;
      return register_new_view();
    }

    depth_texture_owner = &depth_texture;
    return active_depth_srv;
  }

  auto EnsureViewResources(const ViewId view_id, const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t mip_count) -> ViewState&
  {
    auto& state = view_states[view_id];
    auto& registry = gfx->GetResourceRegistry();

    const bool needs_recreate = state.history_textures[0] == nullptr
      || state.history_textures[1] == nullptr
      || state.scratch_textures[0] == nullptr
      || state.scratch_textures[1] == nullptr || state.width != width
      || state.height != height || state.mip_count != mip_count;
    if (!needs_recreate) {
      return state;
    }

    if (state.history_textures[0] != nullptr
      || state.history_textures[1] != nullptr
      || state.scratch_textures[0] != nullptr
      || state.scratch_textures[1] != nullptr) {
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
        = base_name + "_HzbHistory" + std::to_string(slot);
      state.history_textures[slot] = gfx->CreateTexture(history_desc);
      CHECK_NOTNULL_F(state.history_textures[slot].get(),
        "Failed to create Screen HZB history texture");
      registry.Register(state.history_textures[slot]);

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
        = base_name + "_HzbScratch" + std::to_string(slot);
      state.scratch_textures[slot] = gfx->CreateTexture(scratch_desc);
      CHECK_NOTNULL_F(state.scratch_textures[slot].get(),
        "Failed to create Screen HZB scratch texture");
      registry.Register(state.scratch_textures[slot]);

      const auto history_srv = EnsureTextureSrv(*state.history_textures[slot],
        WholeTextureSrvDesc(), state.history_srv_indices[slot],
        slot == 0U ? "history-0" : "history-1");
      CHECK_F(history_srv.IsValid(), "Screen HZB history SRV must be valid");

      const auto scratch_srv = EnsureTextureSrv(*state.scratch_textures[slot],
        SingleMipSrvDesc(), state.scratch_srv_indices[slot],
        slot == 0U ? "scratch-0" : "scratch-1");
      CHECK_F(scratch_srv.IsValid(), "Screen HZB scratch SRV must be valid");

      const auto scratch_uav = EnsureTextureUav(*state.scratch_textures[slot],
        SingleMipUavDesc(), state.scratch_uav_indices[slot],
        slot == 0U ? "scratch-0" : "scratch-1");
      CHECK_F(scratch_uav.IsValid(), "Screen HZB scratch UAV must be valid");
    }

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
    -> ScreenHzbBuildPass::ViewOutput
  {
    if (!available) {
      return {};
    }
    return ScreenHzbBuildPass::ViewOutput {
      .texture = state.history_textures[slot],
      .srv_index = state.history_srv_indices[slot],
      .width = state.width,
      .height = state.height,
      .mip_count = state.mip_count,
      .available = state.history_textures[slot] != nullptr
        && state.history_srv_indices[slot].IsValid(),
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

auto ScreenHzbBuildPass::GetCurrentOutput(const ViewId view_id) const
  -> ViewOutput
{
  const auto it = impl_->view_states.find(view_id);
  if (it == impl_->view_states.end()) {
    return {};
  }
  return Impl::BuildOutput(
    it->second, it->second.current_history_slot, it->second.has_current_output);
}

auto ScreenHzbBuildPass::GetPreviousFrameOutput(const ViewId view_id) const
  -> ViewOutput
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

  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  if (depth_pass == nullptr) {
    LOG_F(
      WARNING, "Screen HZB pass skipped because DepthPrePass is unavailable");
    co_return;
  }

  const auto& depth_texture = depth_pass->GetDepthTexture();
  const auto& depth_desc = depth_texture.GetDescriptor();
  const auto mip_count = ComputeMipCount(depth_desc.width, depth_desc.height);
  auto& state = impl_->EnsureViewResources(
    view_id, depth_desc.width, depth_desc.height, mip_count);

  impl_->EnsurePassConstantsBuffer(mip_count);

  const auto depth_srv = impl_->EnsureDepthSrv(depth_texture);
  if (!depth_srv.IsValid()) {
    LOG_F(ERROR, "Screen HZB pass failed to acquire a valid depth SRV");
    co_return;
  }

  impl_->active_view_state = &state;
  impl_->active_view_id = view_id;
  impl_->active_depth_texture = &depth_texture;
  impl_->active_write_slot
    = state.has_current_output ? (state.current_history_slot ^ 1U) : 0U;

  auto* write_texture = state.history_textures[impl_->active_write_slot].get();
  CHECK_NOTNULL_F(write_texture, "Screen HZB write texture is unavailable");

  if (!recorder.IsResourceTracked(depth_texture)) {
    recorder.BeginTrackingResourceState(
      depth_texture, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*write_texture)) {
    recorder.BeginTrackingResourceState(*write_texture,
      impl_->GetHistoryInitialState(state, impl_->active_write_slot), true);
  }
  for (const auto& scratch_texture : state.scratch_textures) {
    CHECK_NOTNULL_F(
      scratch_texture.get(), "Screen HZB scratch texture is unavailable");
    if (!recorder.IsResourceTracked(*scratch_texture)) {
      recorder.BeginTrackingResourceState(
        *scratch_texture, graphics::ResourceStates::kCommon, true);
    }
  }

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
  auto write_texture = state.history_textures[impl_->active_write_slot];
  CHECK_NOTNULL_F(write_texture.get(), "Screen HZB write texture is null");

  for (std::uint32_t mip_level = 0U; mip_level < state.mip_count; ++mip_level) {
    const auto destination_width = MipExtent(state.width, mip_level);
    const auto destination_height = MipExtent(state.height, mip_level);
    const auto source_width
      = mip_level == 0U ? state.width : MipExtent(state.width, mip_level - 1U);
    const auto source_height = mip_level == 0U
      ? state.height
      : MipExtent(state.height, mip_level - 1U);
    const auto scratch_slot = mip_level & 1U;

    ShaderVisibleIndex source_srv = impl_->active_depth_srv;
    if (mip_level > 0U) {
      source_srv = state.scratch_srv_indices[scratch_slot ^ 1U];
    }
    const auto destination_uav = state.scratch_uav_indices[scratch_slot];

    CHECK_F(source_srv.IsValid(), "Screen HZB source SRV is invalid for mip {}",
      mip_level);
    CHECK_F(destination_uav.IsValid(),
      "Screen HZB destination UAV is invalid for mip {}", mip_level);

    const auto constants = ScreenHzbBuildPassConstants {
      .source_texture_index = source_srv,
      .destination_texture_uav_index = destination_uav,
      .source_width = source_width,
      .source_height = source_height,
      .destination_width = destination_width,
      .destination_height = destination_height,
      .source_texel_step = mip_level == 0U ? 1U : 2U,
    };
    impl_->WritePassConstants(mip_level, constants);
    SetPassConstantsIndex(impl_->pass_constants_indices[mip_level]);
    // This pass switches pass-constant slots per mip, so it must refresh the
    // root constants per dispatch instead of relying on ComputeRenderPass's
    // one-time Execute() binding.
    BindDispatchConstants(
      recorder, Context(), impl_->pass_constants_indices[mip_level]);

    if (mip_level == 0U) {
      recorder.RequireResourceState(*impl_->active_depth_texture,
        graphics::ResourceStates::kShaderResource);
    } else {
      recorder.RequireResourceState(*state.scratch_textures[scratch_slot ^ 1U],
        graphics::ResourceStates::kShaderResource);
    }
    recorder.RequireResourceState(*state.scratch_textures[scratch_slot],
      graphics::ResourceStates::kUnorderedAccess);
    recorder.FlushBarriers();

    recorder.Dispatch(
      (destination_width + (kThreadGroupSize - 1U)) / kThreadGroupSize,
      (destination_height + (kThreadGroupSize - 1U)) / kThreadGroupSize, 1U);

    recorder.RequireResourceState(*state.scratch_textures[scratch_slot],
      graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *write_texture, graphics::ResourceStates::kCopyDest);
    recorder.FlushBarriers();

    recorder.CopyTexture(*state.scratch_textures[scratch_slot],
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
  }

  recorder.RequireResourceState(
    *state.scratch_textures[0], graphics::ResourceStates::kCommon);
  recorder.RequireResourceState(
    *state.scratch_textures[1], graphics::ResourceStates::kCommon);
  recorder.RequireResourceState(
    *write_texture, graphics::ResourceStates::kShaderResource);
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
