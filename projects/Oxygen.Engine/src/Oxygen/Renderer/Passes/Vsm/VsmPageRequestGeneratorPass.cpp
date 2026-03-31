//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/Vsm/VsmPageRequestGeneratorPass.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

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
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/RenderContext.h>

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::ShaderVisibleIndex;
using oxygen::engine::VsmPageRequestGeneratorPass;
using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::ComputePipelineDesc;
using oxygen::graphics::ResourceViewType;
using oxygen::renderer::vsm::VsmPageRequestProjection;
using oxygen::renderer::vsm::VsmShaderPageRequestFlags;

namespace oxygen::engine {

namespace {

  constexpr std::uint32_t kThreadGroupSize = 8U;

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

  struct alignas(packing::kShaderDataFieldAlignment)
    VsmPageRequestGeneratorPassConstants {
    ShaderVisibleIndex depth_texture_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex projection_buffer_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex page_request_flags_uav_index {
      kInvalidShaderVisibleIndex
    };
    ShaderVisibleIndex cluster_grid_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex light_index_list_index { kInvalidShaderVisibleIndex };
    glm::uvec2 screen_dimensions { 0U, 0U };
    std::uint32_t projection_count { 0U };
    std::uint32_t virtual_page_count { 0U };
    // HLSL cbuffer packing aligns the matrix to the next 16-byte register.
    std::uint32_t _pad_after_counts[3] { 0U, 0U, 0U };
    glm::mat4 inverse_view_projection { 1.0F };
    std::uint32_t cluster_dim_x { 0U };
    std::uint32_t cluster_dim_y { 0U };
    std::uint32_t tile_size_px { 16U };
    std::uint32_t enable_light_grid_pruning { 0U };
  };
  static_assert(
    offsetof(VsmPageRequestGeneratorPassConstants, inverse_view_projection)
    == 48U);
  static_assert(sizeof(VsmPageRequestGeneratorPassConstants) == 128U);
  static_assert(sizeof(VsmPageRequestGeneratorPassConstants)
      % packing::kShaderDataFieldAlignment
    == 0U);

} // namespace

struct VsmPageRequestGeneratorPass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;

  std::vector<VsmPageRequestProjection> frame_projections {};
  std::uint32_t virtual_page_count { 0U };
  bool resources_prepared { false };

  std::shared_ptr<Buffer> projection_buffer;
  std::shared_ptr<Buffer> projection_upload_buffer;
  std::shared_ptr<Buffer> request_flags_buffer;
  std::shared_ptr<Buffer> request_clear_buffer;
  std::shared_ptr<Buffer> pass_constants_buffer;
  void* projection_mapped_ptr { nullptr };
  void* request_clear_mapped_ptr { nullptr };
  void* pass_constants_mapped_ptr { nullptr };
  std::uint32_t projection_capacity { 0U };
  std::uint32_t request_capacity { 0U };

  ShaderVisibleIndex projection_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex request_flags_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex request_flags_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex pass_constants_index { kInvalidShaderVisibleIndex };
  DepthPrePassOutput depth_output {};

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl()
  {
    if (projection_upload_buffer && projection_mapped_ptr != nullptr) {
      projection_upload_buffer->UnMap();
      projection_mapped_ptr = nullptr;
    }
    if (request_clear_buffer && request_clear_mapped_ptr != nullptr) {
      request_clear_buffer->UnMap();
      request_clear_mapped_ptr = nullptr;
    }
    if (pass_constants_buffer && pass_constants_mapped_ptr != nullptr) {
      pass_constants_buffer->UnMap();
      pass_constants_mapped_ptr = nullptr;
    }

    if (gfx != nullptr) {
      UnregisterResourceIfPresent(*gfx, pass_constants_buffer);
      UnregisterResourceIfPresent(*gfx, request_clear_buffer);
      UnregisterResourceIfPresent(*gfx, request_flags_buffer);
      UnregisterResourceIfPresent(*gfx, projection_upload_buffer);
      UnregisterResourceIfPresent(*gfx, projection_buffer);
    }
  }

  auto EnsureProjectionBuffer(const std::uint32_t required_capacity) -> void
  {
    if (required_capacity == 0U) {
      return;
    }
    if (projection_buffer && projection_upload_buffer
      && projection_capacity >= required_capacity) {
      return;
    }

    if (projection_upload_buffer && projection_mapped_ptr != nullptr) {
      projection_upload_buffer->UnMap();
      projection_mapped_ptr = nullptr;
    }
    UnregisterResourceIfPresent(*gfx, projection_upload_buffer);
    UnregisterResourceIfPresent(*gfx, projection_buffer);
    projection_upload_buffer.reset();
    projection_buffer.reset();
    projection_srv = kInvalidShaderVisibleIndex;

    const auto buffer_size = static_cast<std::uint64_t>(required_capacity)
      * sizeof(VsmPageRequestProjection);
    const graphics::BufferDesc desc {
      .size_bytes = buffer_size,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = config->debug_name + "_ProjectionBuffer",
    };
    projection_buffer = gfx->CreateBuffer(desc);
    CHECK_NOTNULL_F(
      projection_buffer.get(), "Failed to create VSM projection buffer");
    projection_buffer->SetName(desc.debug_name);
    RegisterResourceIfNeeded(*gfx, projection_buffer);

    const graphics::BufferDesc upload_desc {
      .size_bytes = buffer_size,
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = config->debug_name + "_ProjectionUpload",
    };
    projection_upload_buffer = gfx->CreateBuffer(upload_desc);
    CHECK_NOTNULL_F(projection_upload_buffer.get(),
      "Failed to create VSM projection upload buffer");
    projection_upload_buffer->SetName(upload_desc.debug_name);
    RegisterResourceIfNeeded(*gfx, projection_upload_buffer);

    projection_mapped_ptr
      = projection_upload_buffer->Map(0U, upload_desc.size_bytes);
    CHECK_NOTNULL_F(
      projection_mapped_ptr, "Failed to map VSM projection buffer");

    auto& allocator = gfx->GetDescriptorAllocator();
    auto srv_handle
      = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(srv_handle.IsValid(), "Failed to allocate VSM projection SRV");
    projection_srv = allocator.GetShaderVisibleIndex(srv_handle);

    graphics::BufferViewDescription srv_desc;
    srv_desc.view_type = ResourceViewType::kStructuredBuffer_SRV;
    srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    srv_desc.range = { 0U, desc.size_bytes };
    srv_desc.stride = sizeof(VsmPageRequestProjection);
    auto& registry = gfx->GetResourceRegistry();
    registry.RegisterView(*projection_buffer, std::move(srv_handle), srv_desc);
    projection_capacity = required_capacity;
  }

  auto EnsureRequestBuffers(const std::uint32_t required_capacity) -> void
  {
    if (required_capacity == 0U) {
      return;
    }
    if (request_flags_buffer && request_clear_buffer
      && request_capacity >= required_capacity) {
      return;
    }

    if (request_clear_buffer && request_clear_mapped_ptr != nullptr) {
      request_clear_buffer->UnMap();
      request_clear_mapped_ptr = nullptr;
    }
    UnregisterResourceIfPresent(*gfx, request_clear_buffer);
    UnregisterResourceIfPresent(*gfx, request_flags_buffer);
    request_clear_buffer.reset();
    request_flags_buffer.reset();
    request_flags_uav = kInvalidShaderVisibleIndex;
    request_flags_srv = kInvalidShaderVisibleIndex;

    const auto buffer_size = static_cast<std::uint64_t>(required_capacity)
      * sizeof(VsmShaderPageRequestFlags);
    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    const graphics::BufferDesc flags_desc {
      .size_bytes = buffer_size,
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = config->debug_name + "_RequestFlags",
    };
    request_flags_buffer = gfx->CreateBuffer(flags_desc);
    CHECK_NOTNULL_F(
      request_flags_buffer.get(), "Failed to create VSM request flag buffer");
    request_flags_buffer->SetName(flags_desc.debug_name);
    RegisterResourceIfNeeded(*gfx, request_flags_buffer);

    const graphics::BufferDesc clear_desc {
      .size_bytes = buffer_size,
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = config->debug_name + "_RequestFlagsClear",
    };
    request_clear_buffer = gfx->CreateBuffer(clear_desc);
    CHECK_NOTNULL_F(
      request_clear_buffer.get(), "Failed to create VSM request clear buffer");
    request_clear_buffer->SetName(clear_desc.debug_name);
    RegisterResourceIfNeeded(*gfx, request_clear_buffer);

    request_clear_mapped_ptr
      = request_clear_buffer->Map(0U, clear_desc.size_bytes);
    CHECK_NOTNULL_F(
      request_clear_mapped_ptr, "Failed to map VSM request clear buffer");
    std::memset(request_clear_mapped_ptr, 0, clear_desc.size_bytes);

    auto uav_handle
      = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(uav_handle.IsValid(), "Failed to allocate VSM request flag UAV");
    request_flags_uav = allocator.GetShaderVisibleIndex(uav_handle);

    graphics::BufferViewDescription uav_desc;
    uav_desc.view_type = ResourceViewType::kStructuredBuffer_UAV;
    uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    uav_desc.range = { 0U, flags_desc.size_bytes };
    uav_desc.stride = sizeof(VsmShaderPageRequestFlags);
    registry.RegisterView(
      *request_flags_buffer, std::move(uav_handle), uav_desc);

    auto srv_handle
      = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(srv_handle.IsValid(), "Failed to allocate VSM request flag SRV");
    request_flags_srv = allocator.GetShaderVisibleIndex(srv_handle);

    graphics::BufferViewDescription srv_desc;
    srv_desc.view_type = ResourceViewType::kStructuredBuffer_SRV;
    srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    srv_desc.range = { 0U, flags_desc.size_bytes };
    srv_desc.stride = sizeof(VsmShaderPageRequestFlags);
    registry.RegisterView(
      *request_flags_buffer, std::move(srv_handle), srv_desc);
    request_capacity = required_capacity;
  }

  auto EnsurePassConstantsBuffer() -> void
  {
    if (pass_constants_buffer) {
      return;
    }

    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    const graphics::BufferDesc desc {
      .size_bytes = 256U,
      .usage = graphics::BufferUsage::kConstant,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = config->debug_name + "_PassConstants",
    };
    pass_constants_buffer = gfx->CreateBuffer(desc);
    CHECK_NOTNULL_F(
      pass_constants_buffer.get(), "Failed to create VSM request constants");
    pass_constants_buffer->SetName(desc.debug_name);
    RegisterResourceIfNeeded(*gfx, pass_constants_buffer);

    pass_constants_mapped_ptr = pass_constants_buffer->Map(0U, desc.size_bytes);
    CHECK_NOTNULL_F(
      pass_constants_mapped_ptr, "Failed to map VSM request constants");

    graphics::BufferViewDescription cbv_desc;
    cbv_desc.view_type = ResourceViewType::kConstantBuffer;
    cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_desc.range = { 0U, desc.size_bytes };

    auto cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(
      cbv_handle.IsValid(), "Failed to allocate VSM request constants CBV");
    pass_constants_index = allocator.GetShaderVisibleIndex(cbv_handle);
    registry.RegisterView(
      *pass_constants_buffer, std::move(cbv_handle), cbv_desc);
  }
};

VsmPageRequestGeneratorPass::VsmPageRequestGeneratorPass(
  observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config->debug_name)
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
}

VsmPageRequestGeneratorPass::~VsmPageRequestGeneratorPass() = default;

auto VsmPageRequestGeneratorPass::SetFrameInputs(
  std::vector<VsmPageRequestProjection> projections,
  const std::uint32_t virtual_page_count) -> void
{
  CHECK_F(virtual_page_count <= impl_->config->max_virtual_page_count,
    "VSM request page count {} exceeds configured capacity {}",
    virtual_page_count, impl_->config->max_virtual_page_count);
  CHECK_F(projections.size() <= impl_->config->max_projection_count,
    "VSM projection count {} exceeds configured capacity {}",
    projections.size(), impl_->config->max_projection_count);

  if (virtual_page_count == 0U && !projections.empty()) {
    LOG_F(WARNING,
      "VSM page-request pass received {} projections but zero virtual pages",
      projections.size());
  }

  impl_->frame_projections = std::move(projections);
  impl_->virtual_page_count = virtual_page_count;
}

auto VsmPageRequestGeneratorPass::ResetFrameInputs() noexcept -> void
{
  impl_->frame_projections.clear();
  impl_->virtual_page_count = 0U;
}

auto VsmPageRequestGeneratorPass::GetProjectionCount() const noexcept
  -> std::uint32_t
{
  return static_cast<std::uint32_t>(impl_->frame_projections.size());
}

auto VsmPageRequestGeneratorPass::GetVirtualPageCount() const noexcept
  -> std::uint32_t
{
  return impl_->virtual_page_count;
}

auto VsmPageRequestGeneratorPass::GetProjectionSrvIndex() const noexcept
  -> ShaderVisibleIndex
{
  return impl_->projection_srv;
}

auto VsmPageRequestGeneratorPass::GetPageRequestFlagsSrvIndex() const noexcept
  -> ShaderVisibleIndex
{
  return impl_->request_flags_srv;
}

auto VsmPageRequestGeneratorPass::GetProjectionBuffer() const noexcept
  -> std::shared_ptr<const graphics::Buffer>
{
  return impl_->projection_buffer;
}

auto VsmPageRequestGeneratorPass::GetPageRequestFlagsBuffer() const noexcept
  -> std::shared_ptr<const graphics::Buffer>
{
  return impl_->request_flags_buffer;
}

auto VsmPageRequestGeneratorPass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  impl_->resources_prepared = false;
  impl_->depth_output = {};

  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  if (depth_pass == nullptr) {
    LOG_F(WARNING,
      "VSM page-request pass skipped because DepthPrePass is unavailable");
    co_return;
  }
  const auto* resolved_view = Context().current_view.resolved_view.get();
  if (resolved_view == nullptr) {
    LOG_F(WARNING,
      "VSM page-request pass skipped because resolved view is unavailable");
    co_return;
  }

  impl_->EnsureProjectionBuffer(
    std::max(impl_->config->max_projection_count, GetProjectionCount()));
  impl_->EnsureRequestBuffers(
    std::max(impl_->config->max_virtual_page_count, impl_->virtual_page_count));
  impl_->EnsurePassConstantsBuffer();

  if (!impl_->frame_projections.empty()) {
    std::memcpy(impl_->projection_mapped_ptr, impl_->frame_projections.data(),
      impl_->frame_projections.size() * sizeof(VsmPageRequestProjection));
  }

  impl_->depth_output = depth_pass->GetOutput();
  if (!impl_->depth_output.is_complete
    || impl_->depth_output.depth_texture == nullptr
    || !impl_->depth_output.has_canonical_srv) {
    LOG_F(ERROR,
      "VSM page-request pass failed because DepthPrePass output is incomplete");
    co_return;
  }
  const auto& depth_texture = *impl_->depth_output.depth_texture;
  const auto depth_srv = impl_->depth_output.canonical_srv_index;

  ShaderVisibleIndex cluster_grid_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex light_index_list_index { kInvalidShaderVisibleIndex };
  std::shared_ptr<const graphics::Buffer> cluster_grid_buffer;
  std::shared_ptr<const graphics::Buffer> light_index_list_buffer;
  std::uint32_t cluster_dim_x = 0U;
  std::uint32_t cluster_dim_y = 0U;
  std::uint32_t tile_size_px = 16U;
  if (const auto* light_culling = Context().GetPass<LightCullingPass>();
    light_culling != nullptr) {
    cluster_grid_index = light_culling->GetClusterGridSrvIndex();
    light_index_list_index = light_culling->GetLightIndexListSrvIndex();
    cluster_grid_buffer = light_culling->GetClusterGridBuffer();
    light_index_list_buffer = light_culling->GetLightIndexListBuffer();
    const auto dims = light_culling->GetGridDimensions();
    cluster_dim_x = dims.x;
    cluster_dim_y = dims.y;
    tile_size_px = light_culling->GetClusterConfig().tile_size_px;
  }

  const auto constants = VsmPageRequestGeneratorPassConstants {
    .depth_texture_index = depth_srv,
    .projection_buffer_index = impl_->projection_srv,
    .page_request_flags_uav_index = impl_->request_flags_uav,
    .cluster_grid_index = cluster_grid_index,
    .light_index_list_index = light_index_list_index,
    .screen_dimensions
    = { impl_->depth_output.width, impl_->depth_output.height },
    .projection_count = GetProjectionCount(),
    .virtual_page_count = impl_->virtual_page_count,
    .inverse_view_projection = resolved_view->InverseViewProjection(),
    .cluster_dim_x = cluster_dim_x,
    .cluster_dim_y = cluster_dim_y,
    .tile_size_px = tile_size_px,
    .enable_light_grid_pruning
    = impl_->config->enable_light_grid_pruning ? 1U : 0U,
  };
  std::memcpy(impl_->pass_constants_mapped_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(impl_->pass_constants_index);

  if (!recorder.IsResourceTracked(*impl_->projection_buffer)) {
    recorder.BeginTrackingResourceState(
      *impl_->projection_buffer, graphics::ResourceStates::kGenericRead, true);
  }
  if (!recorder.IsResourceTracked(*impl_->projection_upload_buffer)) {
    recorder.BeginTrackingResourceState(*impl_->projection_upload_buffer,
      graphics::ResourceStates::kGenericRead, true);
  }
  if (!recorder.IsResourceTracked(*impl_->request_flags_buffer)) {
    recorder.BeginTrackingResourceState(
      *impl_->request_flags_buffer, graphics::ResourceStates::kCommon, true);
  }
  if (!recorder.IsResourceTracked(*impl_->request_clear_buffer)) {
    recorder.BeginTrackingResourceState(*impl_->request_clear_buffer,
      graphics::ResourceStates::kGenericRead, true);
  }
  if (!recorder.IsResourceTracked(depth_texture)) {
    recorder.BeginTrackingResourceState(
      depth_texture, DepthTextureInitialState(depth_texture), true);
  }
  if (impl_->config->enable_light_grid_pruning) {
    if (cluster_grid_buffer == nullptr || light_index_list_buffer == nullptr) {
      LOG_F(ERROR,
        "VSM page-request light-grid pruning requested but LightCullingPass "
        "did not publish tracked buffers");
      co_return;
    }
    if (!recorder.IsResourceTracked(*cluster_grid_buffer)) {
      recorder.BeginTrackingResourceState(
        *cluster_grid_buffer, graphics::ResourceStates::kCommon, true);
    }
    if (!recorder.IsResourceTracked(*light_index_list_buffer)) {
      recorder.BeginTrackingResourceState(
        *light_index_list_buffer, graphics::ResourceStates::kCommon, true);
    }
  }

  recorder.RequireResourceState(
    depth_texture, graphics::ResourceStates::kShaderResource);
  if (!impl_->frame_projections.empty()) {
    recorder.RequireResourceState(
      *impl_->projection_upload_buffer, graphics::ResourceStates::kCopySource);
    recorder.RequireResourceState(
      *impl_->projection_buffer, graphics::ResourceStates::kCopyDest);
  }
  if (impl_->config->enable_light_grid_pruning) {
    recorder.RequireResourceState(
      *cluster_grid_buffer, graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(
      *light_index_list_buffer, graphics::ResourceStates::kShaderResource);
  }
  recorder.RequireResourceState(
    *impl_->request_clear_buffer, graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *impl_->request_flags_buffer, graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();

  if (!impl_->frame_projections.empty()) {
    recorder.CopyBuffer(*impl_->projection_buffer, 0U,
      *impl_->projection_upload_buffer, 0U,
      impl_->frame_projections.size() * sizeof(VsmPageRequestProjection));
    recorder.RequireResourceState(
      *impl_->projection_buffer, graphics::ResourceStates::kGenericRead);
  }
  recorder.CopyBuffer(*impl_->request_flags_buffer, 0U,
    *impl_->request_clear_buffer, 0U,
    static_cast<std::size_t>(impl_->virtual_page_count)
      * sizeof(VsmShaderPageRequestFlags));

  recorder.RequireResourceState(
    *impl_->request_flags_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  impl_->resources_prepared = true;
  co_return;
}

auto VsmPageRequestGeneratorPass::DoExecute(CommandRecorder& recorder)
  -> co::Co<>
{
  if (!impl_->resources_prepared) {
    DLOG_F(2, "VSM page-request resources were not prepared, skipping");
    co_return;
  }

  if (!impl_->depth_output.is_complete) {
    co_return;
  }

  const auto groups_x = std::max(
    1U, (impl_->depth_output.width + kThreadGroupSize - 1U) / kThreadGroupSize);
  const auto groups_y = std::max(1U,
    (impl_->depth_output.height + kThreadGroupSize - 1U) / kThreadGroupSize);

  recorder.Dispatch(groups_x, groups_y, 1U);
  recorder.RequireResourceState(
    *impl_->request_flags_buffer, graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  Context().RegisterPass(this);
  co_return;
}

auto VsmPageRequestGeneratorPass::ValidateConfig() -> void
{
  if (impl_->gfx == nullptr) {
    throw std::runtime_error("VsmPageRequestGeneratorPass requires Graphics");
  }
  if (impl_->config->max_projection_count == 0U) {
    throw std::runtime_error(
      "VsmPageRequestGeneratorPass max_projection_count must be > 0");
  }
  if (impl_->config->max_virtual_page_count == 0U) {
    throw std::runtime_error(
      "VsmPageRequestGeneratorPass max_virtual_page_count must be > 0");
  }
}

auto VsmPageRequestGeneratorPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  graphics::ShaderRequest shader_request {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Renderer/Vsm/VsmPageRequestGenerator.hlsl",
    .entry_point = "CS",
  };

  return ComputePipelineDesc::Builder()
    .SetComputeShader(std::move(shader_request))
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VsmPageRequestGenerator_PSO")
    .Build();
}

auto VsmPageRequestGeneratorPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
