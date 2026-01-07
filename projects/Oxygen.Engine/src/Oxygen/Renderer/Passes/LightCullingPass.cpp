//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>

using oxygen::engine::LightCullingPass;
using oxygen::engine::LightCullingPassConfig;
using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::ComputePipelineDesc;
using oxygen::graphics::ResourceViewType;

namespace {

//! Pass constants uploaded to GPU for the light culling dispatch.
/*!
 Layout must match `LightCullingPassConstants` in LightCulling.hlsl.
*/
struct alignas(16) LightCullingPassConstants {
  // Resources (heap indices)
  uint32_t depth_texture_index { oxygen::engine::kInvalidDescriptorSlot };
  uint32_t light_buffer_index { oxygen::engine::kInvalidDescriptorSlot };
  uint32_t light_list_uav_index { oxygen::engine::kInvalidDescriptorSlot };
  uint32_t light_count_uav_index { oxygen::engine::kInvalidDescriptorSlot };

  // Dispatch parameters
  glm::mat4 inv_projection_matrix { 1.0F };
  glm::vec2 screen_dimensions { 0.0F };
  uint32_t num_lights { 0 };
  uint32_t _pad0 { 0 };

  // Cluster config (for 3D clustering)
  uint32_t cluster_dim_x { 0 };
  uint32_t cluster_dim_y { 0 };
  uint32_t cluster_dim_z { 0 };
  uint32_t tile_size_px { 16 };

  float z_near { 0.1F };
  float z_far { 1000.0F };
  float z_scale { 0.0F };
  float z_bias { 0.0F };
};
static_assert(sizeof(LightCullingPassConstants) % 16 == 0,
  "Pass constants must be 16-byte aligned");

} // namespace

namespace oxygen::engine {

//=== Implementation Details ===----------------------------------------------//

struct LightCullingPass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;
  std::string name;

  // Current grid dimensions (computed per-frame based on screen size)
  ClusterConfig::GridDimensions grid_dims {};

  // Transient buffers for output (created lazily on first PrepareResources)
  std::unique_ptr<upload::TransientStructuredBuffer> cluster_grid_buffer;
  std::unique_ptr<upload::TransientStructuredBuffer> light_index_list_buffer;

  // Cached SRV indices for current frame
  ShaderVisibleIndex cluster_grid_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex light_index_list_srv { kInvalidShaderVisibleIndex };

  // Pass constants CBV
  std::shared_ptr<Buffer> pass_constants_buffer;
  graphics::NativeView pass_constants_cbv {};
  ShaderVisibleIndex pass_constants_index { kInvalidShaderVisibleIndex };
  void* pass_constants_mapped_ptr { nullptr };

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
    , name(config->debug_name)
  {
    // Buffers are created lazily in EnsureBuffers() when we have access
    // to the Renderer's upload services via RenderContext.
  }

  ~Impl()
  {
    if (pass_constants_buffer && pass_constants_mapped_ptr) {
      pass_constants_buffer->UnMap();
      pass_constants_mapped_ptr = nullptr;
    }
  }

  auto EnsurePassConstantsBuffer() -> void
  {
    if (pass_constants_buffer
      && pass_constants_index != kInvalidShaderVisibleIndex) {
      return;
    }

    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    const graphics::BufferDesc desc {
      .size_bytes = 256u,
      .usage = graphics::BufferUsage::kConstant,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = name + "_PassConstants",
    };

    pass_constants_buffer = gfx->CreateBuffer(desc);
    if (!pass_constants_buffer) {
      throw std::runtime_error(
        "LightCullingPass: Failed to create pass constants buffer");
    }
    pass_constants_buffer->SetName(desc.debug_name);

    pass_constants_mapped_ptr = pass_constants_buffer->Map(0, desc.size_bytes);
    if (!pass_constants_mapped_ptr) {
      throw std::runtime_error(
        "LightCullingPass: Failed to map pass constants buffer");
    }

    graphics::BufferViewDescription cbv_view_desc;
    cbv_view_desc.view_type = ResourceViewType::kConstantBuffer;
    cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_view_desc.range = { 0u, desc.size_bytes };

    auto cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!cbv_handle.IsValid()) {
      throw std::runtime_error(
        "LightCullingPass: Failed to allocate CBV descriptor handle");
    }
    pass_constants_index = allocator.GetShaderVisibleIndex(cbv_handle);

    registry.Register(pass_constants_buffer);
    pass_constants_cbv = registry.RegisterView(
      *pass_constants_buffer, std::move(cbv_handle), cbv_view_desc);
    // RegisterView internally validates and throws on failure
  }

  //! Create transient buffers lazily using Renderer's upload services.
  auto EnsureBuffers(Renderer& renderer) -> void
  {
    if (cluster_grid_buffer && light_index_list_buffer) {
      return; // Already created
    }

    auto& staging = renderer.GetStagingProvider();
    auto inline_transfers
      = observer_ptr(&renderer.GetInlineTransfersCoordinator());

    // Cluster grid: uint2 per cluster (offset, count)
    constexpr uint32_t kClusterGridStride = sizeof(uint32_t) * 2;
    cluster_grid_buffer
      = std::make_unique<upload::TransientStructuredBuffer>(gfx, staging,
        kClusterGridStride, inline_transfers, "LightCulling_ClusterGrid");

    // Light index list: uint per light reference
    constexpr uint32_t kLightIndexStride = sizeof(uint32_t);
    light_index_list_buffer
      = std::make_unique<upload::TransientStructuredBuffer>(gfx, staging,
        kLightIndexStride, inline_transfers, "LightCulling_LightIndexList");
  }
};

//=== Public Interface ===----------------------------------------------------//

LightCullingPass::LightCullingPass(
  observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config->debug_name)
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
}

LightCullingPass::~LightCullingPass() = default;

auto LightCullingPass::DoPrepareResources(CommandRecorder& recorder) -> co::Co<>
{
  // Get frame lifecycle info from RenderContext
  const auto frame_sequence = Context().frame_sequence;
  const auto frame_slot = Context().frame_slot;

  // Validate frame lifecycle state - these must be set by Renderer before
  // render graph execution
  DCHECK_F(frame_slot != frame::kInvalidSlot,
    "LightCullingPass::DoPrepareResources called with invalid frame_slot; "
    "Renderer must call OnFrameStart before executing render graph");
  DCHECK_F(frame_slot.get() < frame::kFramesInFlight.get(),
    "LightCullingPass::DoPrepareResources frame_slot {} >= kFramesInFlight {}",
    frame_slot.get(), frame::kFramesInFlight.get());

  // Ensure transient buffers are created (lazy init from Renderer services)
  impl_->EnsureBuffers(Context().GetRenderer());

  // Reset cached SRV indices for this frame
  impl_->cluster_grid_srv = kInvalidShaderVisibleIndex;
  impl_->light_index_list_srv = kInvalidShaderVisibleIndex;

  // Sync OnFrameStart for transient buffers
  DCHECK_NOTNULL_F(impl_->cluster_grid_buffer.get(),
    "cluster_grid_buffer must be created by EnsureBuffers");
  DCHECK_NOTNULL_F(impl_->light_index_list_buffer.get(),
    "light_index_list_buffer must be created by EnsureBuffers");

  impl_->cluster_grid_buffer->OnFrameStart(frame_sequence, frame_slot);
  impl_->light_index_list_buffer->OnFrameStart(frame_sequence, frame_slot);

  // Get screen dimensions from depth texture
  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  if (!depth_pass) {
    LOG_F(WARNING, "LightCullingPass: No DepthPrePass found, skipping");
    co_return;
  }

  const auto& depth_tex = depth_pass->GetDepthTexture();
  const auto& depth_desc = depth_tex.GetDescriptor();
  const uint32_t screen_width = depth_desc.width;
  const uint32_t screen_height = depth_desc.height;

  // Compute grid dimensions
  const auto& cluster_cfg = impl_->config->cluster;
  impl_->grid_dims
    = cluster_cfg.ComputeGridDimensions(screen_width, screen_height);

  // Allocate cluster grid buffer
  auto grid_alloc
    = impl_->cluster_grid_buffer->Allocate(impl_->grid_dims.total_clusters);
  if (!grid_alloc) {
    LOG_F(ERROR, "LightCullingPass: Failed to allocate cluster grid buffer");
    co_return;
  }
  impl_->cluster_grid_srv = grid_alloc->srv;

  // Allocate light index list buffer
  // Worst case: every cluster has max_lights_per_cluster lights
  const uint32_t max_light_indices
    = impl_->grid_dims.total_clusters * cluster_cfg.max_lights_per_cluster;
  auto list_alloc = impl_->light_index_list_buffer->Allocate(max_light_indices);
  if (!list_alloc) {
    LOG_F(
      ERROR, "LightCullingPass: Failed to allocate light index list buffer");
    co_return;
  }
  impl_->light_index_list_srv = list_alloc->srv;

  // Ensure pass constants buffer exists
  impl_->EnsurePassConstantsBuffer();
  SetPassConstantsIndex(impl_->pass_constants_index);

  // Transition depth texture to shader resource state for reading
  recorder.RequireResourceState(
    depth_tex, graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  co_return;
}

auto LightCullingPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  if (impl_->cluster_grid_srv == kInvalidShaderVisibleIndex
    || impl_->light_index_list_srv == kInvalidShaderVisibleIndex) {
    LOG_F(WARNING, "LightCullingPass: Resources not prepared, skipping");
    co_return;
  }

  // Get required resources
  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  if (!depth_pass) {
    co_return;
  }

  const auto& depth_tex = depth_pass->GetDepthTexture();
  const auto& depth_desc = depth_tex.GetDescriptor();

  // Get light data from the prepared frame
  // The prepared scene frame provides access to light manager via context
  // For now, we check if there's prepared light data available
  // TODO: Access light manager through prepared frame when available
  const ShaderVisibleIndex positional_lights_srv { kInvalidShaderVisibleIndex };
  const uint32_t num_lights = 0; // Will be populated when scene prep is wired

  if (num_lights == 0) {
    // No positional lights to cull - still need to clear the grid
    // For now, skip dispatch if no lights
    co_return;
  }

  // TODO: Get the actual SRV index for the depth texture
  // For now, we'll need to create one or get it from the registry
  const ShaderVisibleIndex depth_srv_index = kInvalidShaderVisibleIndex;

  // Update pass constants
  const auto& cluster_cfg = impl_->config->cluster;
  LightCullingPassConstants constants {
    .depth_texture_index = depth_srv_index.get(),
    .light_buffer_index = positional_lights_srv.get(),
    .light_list_uav_index = impl_->light_index_list_srv.get(),
    .light_count_uav_index = impl_->cluster_grid_srv.get(),
    .inv_projection_matrix = Context().current_view.resolved_view
      ? Context().current_view.resolved_view->InverseProjection()
      : glm::mat4 { 1.0F },
    .screen_dimensions = { static_cast<float>(depth_desc.width),
      static_cast<float>(depth_desc.height) },
    .num_lights = num_lights,
    ._pad0 = 0,
    .cluster_dim_x = impl_->grid_dims.x,
    .cluster_dim_y = impl_->grid_dims.y,
    .cluster_dim_z = impl_->grid_dims.z,
    .tile_size_px = cluster_cfg.tile_size_px,
    .z_near = cluster_cfg.z_near,
    .z_far = cluster_cfg.z_far,
    .z_scale = cluster_cfg.ComputeZScale(),
    .z_bias = cluster_cfg.ComputeZBias(),
  };

  std::memcpy(impl_->pass_constants_mapped_ptr, &constants, sizeof(constants));

  // Pipeline state is set by ComputeRenderPass::OnExecute()

  // Dispatch compute shader
  // One thread group per tile
  recorder.Dispatch(impl_->grid_dims.x, impl_->grid_dims.y, impl_->grid_dims.z);

  co_return;
}

auto LightCullingPass::GetClusterGridSrvIndex() const noexcept
  -> ShaderVisibleIndex
{
  return impl_->cluster_grid_srv;
}

auto LightCullingPass::GetLightIndexListSrvIndex() const noexcept
  -> ShaderVisibleIndex
{
  return impl_->light_index_list_srv;
}

auto LightCullingPass::GetClusterConfig() const noexcept -> const ClusterConfig&
{
  return impl_->config->cluster;
}

auto LightCullingPass::GetGridDimensions() const noexcept
  -> ClusterConfig::GridDimensions
{
  return impl_->grid_dims;
}

//=== ComputeRenderPass Virtual Methods ===---------------------------------//

auto LightCullingPass::ValidateConfig() -> void
{
  // Validate cluster configuration
  const auto& cluster = impl_->config->cluster;
  if (cluster.tile_size_px == 0) {
    throw std::runtime_error("LightCullingPass: tile_size_px must be > 0");
  }
  if (cluster.depth_slices == 0) {
    throw std::runtime_error("LightCullingPass: depth_slices must be > 0");
  }
  if (cluster.z_near >= cluster.z_far) {
    throw std::runtime_error("LightCullingPass: z_near must be < z_far");
  }
}

auto LightCullingPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  return ComputePipelineDesc::Builder()
    .SetComputeShader(graphics::ShaderRequest {
      .stage = oxygen::ShaderType::kCompute,
      .source_path = "Passes/Lighting/LightCulling.hlsl",
      .entry_point = "CS",
    })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("LightCulling_PSO")
    .Build();
}

auto LightCullingPass::NeedRebuildPipelineState() const -> bool
{
  // Compute PSO is static, only need to build once
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
