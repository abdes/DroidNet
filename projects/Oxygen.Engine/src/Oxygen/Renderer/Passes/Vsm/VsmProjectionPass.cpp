//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/Vsm/VsmProjectionPass.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Constants.h>
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
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>

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
using oxygen::renderer::vsm::VsmProjectionLightType;

namespace oxygen::engine {

namespace {

  constexpr std::uint32_t kThreadGroupSize = 8U;
  constexpr std::uint32_t kPassConstantsStride
    = packing::kConstantBufferAlignment;

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmProjectionClearPassConstants {
    ShaderVisibleIndex directional_shadow_mask_uav_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex shadow_mask_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t output_width { 0U };
    std::uint32_t output_height { 0U };
  };
  static_assert(
    sizeof(VsmProjectionClearPassConstants) % packing::kShaderDataFieldAlignment
    == 0U);

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmProjectionCompositePassConstants {
    ShaderVisibleIndex scene_depth_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex directional_shadow_mask_uav_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex shadow_mask_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex projection_buffer_srv_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex page_table_buffer_srv_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex shadow_texture_srv_index { kInvalidShaderVisibleIndex };
    std::uint32_t page_table_entry_count { 0U };
    std::uint32_t projection_count { 0U };
    std::uint32_t output_width { 0U };
    std::uint32_t output_height { 0U };
    std::uint32_t page_size_texels { 0U };
    std::uint32_t tiles_per_axis { 0U };
    std::uint32_t dynamic_slice_index { 0U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };
    glm::mat4 inverse_view_projection { 1.0F };
    glm::mat4 main_view_matrix { 1.0F };
  };
  static_assert(sizeof(VsmProjectionCompositePassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

  [[nodiscard]] auto MakeDispatchGroups(const std::uint32_t count) noexcept
    -> std::uint32_t
  {
    return count == 0U ? 0U
                       : (count + kThreadGroupSize - 1U) / kThreadGroupSize;
  }

  [[nodiscard]] auto MakeStructuredSrvDesc(const std::uint64_t size_bytes,
    const std::uint32_t stride) -> graphics::BufferViewDescription
  {
    return graphics::BufferViewDescription {
      .view_type = ResourceViewType::kStructuredBuffer_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = { 0U, size_bytes },
      .stride = stride,
    };
  }

  [[nodiscard]] auto DepthSrvDesc(const graphics::Texture& texture)
    -> graphics::TextureViewDescription
  {
    auto format = texture.GetDescriptor().format;
    switch (format) {
    case oxygen::Format::kDepth32:
    case oxygen::Format::kDepth24Stencil8:
    case oxygen::Format::kDepth32Stencil8:
      format = oxygen::Format::kR32Float;
      break;
    case oxygen::Format::kDepth16:
      format = oxygen::Format::kR16UNorm;
      break;
    default:
      break;
    }

    return graphics::TextureViewDescription {
      .view_type = ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = format,
      .dimension = texture.GetDescriptor().texture_type,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      .is_read_only_dsv = false,
    };
  }

  [[nodiscard]] auto DepthTextureInitialState(const graphics::Texture& texture)
    -> ResourceStates
  {
    const auto initial_state = texture.GetDescriptor().initial_state;
    if (initial_state != ResourceStates::kUnknown
      && initial_state != ResourceStates::kUndefined) {
      return initial_state;
    }
    switch (texture.GetDescriptor().format) {
    case oxygen::Format::kDepth16:
    case oxygen::Format::kDepth24Stencil8:
    case oxygen::Format::kDepth32:
    case oxygen::Format::kDepth32Stencil8:
      return ResourceStates::kDepthWrite;
    default:
      return ResourceStates::kCommon;
    }
  }

  [[nodiscard]] auto WholeShadowTextureSrvDesc(const oxygen::Format format)
    -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
      .view_type = ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format
      = format == oxygen::Format::kDepth32 ? oxygen::Format::kR32Float : format,
      .dimension = oxygen::TextureType::kTexture2DArray,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      .is_read_only_dsv = false,
    };
  }

  [[nodiscard]] auto ShadowMaskSrvDesc() -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
      .view_type = ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = oxygen::Format::kR32Float,
      .dimension = oxygen::TextureType::kTexture2D,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      .is_read_only_dsv = false,
    };
  }

  [[nodiscard]] auto ShadowMaskUavDesc() -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
      .view_type = ResourceViewType::kTexture_UAV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = oxygen::Format::kR32Float,
      .dimension = oxygen::TextureType::kTexture2D,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      .is_read_only_dsv = false,
    };
  }

  [[nodiscard]] auto FindDynamicSliceIndex(
    const renderer::vsm::VsmPhysicalPoolSnapshot& pool) noexcept
    -> std::optional<std::uint32_t>
  {
    for (std::uint32_t i = 0U; i < pool.slice_roles.size(); ++i) {
      if (pool.slice_roles[i] == VsmPhysicalPoolSliceRole::kDynamicDepth) {
        return i;
      }
    }
    return std::nullopt;
  }

  auto BindComputeStage(CommandRecorder& recorder,
    const graphics::ComputePipelineDesc& pso_desc,
    const ShaderVisibleIndex pass_constants_index, const RenderContext& context)
    -> void
  {
    DCHECK_NOTNULL_F(context.view_constants);
    recorder.SetPipelineState(pso_desc);
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

} // namespace

struct VsmProjectionPass::Impl {
  struct ViewState {
    std::shared_ptr<Texture> directional_shadow_mask_texture {};
    ShaderVisibleIndex directional_shadow_mask_srv_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex directional_shadow_mask_uav_index {
      kInvalidShaderVisibleIndex
    };
    std::shared_ptr<Texture> shadow_mask_texture {};
    ShaderVisibleIndex shadow_mask_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex shadow_mask_uav_index { kInvalidShaderVisibleIndex };
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    bool has_current_output { false };
  };

  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;

  std::optional<VsmProjectionPassInput> input {};
  std::unordered_map<ViewId, ViewState> view_states {};

  std::shared_ptr<Buffer> projection_buffer {};
  std::shared_ptr<Buffer> projection_upload_buffer {};
  void* projection_upload_mapped_ptr { nullptr };
  std::uint32_t projection_capacity { 0U };
  ShaderVisibleIndex projection_buffer_srv_index { kInvalidShaderVisibleIndex };

  std::shared_ptr<Buffer> pass_constants_buffer {};
  void* pass_constants_mapped_ptr { nullptr };
  std::vector<ShaderVisibleIndex> pass_constants_indices {};
  std::uint32_t pass_constants_slot_count { 0U };
  std::uint32_t next_pass_constants_slot { 0U };

  const Texture* active_scene_depth_texture { nullptr };
  ShaderVisibleIndex scene_depth_srv_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex page_table_srv_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex shadow_texture_srv_index { kInvalidShaderVisibleIndex };

  ViewId active_view_id {};
  ViewState* active_view_state { nullptr };
  std::optional<std::uint32_t> active_dynamic_slice_index {};
  std::uint32_t active_projection_count { 0U };
  std::uint32_t active_directional_projection_count { 0U };
  std::uint32_t active_local_projection_count { 0U };
  bool resources_prepared { false };
  bool pipelines_ready { false };

  std::optional<graphics::ComputePipelineDesc> clear_shadow_mask_pso {};
  std::optional<graphics::ComputePipelineDesc> project_directional_pso {};
  std::optional<graphics::ComputePipelineDesc> project_local_pso {};

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl()
  {
    if (projection_upload_buffer && projection_upload_mapped_ptr != nullptr) {
      projection_upload_buffer->UnMap();
      projection_upload_mapped_ptr = nullptr;
    }
    if (pass_constants_buffer && pass_constants_mapped_ptr != nullptr) {
      pass_constants_buffer->UnMap();
      pass_constants_mapped_ptr = nullptr;
    }

    if (gfx == nullptr) {
      return;
    }

    for (auto& [view_id, view_state] : view_states) {
      static_cast<void>(view_id);
      UnregisterResourceIfPresent(
        *gfx, view_state.directional_shadow_mask_texture);
      UnregisterResourceIfPresent(*gfx, view_state.shadow_mask_texture);
    }
    UnregisterResourceIfPresent(*gfx, projection_upload_buffer);
    UnregisterResourceIfPresent(*gfx, projection_buffer);
    UnregisterResourceIfPresent(*gfx, pass_constants_buffer);
  }

  auto EnsureProjectionBuffers(const std::uint32_t required_capacity) -> void
  {
    if (required_capacity == 0U) {
      return;
    }
    if (projection_buffer && projection_upload_buffer
      && projection_capacity >= required_capacity) {
      return;
    }

    if (projection_upload_buffer && projection_upload_mapped_ptr != nullptr) {
      projection_upload_buffer->UnMap();
      projection_upload_mapped_ptr = nullptr;
    }
    UnregisterResourceIfPresent(*gfx, projection_upload_buffer);
    UnregisterResourceIfPresent(*gfx, projection_buffer);
    projection_buffer.reset();
    projection_upload_buffer.reset();
    projection_buffer_srv_index = kInvalidShaderVisibleIndex;

    const auto size_bytes = static_cast<std::uint64_t>(required_capacity)
      * sizeof(renderer::vsm::VsmPageRequestProjection);

    auto buffer_desc = BufferDesc {};
    buffer_desc.size_bytes = size_bytes;
    buffer_desc.usage = BufferUsage::kStorage;
    buffer_desc.memory = BufferMemory::kDeviceLocal;
    buffer_desc.debug_name = config->debug_name + ".ProjectionBuffer";
    projection_buffer = gfx->CreateBuffer(buffer_desc);
    CHECK_NOTNULL_F(
      projection_buffer.get(), "Failed to create VSM projection buffer");
    RegisterResourceIfNeeded(*gfx, projection_buffer);

    auto upload_desc = BufferDesc {};
    upload_desc.size_bytes = size_bytes;
    upload_desc.usage = BufferUsage::kNone;
    upload_desc.memory = BufferMemory::kUpload;
    upload_desc.debug_name = config->debug_name + ".ProjectionUpload";
    projection_upload_buffer = gfx->CreateBuffer(upload_desc);
    CHECK_NOTNULL_F(projection_upload_buffer.get(),
      "Failed to create VSM projection upload buffer");
    RegisterResourceIfNeeded(*gfx, projection_upload_buffer);
    projection_upload_mapped_ptr
      = projection_upload_buffer->Map(0U, upload_desc.size_bytes);
    CHECK_NOTNULL_F(projection_upload_mapped_ptr,
      "Failed to map VSM projection upload buffer");

    auto& allocator = gfx->GetDescriptorAllocator();
    auto srv_handle
      = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(srv_handle.IsValid(), "Failed to allocate VSM projection SRV");
    projection_buffer_srv_index = allocator.GetShaderVisibleIndex(srv_handle);

    auto srv_desc = MakeStructuredSrvDesc(
      size_bytes, sizeof(renderer::vsm::VsmPageRequestProjection));
    auto& registry = gfx->GetResourceRegistry();
    registry.RegisterView(*projection_buffer, std::move(srv_handle), srv_desc);

    projection_capacity = required_capacity;
  }

  auto EnsurePassConstantsBuffer(const std::uint32_t required_slot_count)
    -> void
  {
    CHECK_F(required_slot_count != 0U,
      "VSM projection pass requires at least one pass-constants slot");
    if (pass_constants_buffer
      && pass_constants_slot_count >= required_slot_count) {
      next_pass_constants_slot = 0U;
      return;
    }

    if (pass_constants_buffer && pass_constants_mapped_ptr != nullptr) {
      pass_constants_buffer->UnMap();
      pass_constants_mapped_ptr = nullptr;
    }
    UnregisterResourceIfPresent(*gfx, pass_constants_buffer);
    pass_constants_buffer.reset();
    pass_constants_indices.clear();

    auto desc = BufferDesc {};
    desc.size_bytes
      = static_cast<std::uint64_t>(required_slot_count) * kPassConstantsStride;
    desc.usage = BufferUsage::kConstant;
    desc.memory = BufferMemory::kUpload;
    desc.debug_name = config->debug_name + ".PassConstants";
    pass_constants_buffer = gfx->CreateBuffer(desc);
    CHECK_NOTNULL_F(pass_constants_buffer.get(),
      "Failed to create VSM projection pass-constants buffer");
    RegisterResourceIfNeeded(*gfx, pass_constants_buffer);
    pass_constants_mapped_ptr = pass_constants_buffer->Map(0U, desc.size_bytes);
    CHECK_NOTNULL_F(pass_constants_mapped_ptr,
      "Failed to map VSM projection pass-constants buffer");

    auto& allocator = gfx->GetDescriptorAllocator();
    auto& registry = gfx->GetResourceRegistry();
    pass_constants_indices.reserve(required_slot_count);
    for (std::uint32_t i = 0U; i < required_slot_count; ++i) {
      auto handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
      CHECK_F(handle.IsValid(),
        "Failed to allocate VSM projection pass-constants CBV");
      const auto shader_visible_index = allocator.GetShaderVisibleIndex(handle);
      auto cbv_desc = graphics::BufferViewDescription {};
      cbv_desc.view_type = ResourceViewType::kConstantBuffer;
      cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      cbv_desc.range = { static_cast<std::uint64_t>(i) * kPassConstantsStride,
        kPassConstantsStride };
      cbv_desc.stride = 0U;
      auto view = registry.RegisterView(
        *pass_constants_buffer, std::move(handle), cbv_desc);
      CHECK_F(view->IsValid(),
        "Failed to register VSM projection pass-constants CBV");
      pass_constants_indices.push_back(shader_visible_index);
    }

    pass_constants_slot_count = required_slot_count;
    next_pass_constants_slot = 0U;
  }

  auto EnsureViewState(const ViewId view_id, const std::uint32_t width,
    const std::uint32_t height) -> ViewState&
  {
    auto& state = view_states[view_id];
    if (state.directional_shadow_mask_texture && state.shadow_mask_texture
      && state.width == width && state.height == height) {
      return state;
    }

    UnregisterResourceIfPresent(*gfx, state.directional_shadow_mask_texture);
    UnregisterResourceIfPresent(*gfx, state.shadow_mask_texture);
    state = {};

    auto make_mask_texture = [&](const std::string& suffix) {
      auto desc = graphics::TextureDesc {};
      desc.width = width;
      desc.height = height;
      desc.format = oxygen::Format::kR32Float;
      desc.texture_type = oxygen::TextureType::kTexture2D;
      desc.is_shader_resource = true;
      desc.is_uav = true;
      desc.initial_state = ResourceStates::kCommon;
      desc.debug_name
        = config->debug_name + suffix + ".View" + std::to_string(view_id.get());
      auto texture = gfx->CreateTexture(desc);
      CHECK_NOTNULL_F(
        texture.get(), "Failed to create VSM projection mask texture");
      RegisterResourceIfNeeded(*gfx, texture);
      return texture;
    };

    state.directional_shadow_mask_texture
      = make_mask_texture(".DirectionalShadowMask");
    state.shadow_mask_texture = make_mask_texture(".ShadowMask");

    state.directional_shadow_mask_srv_index = EnsureTextureViewIndex(
      *state.directional_shadow_mask_texture, ShadowMaskSrvDesc());
    state.directional_shadow_mask_uav_index = EnsureTextureViewIndex(
      *state.directional_shadow_mask_texture, ShadowMaskUavDesc());
    state.shadow_mask_srv_index
      = EnsureTextureViewIndex(*state.shadow_mask_texture, ShadowMaskSrvDesc());
    state.shadow_mask_uav_index
      = EnsureTextureViewIndex(*state.shadow_mask_texture, ShadowMaskUavDesc());
    state.width = width;
    state.height = height;
    return state;
  }

  auto EnsureBufferViewIndex(Buffer& buffer,
    const graphics::BufferViewDescription& desc) -> ShaderVisibleIndex
  {
    auto& registry = gfx->GetResourceRegistry();
    if (!registry.Contains(buffer)) {
      registry.Register(std::shared_ptr<Buffer>(&buffer, [](Buffer*) { }));
    }
    if (const auto existing = registry.FindShaderVisibleIndex(buffer, desc);
      existing.has_value()) {
      return *existing;
    }

    auto& allocator = gfx->GetDescriptorAllocator();
    auto handle = allocator.Allocate(desc.view_type, desc.visibility);
    CHECK_F(handle.IsValid(), "Failed to allocate VSM projection buffer view");
    const auto shader_visible_index = allocator.GetShaderVisibleIndex(handle);
    auto view = registry.RegisterView(buffer, std::move(handle), desc);
    CHECK_F(view->IsValid(), "Failed to register VSM projection buffer view");
    return shader_visible_index;
  }

  auto EnsureTextureViewIndex(Texture& texture,
    const graphics::TextureViewDescription& desc) -> ShaderVisibleIndex
  {
    auto& registry = gfx->GetResourceRegistry();
    if (!registry.Contains(texture)) {
      registry.Register(std::shared_ptr<Texture>(&texture, [](Texture*) { }));
    }
    if (const auto existing = registry.FindShaderVisibleIndex(texture, desc);
      existing.has_value()) {
      return *existing;
    }

    auto& allocator = gfx->GetDescriptorAllocator();
    auto handle = allocator.Allocate(desc.view_type, desc.visibility);
    CHECK_F(handle.IsValid(), "Failed to allocate VSM projection texture view");
    const auto shader_visible_index = allocator.GetShaderVisibleIndex(handle);
    auto view = registry.RegisterView(texture, std::move(handle), desc);
    CHECK_F(view->IsValid(), "Failed to register VSM projection texture view");
    return shader_visible_index;
  }

  template <typename T>
  auto WritePassConstants(const T& constants) -> ShaderVisibleIndex
  {
    CHECK_NOTNULL_F(pass_constants_mapped_ptr,
      "VSM projection pass constants must be mapped before writing");
    CHECK_F(next_pass_constants_slot < pass_constants_slot_count,
      "VSM projection pass exhausted its pass-constants slots");
    std::memcpy(static_cast<std::byte*>(pass_constants_mapped_ptr)
        + static_cast<std::size_t>(next_pass_constants_slot)
          * kPassConstantsStride,
      &constants, sizeof(constants));
    return pass_constants_indices[next_pass_constants_slot++];
  }
};

VsmProjectionPass::VsmProjectionPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "VsmProjectionPass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
  CHECK_NOTNULL_F(
    impl_->config.get(), "VsmProjectionPass requires a non-null config");
  ValidateConfig();
}

VsmProjectionPass::~VsmProjectionPass() = default;

auto VsmProjectionPass::SetInput(VsmProjectionPassInput input) -> void
{
  impl_->input = std::move(input);
}

auto VsmProjectionPass::ResetInput() noexcept -> void
{
  impl_->input.reset();
  impl_->resources_prepared = false;
}

auto VsmProjectionPass::GetCurrentOutput(const ViewId view_id) const
  -> ViewOutput
{
  const auto it = impl_->view_states.find(view_id);
  if (it == impl_->view_states.end() || !it->second.shadow_mask_texture) {
    return {};
  }

  return ViewOutput {
    .directional_shadow_mask_texture
    = it->second.directional_shadow_mask_texture,
    .directional_shadow_mask_srv_index
    = it->second.directional_shadow_mask_srv_index,
    .shadow_mask_texture = it->second.shadow_mask_texture,
    .shadow_mask_srv_index = it->second.shadow_mask_srv_index,
    .width = it->second.width,
    .height = it->second.height,
    .available = it->second.has_current_output,
  };
}

auto VsmProjectionPass::ValidateConfig() -> void
{
  if (!impl_->gfx) {
    throw std::runtime_error("VsmProjectionPass requires Graphics");
  }
}

auto VsmProjectionPass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  impl_->resources_prepared = false;
  impl_->active_view_state = nullptr;
  impl_->active_projection_count = 0U;
  impl_->active_directional_projection_count = 0U;
  impl_->active_local_projection_count = 0U;
  impl_->active_dynamic_slice_index.reset();
  impl_->active_scene_depth_texture = nullptr;
  impl_->next_pass_constants_slot = 0U;

  if (!impl_->input.has_value()) {
    DLOG_F(2, "VSM projection pass skipped because no input is set");
    co_return;
  }

  if (!Context().current_view.resolved_view) {
    LOG_F(WARNING,
      "VSM projection pass skipped because the resolved view is unavailable");
    co_return;
  }

  const auto& input = *impl_->input;
  if (!input.frame.is_ready) {
    LOG_F(
      WARNING, "VSM projection pass skipped because frame input is not ready");
    co_return;
  }
  if (!input.physical_pool.is_available
    || !input.physical_pool.shadow_texture) {
    LOG_F(WARNING,
      "VSM projection pass skipped because the physical shadow pool is "
      "unavailable");
    co_return;
  }
  const auto* scene_depth_texture = input.scene_depth_output.is_complete
      && input.scene_depth_output.depth_texture != nullptr
    ? input.scene_depth_output.depth_texture
    : input.scene_depth_texture.get();
  if (scene_depth_texture == nullptr) {
    LOG_F(WARNING,
      "VSM projection pass skipped because the scene depth texture is "
      "unavailable");
    co_return;
  }
  if (!input.frame.page_table_buffer) {
    LOG_F(WARNING,
      "VSM projection pass skipped because the page-table buffer is "
      "unavailable");
    co_return;
  }

  if (input.scene_depth_output.depth_texture != nullptr
    && input.scene_depth_texture
    && input.scene_depth_output.depth_texture
      != input.scene_depth_texture.get()) {
    DLOG_F(WARNING,
      "VSM projection pass received mismatched scene depth inputs: "
      "DepthPrePassOutput depth={} raw fallback depth={}",
      fmt::ptr(input.scene_depth_output.depth_texture),
      fmt::ptr(input.scene_depth_texture.get()));
  }

  const auto dynamic_slice_index = FindDynamicSliceIndex(input.physical_pool);
  if (!dynamic_slice_index.has_value()) {
    LOG_F(ERROR, "VSM projection pass requires a dynamic shadow slice");
    co_return;
  }

  const auto depth_desc = scene_depth_texture->GetDescriptor();
  if (depth_desc.width == 0U || depth_desc.height == 0U) {
    LOG_F(
      ERROR, "VSM projection pass requires a non-empty scene depth texture");
    co_return;
  }

  impl_->EnsurePassConstantsBuffer(3U);
  auto& view_state = impl_->EnsureViewState(
    Context().current_view.view_id, depth_desc.width, depth_desc.height);

  impl_->active_view_id = Context().current_view.view_id;
  impl_->active_view_state = &view_state;
  impl_->active_dynamic_slice_index = dynamic_slice_index;
  impl_->active_projection_count = static_cast<std::uint32_t>(
    input.frame.snapshot.projection_records.size());
  for (const auto& projection_record :
    input.frame.snapshot.projection_records) {
    const auto light_type = static_cast<VsmProjectionLightType>(
      projection_record.projection.light_type);
    if (light_type == VsmProjectionLightType::kDirectional) {
      ++impl_->active_directional_projection_count;
    } else {
      ++impl_->active_local_projection_count;
    }
  }

  if (impl_->active_projection_count > 0U) {
    impl_->EnsureProjectionBuffers(impl_->active_projection_count);
    std::memcpy(impl_->projection_upload_mapped_ptr,
      input.frame.snapshot.projection_records.data(),
      static_cast<std::size_t>(impl_->active_projection_count)
        * sizeof(renderer::vsm::VsmPageRequestProjection));
  }

  impl_->active_scene_depth_texture = scene_depth_texture;
  if (input.scene_depth_output.is_complete
    && input.scene_depth_output.depth_texture == scene_depth_texture
    && input.scene_depth_output.has_canonical_srv) {
    impl_->scene_depth_srv_index = input.scene_depth_output.canonical_srv_index;
  } else {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    impl_->scene_depth_srv_index = impl_->EnsureTextureViewIndex(
      const_cast<Texture&>(*scene_depth_texture),
      DepthSrvDesc(*scene_depth_texture));
  }
  impl_->shadow_texture_srv_index = impl_->EnsureTextureViewIndex(
    *std::const_pointer_cast<Texture>(input.physical_pool.shadow_texture),
    WholeShadowTextureSrvDesc(input.physical_pool.depth_format));
  impl_->page_table_srv_index = impl_->EnsureBufferViewIndex(
    *std::const_pointer_cast<Buffer>(input.frame.page_table_buffer),
    MakeStructuredSrvDesc(
      input.frame.page_table_buffer->GetDescriptor().size_bytes,
      sizeof(renderer::vsm::VsmShaderPageTableEntry)));

  recorder.BeginTrackingResourceState(
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    *const_cast<Texture*>(scene_depth_texture),
    DepthTextureInitialState(*scene_depth_texture), true);
  recorder.BeginTrackingResourceState(
    *std::const_pointer_cast<Texture>(input.physical_pool.shadow_texture),
    ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *std::const_pointer_cast<Buffer>(input.frame.page_table_buffer),
    ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *view_state.directional_shadow_mask_texture,
    view_state.has_current_output ? ResourceStates::kShaderResource
                                  : ResourceStates::kCommon,
    true);
  recorder.BeginTrackingResourceState(*view_state.shadow_mask_texture,
    view_state.has_current_output ? ResourceStates::kShaderResource
                                  : ResourceStates::kCommon,
    true);
  if (impl_->active_projection_count > 0U) {
    recorder.BeginTrackingResourceState(
      *impl_->projection_upload_buffer, ResourceStates::kGenericRead, true);
    recorder.BeginTrackingResourceState(
      *impl_->projection_buffer, ResourceStates::kGenericRead, true);
  }

  impl_->resources_prepared = true;

  DLOG_F(2,
    "prepared VSM projection pass generation={} projections={} directional={} "
    "local={} depth={}x{} view={} canonical_scene_depth_srv={}",
    input.frame.snapshot.frame_generation, impl_->active_projection_count,
    impl_->active_directional_projection_count,
    impl_->active_local_projection_count, depth_desc.width, depth_desc.height,
    impl_->active_view_id.get(),
    input.scene_depth_output.is_complete
      && input.scene_depth_output.depth_texture == scene_depth_texture
      && input.scene_depth_output.has_canonical_srv);

  co_return;
}

auto VsmProjectionPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  if (!impl_->resources_prepared || impl_->active_view_state == nullptr
    || !impl_->active_dynamic_slice_index.has_value()
    || !impl_->clear_shadow_mask_pso || !impl_->project_directional_pso
    || !impl_->project_local_pso) {
    DLOG_F(2, "VSM projection pass skipped execute");
    co_return;
  }

  const auto& input = *impl_->input;
  const auto output_width = impl_->active_view_state->width;
  const auto output_height = impl_->active_view_state->height;
  const auto dispatch_x = MakeDispatchGroups(output_width);
  const auto dispatch_y = MakeDispatchGroups(output_height);

  auto clear_constants = VsmProjectionClearPassConstants {
    .directional_shadow_mask_uav_index
    = impl_->active_view_state->directional_shadow_mask_uav_index,
    .shadow_mask_uav_index = impl_->active_view_state->shadow_mask_uav_index,
    .output_width = output_width,
    .output_height = output_height,
  };
  recorder.RequireResourceState(
    *impl_->active_view_state->directional_shadow_mask_texture,
    ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*impl_->active_view_state->shadow_mask_texture,
    ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  BindComputeStage(recorder, *impl_->clear_shadow_mask_pso,
    impl_->WritePassConstants(clear_constants), Context());
  recorder.Dispatch(dispatch_x, dispatch_y, 1U);

  if (impl_->active_projection_count > 0U) {
    recorder.RequireResourceState(
      *impl_->projection_upload_buffer, ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *impl_->projection_buffer, ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    recorder.CopyBuffer(*impl_->projection_buffer, 0U,
      *impl_->projection_upload_buffer, 0U,
      static_cast<std::uint64_t>(impl_->active_projection_count)
        * sizeof(renderer::vsm::VsmPageRequestProjection));
    recorder.RequireResourceState(
      *impl_->projection_buffer, ResourceStates::kGenericRead);
  }

  auto projection_constants = VsmProjectionCompositePassConstants {
    .scene_depth_srv_index = impl_->scene_depth_srv_index,
    .directional_shadow_mask_uav_index
    = impl_->active_view_state->directional_shadow_mask_uav_index,
    .shadow_mask_uav_index = impl_->active_view_state->shadow_mask_uav_index,
    .projection_buffer_srv_index = impl_->projection_buffer_srv_index,
    .page_table_buffer_srv_index = impl_->page_table_srv_index,
    .shadow_texture_srv_index = impl_->shadow_texture_srv_index,
    .page_table_entry_count
    = static_cast<std::uint32_t>(input.frame.snapshot.page_table.size()),
    .projection_count = impl_->active_projection_count,
    .output_width = output_width,
    .output_height = output_height,
    .page_size_texels = input.physical_pool.page_size_texels,
    .tiles_per_axis = input.physical_pool.tiles_per_axis,
    .dynamic_slice_index = *impl_->active_dynamic_slice_index,
    .inverse_view_projection
    = Context().current_view.resolved_view->InverseViewProjection(),
    .main_view_matrix = Context().current_view.resolved_view->ViewMatrix(),
  };

  if (impl_->active_directional_projection_count > 0U) {
    recorder.RequireResourceState(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
      *const_cast<Texture*>(impl_->active_scene_depth_texture),
      ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *std::const_pointer_cast<Texture>(input.physical_pool.shadow_texture),
      ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *std::const_pointer_cast<Buffer>(input.frame.page_table_buffer),
      ResourceStates::kGenericRead);
    recorder.RequireResourceState(
      *impl_->active_view_state->directional_shadow_mask_texture,
      ResourceStates::kUnorderedAccess);
    recorder.RequireResourceState(
      *impl_->active_view_state->shadow_mask_texture,
      ResourceStates::kUnorderedAccess);
    if (impl_->active_projection_count > 0U) {
      recorder.RequireResourceState(
        *impl_->projection_buffer, ResourceStates::kGenericRead);
    }
    recorder.FlushBarriers();
    BindComputeStage(recorder, *impl_->project_directional_pso,
      impl_->WritePassConstants(projection_constants), Context());
    recorder.Dispatch(dispatch_x, dispatch_y, 1U);
  }

  if (impl_->active_local_projection_count > 0U) {
    recorder.RequireResourceState(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
      *const_cast<Texture*>(impl_->active_scene_depth_texture),
      ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *std::const_pointer_cast<Texture>(input.physical_pool.shadow_texture),
      ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *std::const_pointer_cast<Buffer>(input.frame.page_table_buffer),
      ResourceStates::kGenericRead);
    recorder.RequireResourceState(
      *impl_->active_view_state->shadow_mask_texture,
      ResourceStates::kUnorderedAccess);
    if (impl_->active_projection_count > 0U) {
      recorder.RequireResourceState(
        *impl_->projection_buffer, ResourceStates::kGenericRead);
    }
    recorder.FlushBarriers();
    BindComputeStage(recorder, *impl_->project_local_pso,
      impl_->WritePassConstants(projection_constants), Context());
    recorder.Dispatch(dispatch_x, dispatch_y, 1U);
  }

  recorder.RequireResourceStateFinal(
    *impl_->active_view_state->directional_shadow_mask_texture,
    ResourceStates::kShaderResource);
  recorder.RequireResourceStateFinal(
    *impl_->active_view_state->shadow_mask_texture,
    ResourceStates::kShaderResource);
  impl_->active_view_state->has_current_output = true;

  DLOG_F(2,
    "executed VSM projection pass generation={} projections={} directional={} "
    "local={} view={}",
    input.frame.snapshot.frame_generation, impl_->active_projection_count,
    impl_->active_directional_projection_count,
    impl_->active_local_projection_count, impl_->active_view_id.get());

  co_return;
}

auto VsmProjectionPass::CreatePipelineStateDesc() -> ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();
  const auto build_pso = [&](const char* source_path, const char* entry_point,
                           const char* debug_name) {
    graphics::ShaderRequest shader_request {
      .stage = oxygen::ShaderType::kCompute,
      .source_path = source_path,
      .entry_point = entry_point,
    };
    return ComputePipelineDesc::Builder()
      .SetComputeShader(std::move(shader_request))
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        generated_bindings.data(), generated_bindings.size()))
      .SetDebugName(debug_name)
      .Build();
  };

  impl_->clear_shadow_mask_pso
    = build_pso("Renderer/Vsm/VsmDirectionalProjection.hlsl",
      "CS_ClearShadowMask", "VsmProjectionClear_PSO");
  impl_->project_directional_pso
    = build_pso("Renderer/Vsm/VsmDirectionalProjection.hlsl",
      "CS_ProjectDirectional", "VsmProjectionDirectional_PSO");
  impl_->project_local_pso
    = build_pso("Renderer/Vsm/VsmLocalLightProjectionPerLight.hlsl",
      "CS_ProjectLocalLights", "VsmProjectionLocalPerLight_PSO");
  impl_->pipelines_ready = true;
  return *impl_->clear_shadow_mask_pso;
}

auto VsmProjectionPass::NeedRebuildPipelineState() const -> bool
{
  return !impl_->pipelines_ready;
}

} // namespace oxygen::engine
