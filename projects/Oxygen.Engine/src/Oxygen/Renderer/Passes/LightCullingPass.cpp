//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Internal/EnvironmentDynamicDataManager.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/LightCullingData.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/EnvironmentDynamicData.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>

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

  // GPU-only buffers for compute output (default heap, UAV+SRV)
  std::shared_ptr<Buffer> cluster_grid_buffer; // uint2 per cluster
  std::shared_ptr<Buffer> light_index_list_buffer; // uint per light ref

  // UAV indices (for compute shader write)
  ShaderVisibleIndex cluster_grid_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex light_index_list_uav { kInvalidShaderVisibleIndex };

  // SRV indices (for pixel shader read via EnvironmentDynamicData)
  ShaderVisibleIndex cluster_grid_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex light_index_list_srv { kInvalidShaderVisibleIndex };

  // Cached buffer capacity to detect resize needs
  uint32_t cluster_buffer_capacity { 0 };
  uint32_t light_list_buffer_capacity { 0 };

  // Map from depth texture pointer to its SRV index (handles multi-buffering)
  std::unordered_map<const graphics::Texture*, ShaderVisibleIndex>
    depth_texture_srvs;

  // Current frame's depth texture SRV (looked up from the map)
  ShaderVisibleIndex depth_texture_srv { kInvalidShaderVisibleIndex };

  // Cached light data from LightManager
  ShaderVisibleIndex positional_lights_srv { kInvalidShaderVisibleIndex };
  uint32_t num_positional_lights { 0 };

  // Pass constants CBV
  std::shared_ptr<Buffer> pass_constants_buffer;
  graphics::NativeView pass_constants_cbv {};
  ShaderVisibleIndex pass_constants_index { kInvalidShaderVisibleIndex };
  void* pass_constants_mapped_ptr { nullptr };

  // Track last built cluster mode to detect PSO rebuild needs
  uint32_t last_built_depth_slices { 0 };

  // Track last logged grid dimensions to avoid spam
  uint32_t last_logged_z { 0 };

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
    , name(config->debug_name)
  {
    // GPU buffers are created lazily in EnsureClusterBuffers() when grid
    // dimensions are known.
  }

  ~Impl()
  {
    if (pass_constants_buffer && pass_constants_mapped_ptr) {
      pass_constants_buffer->UnMap();
      pass_constants_mapped_ptr = nullptr;
    }
    // GPU buffers (cluster_grid_buffer, light_index_list_buffer) are cleaned
    // up automatically via shared_ptr. Associated descriptors are managed by
    // ResourceRegistry and will be released when views are unregistered.
  }

  //! Create or update the depth texture SRV for reading depth in compute.
  /*!
   Depth textures use typeless formats (e.g., R32_TYPELESS for D32_FLOAT).
   For SRV access, we must use the corresponding readable format (R32_FLOAT).

   With multi-buffered framebuffers, different depth textures may be used
   each frame. We cache the SRV for each texture in a map.
  */
  auto EnsureDepthTextureSrv(const graphics::Texture& depth_tex)
    -> ShaderVisibleIndex
  {
    auto& allocator = gfx->GetDescriptorAllocator();
    auto& registry = gfx->GetResourceRegistry();

    // Convert depth format to SRV-compatible format
    const auto depth_format = depth_tex.GetDescriptor().format;
    Format srv_format = Format::kR32Float;

    // Map depth formats to their SRV-readable equivalents
    switch (depth_format) {
    case Format::kDepth32:
    case Format::kDepth32Stencil8:
      srv_format = Format::kR32Float;
      break;
    case Format::kDepth16:
      srv_format = Format::kR16UNorm;
      break;
    case Format::kDepth24Stencil8:
      srv_format = Format::kR32Float;
      break;
    default:
      srv_format = depth_format;
      break;
    }

    // Create the SRV view description
    graphics::TextureViewDescription srv_desc {
      .view_type = ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = srv_format,
      .dimension = depth_tex.GetDescriptor().texture_type,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      .is_read_only_dsv = false,
    };

    // Check if we have a cached SRV AND the registry still has this view
    // (protects against address reuse after texture destruction)
    if (auto it = depth_texture_srvs.find(&depth_tex);
      it != depth_texture_srvs.end()) {
      if (registry.Contains(depth_tex, srv_desc)) {
        depth_texture_srv = it->second;
        return depth_texture_srv;
      }
      // Stale entry - texture was destroyed and address reused
      depth_texture_srvs.erase(it);
    }

    // Allocate descriptor handle and create SRV
    auto srv_handle = allocator.Allocate(ResourceViewType::kTexture_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!srv_handle.IsValid()) {
      LOG_F(ERROR, "LightCullingPass: Failed to allocate depth SRV handle");
      return kInvalidShaderVisibleIndex;
    }
    depth_texture_srv = allocator.GetShaderVisibleIndex(srv_handle);

    // Register the view (move handle ownership to registry)
    auto native_view
      = registry.RegisterView(const_cast<graphics::Texture&>(depth_tex),
        std::move(srv_handle), srv_desc);
    if (!native_view->IsValid()) {
      LOG_F(ERROR, "LightCullingPass: Failed to register depth SRV view");
      depth_texture_srv = kInvalidShaderVisibleIndex;
      return kInvalidShaderVisibleIndex;
    }

    // Cache in our map
    depth_texture_srvs[&depth_tex] = depth_texture_srv;

    LOG_F(1, "LightCullingPass: Created depth SRV at index {} for texture {}",
      depth_texture_srv.get(), static_cast<const void*>(&depth_tex));
    return depth_texture_srv;
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

  //! Create or resize GPU buffers for cluster grid output.
  /*!
   Creates default-heap buffers with both UAV (for compute write) and SRV
   (for pixel shader read) views. Resizes if capacity is insufficient.
  */
  auto EnsureClusterBuffers(
    uint32_t total_clusters, uint32_t max_lights_per_cluster) -> void
  {
    auto& allocator = gfx->GetDescriptorAllocator();
    auto& registry = gfx->GetResourceRegistry();

    const uint32_t required_light_list_capacity
      = total_clusters * max_lights_per_cluster;

    // Check if we need to create/resize cluster grid buffer
    if (!cluster_grid_buffer || cluster_buffer_capacity < total_clusters) {
      // Release old descriptors if resizing
      // (DescriptorAllocator handles this via RAII when views are
      // re-registered)

      constexpr uint32_t kClusterGridStride = sizeof(uint32_t) * 2; // uint2
      const uint32_t buffer_size = total_clusters * kClusterGridStride;

      const graphics::BufferDesc desc {
        .size_bytes = buffer_size,
        .usage = graphics::BufferUsage::kStorage,
        .memory = graphics::BufferMemory::kDeviceLocal,
        .debug_name = name + "_ClusterGrid",
      };

      cluster_grid_buffer = gfx->CreateBuffer(desc);
      if (!cluster_grid_buffer) {
        throw std::runtime_error(
          "LightCullingPass: Failed to create cluster grid buffer");
      }
      cluster_grid_buffer->SetName(desc.debug_name);
      registry.Register(cluster_grid_buffer);

      // Create UAV view
      auto uav_handle
        = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
          graphics::DescriptorVisibility::kShaderVisible);
      if (!uav_handle.IsValid()) {
        throw std::runtime_error(
          "LightCullingPass: Failed to allocate cluster grid UAV handle");
      }
      cluster_grid_uav = allocator.GetShaderVisibleIndex(uav_handle);

      graphics::BufferViewDescription uav_desc;
      uav_desc.view_type = ResourceViewType::kStructuredBuffer_UAV;
      uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      uav_desc.range = { 0u, buffer_size };
      uav_desc.stride = kClusterGridStride;
      registry.RegisterView(
        *cluster_grid_buffer, std::move(uav_handle), uav_desc);

      // Create SRV view
      auto srv_handle
        = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
          graphics::DescriptorVisibility::kShaderVisible);
      if (!srv_handle.IsValid()) {
        throw std::runtime_error(
          "LightCullingPass: Failed to allocate cluster grid SRV handle");
      }
      cluster_grid_srv = allocator.GetShaderVisibleIndex(srv_handle);

      graphics::BufferViewDescription srv_desc;
      srv_desc.view_type = ResourceViewType::kStructuredBuffer_SRV;
      srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      srv_desc.range = { 0u, buffer_size };
      srv_desc.stride = kClusterGridStride;
      registry.RegisterView(
        *cluster_grid_buffer, std::move(srv_handle), srv_desc);

      cluster_buffer_capacity = total_clusters;
      LOG_F(1, "LightCullingPass: Created cluster grid buffer for {} clusters",
        total_clusters);
    }

    // Check if we need to create/resize light index list buffer
    if (!light_index_list_buffer
      || light_list_buffer_capacity < required_light_list_capacity) {
      constexpr uint32_t kLightIndexStride = sizeof(uint32_t);
      const uint32_t buffer_size
        = required_light_list_capacity * kLightIndexStride;

      const graphics::BufferDesc desc {
        .size_bytes = buffer_size,
        .usage = graphics::BufferUsage::kStorage,
        .memory = graphics::BufferMemory::kDeviceLocal,
        .debug_name = name + "_LightIndexList",
      };

      light_index_list_buffer = gfx->CreateBuffer(desc);
      if (!light_index_list_buffer) {
        throw std::runtime_error(
          "LightCullingPass: Failed to create light index list buffer");
      }
      light_index_list_buffer->SetName(desc.debug_name);
      registry.Register(light_index_list_buffer);

      // Create UAV view
      auto uav_handle
        = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
          graphics::DescriptorVisibility::kShaderVisible);
      if (!uav_handle.IsValid()) {
        throw std::runtime_error(
          "LightCullingPass: Failed to allocate light index list UAV handle");
      }
      light_index_list_uav = allocator.GetShaderVisibleIndex(uav_handle);

      graphics::BufferViewDescription uav_desc;
      uav_desc.view_type = ResourceViewType::kStructuredBuffer_UAV;
      uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      uav_desc.range = { 0u, buffer_size };
      uav_desc.stride = kLightIndexStride;
      registry.RegisterView(
        *light_index_list_buffer, std::move(uav_handle), uav_desc);

      // Create SRV view
      auto srv_handle
        = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
          graphics::DescriptorVisibility::kShaderVisible);
      if (!srv_handle.IsValid()) {
        throw std::runtime_error(
          "LightCullingPass: Failed to allocate light index list SRV handle");
      }
      light_index_list_srv = allocator.GetShaderVisibleIndex(srv_handle);

      graphics::BufferViewDescription srv_desc;
      srv_desc.view_type = ResourceViewType::kStructuredBuffer_SRV;
      srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      srv_desc.range = { 0u, buffer_size };
      srv_desc.stride = kLightIndexStride;
      registry.RegisterView(
        *light_index_list_buffer, std::move(srv_handle), srv_desc);

      light_list_buffer_capacity = required_light_list_capacity;
      LOG_F(1,
        "LightCullingPass: Created light index list buffer for {} entries",
        required_light_list_capacity);
    }
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

  // Debug: Log grid dimensions when they change
  if (impl_->grid_dims.z != impl_->last_logged_z) {
    LOG_F(INFO,
      "LightCullingPass: config={} grid_dims={}x{}x{} depth_slices={} "
      "z_scale={:.3f}",
      static_cast<const void*>(impl_->config.get()), impl_->grid_dims.x,
      impl_->grid_dims.y, impl_->grid_dims.z, cluster_cfg.depth_slices,
      cluster_cfg.ComputeZScale());
    impl_->last_logged_z = impl_->grid_dims.z;
  }

  // Ensure GPU buffers exist with sufficient capacity
  impl_->EnsureClusterBuffers(
    impl_->grid_dims.total_clusters, cluster_cfg.max_lights_per_cluster);

  // Create depth texture SRV for compute shader access
  impl_->depth_texture_srv = impl_->EnsureDepthTextureSrv(depth_tex);
  if (impl_->depth_texture_srv == kInvalidShaderVisibleIndex) {
    LOG_F(ERROR, "LightCullingPass: Failed to create depth texture SRV");
    co_return;
  }

  // Gather light data from LightManager
  auto& renderer = Context().GetRenderer();
  if (const auto light_manager = renderer.GetLightManager()) {
    // Ensure light manager has uploaded its GPU buffers for this frame
    light_manager->EnsureFrameResources();
    impl_->positional_lights_srv = light_manager->GetPositionalLightsSrvIndex();
    impl_->num_positional_lights
      = static_cast<uint32_t>(light_manager->GetPositionalLights().size());
  } else {
    impl_->positional_lights_srv = kInvalidShaderVisibleIndex;
    impl_->num_positional_lights = 0;
  }

  // Ensure pass constants buffer exists
  impl_->EnsurePassConstantsBuffer();
  SetPassConstantsIndex(impl_->pass_constants_index);

  // Begin tracking cluster buffers if newly created (initial state is kCommon)
  // The keep_initial_state=true means no transition barrier is inserted here
  recorder.BeginTrackingResourceState(
    *impl_->cluster_grid_buffer, graphics::ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *impl_->light_index_list_buffer, graphics::ResourceStates::kCommon, true);

  // Transition depth texture to shader resource state for reading
  recorder.RequireResourceState(
    depth_tex, graphics::ResourceStates::kShaderResource);

  // Transition cluster buffers to UAV state for compute shader write
  recorder.RequireResourceState(
    *impl_->cluster_grid_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*impl_->light_index_list_buffer,
    graphics::ResourceStates::kUnorderedAccess);

  recorder.FlushBarriers();

  co_return;
}

auto LightCullingPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  // Get effective z_near/z_far from camera if not explicitly set in config
  // Config value of 0 means "use camera"
  const auto& cluster_cfg = impl_->config->cluster;
  float effective_z_near = cluster_cfg.z_near;
  float effective_z_far = cluster_cfg.z_far;

  if (const auto& view = Context().current_view.resolved_view) {
    if (effective_z_near <= 0.0F) {
      effective_z_near = view->NearPlane();
    }
    if (effective_z_far <= 0.0F) {
      effective_z_far = view->FarPlane();
    }
  }

  // Calculate Z-binning parameters
  auto compute_z_scale = [&]() -> float {
    if (cluster_cfg.depth_slices <= 1 || effective_z_near <= 0.0F
      || effective_z_far <= effective_z_near) {
      return 0.0F;
    }
    const float log_ratio = std::log2(effective_z_far / effective_z_near);
    return static_cast<float>(cluster_cfg.depth_slices) / log_ratio;
  };
  const float z_scale = compute_z_scale();
  const float z_bias = cluster_cfg.ComputeZBias();

  // Wire clustered data into EnvironmentDynamicDataManager (root CBV b3)
  if (auto manager = Context().env_dynamic_manager) {
    const auto view_id = Context().current_view.view_id;

    const auto u_grid_srv = impl_->cluster_grid_srv.get();
    const auto u_index_list_srv = impl_->light_index_list_srv.get();

    // Aggregate culling data into a single struct to simplify the API.
    LightCullingData cull_data {
      .bindless_cluster_grid_slot = u_grid_srv,
      .bindless_cluster_index_list_slot = u_index_list_srv,
      .cluster_dim_x = impl_->grid_dims.x,
      .cluster_dim_y = impl_->grid_dims.y,
      .cluster_dim_z = impl_->grid_dims.z,
      .tile_size_px = cluster_cfg.tile_size_px,
    };

    manager->SetLightCullingData(view_id, cull_data);

    manager->SetZBinning(
      view_id, effective_z_near, effective_z_far, z_scale, z_bias);

    // Resolve and upload to GPU
    manager->UpdateIfNeeded(view_id);
  }

  if (impl_->cluster_grid_uav == kInvalidShaderVisibleIndex
    || impl_->light_index_list_uav == kInvalidShaderVisibleIndex) {
    LOG_F(WARNING, "LightCullingPass: UAV resources not prepared, skipping");
    co_return;
  }

  // Get required resources
  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  if (!depth_pass) {
    co_return;
  }

  const auto& depth_tex = depth_pass->GetDepthTexture();
  const auto& depth_desc = depth_tex.GetDescriptor();

  // Use light data gathered during PrepareResources
  const ShaderVisibleIndex positional_lights_srv = impl_->positional_lights_srv;
  const uint32_t num_lights = impl_->num_positional_lights;

  if (num_lights == 0) {
    // No positional lights to cull - output buffers remain zeroed
    // Skip dispatch but still transition buffers for consistency
    LOG_F(2, "LightCullingPass: No positional lights, skipping dispatch");

    // Transition buffers to SRV state for shading passes
    recorder.RequireResourceState(
      *impl_->cluster_grid_buffer, graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(*impl_->light_index_list_buffer,
      graphics::ResourceStates::kShaderResource);
    recorder.FlushBarriers();

    co_return;
  }

  // Use the depth SRV created during PrepareResources
  const ShaderVisibleIndex depth_srv_index = impl_->depth_texture_srv;

  // Update pass constants - use UAV indices for compute shader write
  LightCullingPassConstants constants {
    .depth_texture_index = depth_srv_index.get(),
    .light_buffer_index = positional_lights_srv.get(),
    .light_list_uav_index = impl_->light_index_list_uav.get(),
    .light_count_uav_index = impl_->cluster_grid_uav.get(),
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
    .z_near = effective_z_near,
    .z_far = effective_z_far,
    .z_scale = z_scale,
    .z_bias = z_bias,
  };

  std::memcpy(impl_->pass_constants_mapped_ptr, &constants, sizeof(constants));

  // Diagnostic: log dispatch parameters
  static uint32_t last_z_dim = 0;
  if (impl_->grid_dims.z != last_z_dim) {
    LOG_F(INFO,
      "LightCullingPass::DoExecute: dispatching {}x{}x{}, z_scale={:.4f}, "
      "z_bias={:.4f}",
      impl_->grid_dims.x, impl_->grid_dims.y, impl_->grid_dims.z,
      constants.z_scale, constants.z_bias);
    last_z_dim = impl_->grid_dims.z;
  }

  // Pipeline state is set by ComputeRenderPass::OnExecute()

  // Dispatch compute shader
  // One thread group per tile
  recorder.Dispatch(impl_->grid_dims.x, impl_->grid_dims.y, impl_->grid_dims.z);

  // Transition buffers from UAV to SRV for pixel shader read
  recorder.RequireResourceState(
    *impl_->cluster_grid_buffer, graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *impl_->light_index_list_buffer, graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

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
  // z_near = 0 means "use camera", so only validate if both are explicitly set
  if (cluster.z_near > 0.0F && cluster.z_far > 0.0F
    && cluster.z_near >= cluster.z_far) {
    throw std::runtime_error("LightCullingPass: z_near must be < z_far");
  }
}

auto LightCullingPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  // Determine if we need the clustered permutation
  const bool use_clustered = impl_->config->cluster.depth_slices > 1;

  // Build shader request with optional CLUSTERED define
  graphics::ShaderRequest shader_request {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Passes/Lighting/LightCulling.hlsl",
    .entry_point = "CS",
    .defines = {},
  };

  if (use_clustered) {
    shader_request.defines.push_back({ "CLUSTERED", "1" });
    LOG_F(INFO, "LightCullingPass: Using CLUSTERED shader variant");
  } else {
    LOG_F(INFO, "LightCullingPass: Using tile-based shader variant");
  }

  // Track what we built for NeedRebuildPipelineState()
  impl_->last_built_depth_slices = impl_->config->cluster.depth_slices;

  return ComputePipelineDesc::Builder()
    .SetComputeShader(std::move(shader_request))
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName(use_clustered ? "LightCulling_Clustered_PSO"
                                : "LightCulling_TileBased_PSO")
    .Build();
}

auto LightCullingPass::NeedRebuildPipelineState() const -> bool
{
  // Rebuild if never built
  if (!LastBuiltPsoDesc().has_value()) {
    return true;
  }

  // Rebuild if cluster mode changed (tile-based vs clustered)
  const bool current_clustered = impl_->config->cluster.depth_slices > 1;
  const bool last_clustered = impl_->last_built_depth_slices > 1;
  if (current_clustered != last_clustered) {
    LOG_F(INFO,
      "LightCullingPass: Cluster mode changed (%s -> %s), rebuilding PSO",
      last_clustered ? "clustered" : "tile-based",
      current_clustered ? "clustered" : "tile-based");
    return true;
  }

  return false;
}

} // namespace oxygen::engine
