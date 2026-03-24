//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Nexus/TimelineGatedSlotReuse.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/LightCullingConfig.h>

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::ShaderVisibleIndex;
using oxygen::engine::LightCullingPass;
using oxygen::engine::LightCullingPassConfig;
using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::ComputePipelineDesc;
using oxygen::graphics::ResourceViewType;
using oxygen::nexus::DomainKey;
using oxygen::nexus::TimelineGatedSlotReuse;

namespace oxygen::engine {

namespace {

  //! Pass constants uploaded to GPU for the light culling dispatch.
  /*!
   Layout must match `LightCullingPassConstants` in LightCulling.hlsl.
  */
  struct alignas(packing::kShaderDataFieldAlignment) LightCullingPassConstants {
    // Resources (heap indices)
    ShaderVisibleIndex depth_texture_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex light_buffer_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex light_list_uav_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex light_count_uav_index { kInvalidShaderVisibleIndex };

    // Dispatch parameters
    glm::mat4 inv_projection_matrix { 1.0F };
    glm::vec2 screen_dimensions { 0.0F };
    uint32_t num_lights { 0 };
    uint32_t _pad0 { 0 };

    // Config (mirrors LightCullingConfig GPU block)
    uint32_t cluster_dim_x { 0 };
    uint32_t cluster_dim_y { 0 };
    uint32_t cluster_dim_z { 0 };
    uint32_t tile_size_px { 16 };

    float z_near { 0.1F };
    float z_far { 1000.0F };
    float z_scale { 0.0F };
    float z_bias { 0.0F };
  };
  static_assert(
    sizeof(LightCullingPassConstants) % packing::kShaderDataFieldAlignment
    == 0);

  const DomainKey kTimelineRetireDomain {
    .view_type = ResourceViewType::kStructuredBuffer_SRV,
    .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible,
  };

} // namespace

//=== Implementation Details ===----------------------------------------------//

struct LightCullingPass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;
  std::string name;

  // Current grid dimensions (computed per-frame based on screen size)
  LightCullingConfig::GridDimensions grid_dims {};

  // GPU-only buffers for compute output (default heap, UAV+SRV)
  std::shared_ptr<Buffer> cluster_grid_buffer; // uint2 per cluster
  std::shared_ptr<Buffer> light_index_list_buffer; // uint per light ref

  // UAV indices (for compute shader write)
  ShaderVisibleIndex cluster_grid_uav { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex light_index_list_uav { kInvalidShaderVisibleIndex };

  // SRV indices (published through LightingFrameBindings for shading)
  ShaderVisibleIndex cluster_grid_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex light_index_list_srv { kInvalidShaderVisibleIndex };

  // Cached buffer capacity to detect resize needs
  uint32_t cluster_buffer_capacity { 0 };
  uint32_t light_list_buffer_capacity { 0 };

  // Current frame's depth texture SRV (single descriptor updated in-place)
  ShaderVisibleIndex depth_texture_srv { kInvalidShaderVisibleIndex };
  const graphics::Texture* depth_texture_owner { nullptr };
  bool resources_prepared { false };

  // Cached light data from LightManager
  ShaderVisibleIndex positional_lights_srv { kInvalidShaderVisibleIndex };
  const graphics::Buffer* positional_lights_buffer { nullptr };
  uint32_t num_positional_lights { 0 };

  // Pass constants CBV
  std::shared_ptr<Buffer> pass_constants_buffer;
  graphics::NativeView pass_constants_cbv;
  ShaderVisibleIndex pass_constants_index { kInvalidShaderVisibleIndex };
  void* pass_constants_mapped_ptr { nullptr };

  // Track last logged grid dimensions to avoid spam
  uint32_t last_logged_z { 0 };

  struct RetiredBuffer {
    std::shared_ptr<Buffer> resource;
  };
  std::vector<std::optional<RetiredBuffer>> retired_buffers_;
  std::vector<bindless::HeapIndex> retired_free_list_;
  TimelineGatedSlotReuse retire_strategy_;
  std::shared_ptr<graphics::CommandQueue> retire_queue_;
  graphics::FenceValue retire_fence_ { 0 };

  struct Telemetry {
    uint64_t frames_prepared { 0 };
    uint64_t frames_executed { 0 };
    uint64_t cluster_buffer_recreate_count { 0 };
    uint64_t light_list_buffer_recreate_count { 0 };
    uint64_t depth_srv_refresh_count { 0 };
    uint64_t peak_clusters { 0 };
    uint64_t peak_light_index_capacity { 0 };
  };
  Telemetry telemetry_ {};

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
    , name(config->debug_name)
    , retire_strategy_(
        [this](const DomainKey) -> bindless::HeapIndex {
          if (!retired_free_list_.empty()) {
            const auto idx = retired_free_list_.back();
            retired_free_list_.pop_back();
            return idx;
          }
          const auto idx = bindless::HeapIndex {
            static_cast<uint32_t>(retired_buffers_.size()),
          };
          retired_buffers_.emplace_back(std::nullopt);
          return idx;
        },
        [this](const DomainKey, const bindless::HeapIndex idx) -> void {
          const auto slot = static_cast<size_t>(idx.get());
          if (slot >= retired_buffers_.size()) {
            return;
          }
          auto& item = retired_buffers_[slot];
          if (item.has_value() && item->resource) {
            auto& registry = gfx->GetResourceRegistry();
            if (registry.Contains(*item->resource)) {
              registry.UnRegisterResource(*item->resource);
            }
          }
          item.reset();
          retired_free_list_.push_back(idx);
        })
  {
    // GPU buffers are created lazily in EnsureClusterBuffers() when grid
    // dimensions are known.
  }

  ~Impl()
  {
    if (pass_constants_buffer && (pass_constants_mapped_ptr != nullptr)) {
      pass_constants_buffer->UnMap();
      pass_constants_mapped_ptr = nullptr;
    }

    const auto nexus_telemetry = retire_strategy_.GetTelemetrySnapshot();
    const auto expected_zero_marker = [](const uint64_t value) -> const char* {
      return value == 0U ? " ✓" : " (expected 0) !";
    };
    LOG_SCOPE_F(INFO, "LightCullingPass Statistics");
    LOG_F(INFO, "frames prepared                     : {}",
      telemetry_.frames_prepared);
    LOG_F(INFO, "frames executed                     : {}",
      telemetry_.frames_executed);
    LOG_F(INFO, "cluster buffer recreate count       : {}",
      telemetry_.cluster_buffer_recreate_count);
    LOG_F(INFO, "light-list buffer recreate count    : {}",
      telemetry_.light_list_buffer_recreate_count);
    LOG_F(INFO, "depth SRV refresh count             : {}",
      telemetry_.depth_srv_refresh_count);
    LOG_F(INFO, "peak clusters                       : {}",
      telemetry_.peak_clusters);
    LOG_F(INFO, "peak light index capacity           : {}",
      telemetry_.peak_light_index_capacity);
    LOG_F(INFO, "nexus.allocate_calls                : {}",
      nexus_telemetry.allocate_calls);
    LOG_F(INFO, "nexus.release_calls                 : {}",
      nexus_telemetry.release_calls);
    LOG_F(INFO, "nexus.batch_release_calls           : {}",
      nexus_telemetry.batch_release_calls);
    LOG_F(INFO, "nexus.stale_reject_count            : {}{}",
      nexus_telemetry.stale_reject_count,
      expected_zero_marker(nexus_telemetry.stale_reject_count));
    LOG_F(INFO, "nexus.duplicate_reject_count        : {}{}",
      nexus_telemetry.duplicate_reject_count,
      expected_zero_marker(nexus_telemetry.duplicate_reject_count));
    LOG_F(INFO, "nexus.reclaimed_count               : {}",
      nexus_telemetry.reclaimed_count);
    LOG_F(INFO, "nexus.pending_count                 : {}{}",
      nexus_telemetry.pending_count,
      expected_zero_marker(nexus_telemetry.pending_count));
    LOG_F(INFO, "nexus.peak_pending_count            : {}",
      nexus_telemetry.peak_pending_count);
    LOG_F(INFO, "nexus.process_calls                 : {}",
      nexus_telemetry.process_calls);
    LOG_F(INFO, "nexus.process_for_calls             : {}",
      nexus_telemetry.process_for_calls);
    LOG_F(INFO, "nexus.peak_queue_count              : {}",
      nexus_telemetry.peak_queue_count);

    ForceReleaseRetiredBuffers();
  }

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  auto ForceReleaseRetiredBuffers() -> void
  {
    auto& registry = gfx->GetResourceRegistry();
    for (auto& item : retired_buffers_) {
      if (item.has_value() && item->resource
        && registry.Contains(*item->resource)) {
        registry.UnRegisterResource(*item->resource);
      }
      item.reset();
    }
    retired_free_list_.clear();
  }

  auto UpdateRetireQueue(CommandRecorder& recorder) -> void
  {
    const auto queue = recorder.GetTargetQueue();
    if (!queue) {
      retire_queue_.reset();
      retire_fence_ = graphics::FenceValue { 0 };
      return;
    }
    if (!retire_queue_ || retire_queue_.get() != queue.get()) {
      retire_queue_ = std::shared_ptr<graphics::CommandQueue>(
        queue.get(), [](graphics::CommandQueue*) { });
    }
    retire_fence_ = graphics::FenceValue { queue->GetCurrentValue() };
    retire_strategy_.ProcessFor(retire_queue_);
  }

  auto ScheduleRetireBuffer(std::shared_ptr<Buffer> old_resource) -> void
  {
    if (!old_resource) {
      return;
    }
    if (!retire_queue_) {
      auto& registry = gfx->GetResourceRegistry();
      if (registry.Contains(*old_resource)) {
        registry.UnRegisterResource(*old_resource);
      }
      return;
    }

    const auto slot_handle = retire_strategy_.Allocate(kTimelineRetireDomain);
    const auto slot_index
      = static_cast<size_t>(slot_handle.ToBindlessHandle().get());
    if (slot_index >= retired_buffers_.size()) {
      return;
    }
    retired_buffers_[slot_index]
      = RetiredBuffer { .resource = std::move(old_resource) };
    retire_strategy_.Release(
      kTimelineRetireDomain, slot_handle, retire_queue_, retire_fence_);
  }

  //! Create or update the depth texture SRV for reading depth in compute.
  auto EnsureDepthTextureSrv(const graphics::Texture& depth_tex)
    -> ShaderVisibleIndex
  {
    auto& allocator = gfx->GetDescriptorAllocator();
    auto& registry = gfx->GetResourceRegistry();

    // Convert depth format to SRV-compatible format
    const auto depth_format = depth_tex.GetDescriptor().format;
    Format srv_format = Format::kR32Float;

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

    graphics::TextureViewDescription srv_desc {
      .view_type = ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = srv_format,
      .dimension = depth_tex.GetDescriptor().texture_type,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      .is_read_only_dsv = false,
    };

    // Recover an already-registered SRV first. This is the normal resize-safe
    // path used by other passes when local pass state is stale but the global
    // registry still owns the descriptor slot.
    if (const auto existing_index
      = registry.FindShaderVisibleIndex(depth_tex, srv_desc);
      existing_index.has_value()) {
      depth_texture_srv = *existing_index;
      depth_texture_owner = &depth_tex;
      return depth_texture_srv;
    }

    if (depth_texture_srv.IsValid() && depth_texture_owner == &depth_tex
      && registry.Contains(depth_tex, srv_desc)) {
      return depth_texture_srv;
    }

    auto register_new_srv = [&]() -> ShaderVisibleIndex {
      auto srv_handle = allocator.Allocate(ResourceViewType::kTexture_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
      if (!srv_handle.IsValid()) {
        LOG_F(ERROR, "Failed to allocate depth SRV handle");
        return kInvalidShaderVisibleIndex;
      }
      depth_texture_srv = allocator.GetShaderVisibleIndex(srv_handle);

      auto native_view
        = registry.RegisterView(const_cast<graphics::Texture&>(depth_tex),
          std::move(srv_handle), srv_desc);
      if (!native_view->IsValid()) {
        LOG_F(ERROR, "Failed to register depth SRV view");
        depth_texture_srv = kInvalidShaderVisibleIndex;
        return kInvalidShaderVisibleIndex;
      }
      depth_texture_owner = &depth_tex;
      ++telemetry_.depth_srv_refresh_count;
      return depth_texture_srv;
    };

    if (!depth_texture_srv.IsValid()) {
      return register_new_srv();
    }

    const auto updated
      = registry.UpdateView(const_cast<graphics::Texture&>(depth_tex),
        bindless::HeapIndex { depth_texture_srv.get() }, srv_desc);
    if (!updated) {
      LOG_F(INFO, "Failed to update depth SRV view, re-registering");
      depth_texture_srv = kInvalidShaderVisibleIndex;
      depth_texture_owner = nullptr;
      return register_new_srv();
    }
    depth_texture_owner = &depth_tex;
    ++telemetry_.depth_srv_refresh_count;
    return depth_texture_srv;
  }

  auto EnsurePassConstantsBuffer() -> void
  {
    if (pass_constants_buffer && pass_constants_index.IsValid()) {
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
      throw std::runtime_error("Failed to create pass constants buffer");
    }
    pass_constants_buffer->SetName(desc.debug_name);

    pass_constants_mapped_ptr = pass_constants_buffer->Map(0, desc.size_bytes);
    if (pass_constants_mapped_ptr == nullptr) {
      throw std::runtime_error("Failed to map pass constants buffer");
    }

    graphics::BufferViewDescription cbv_view_desc;
    cbv_view_desc.view_type = ResourceViewType::kConstantBuffer;
    cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_view_desc.range = { 0U, desc.size_bytes };

    auto cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!cbv_handle.IsValid()) {
      throw std::runtime_error("Failed to allocate CBV descriptor handle");
    }
    pass_constants_index = allocator.GetShaderVisibleIndex(cbv_handle);

    registry.Register(pass_constants_buffer);
    pass_constants_cbv = registry.RegisterView(
      *pass_constants_buffer, std::move(cbv_handle), cbv_view_desc);
  }

  auto EnsureClusterBuffers(
    uint32_t total_clusters, uint32_t max_lights_per_cluster) -> void
  {
    auto& allocator = gfx->GetDescriptorAllocator();
    auto& registry = gfx->GetResourceRegistry();

    const uint32_t required_light_list_capacity
      = total_clusters * max_lights_per_cluster;
    if (telemetry_.peak_clusters < static_cast<uint64_t>(total_clusters)) {
      telemetry_.peak_clusters = total_clusters;
    }
    if (telemetry_.peak_light_index_capacity
      < static_cast<uint64_t>(required_light_list_capacity)) {
      telemetry_.peak_light_index_capacity = required_light_list_capacity;
    }

    if (!cluster_grid_buffer || cluster_buffer_capacity < total_clusters) {
      ++telemetry_.cluster_buffer_recreate_count;
      auto old_buffer = cluster_grid_buffer;
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
        throw std::runtime_error("Failed to create cluster grid buffer");
      }
      cluster_grid_buffer->SetName(desc.debug_name);
      registry.Register(cluster_grid_buffer);

      auto uav_handle
        = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
          graphics::DescriptorVisibility::kShaderVisible);
      if (!uav_handle.IsValid()) {
        throw std::runtime_error("Failed to allocate cluster grid UAV handle");
      }
      cluster_grid_uav = allocator.GetShaderVisibleIndex(uav_handle);

      graphics::BufferViewDescription uav_desc;
      uav_desc.view_type = ResourceViewType::kStructuredBuffer_UAV;
      uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      uav_desc.range = { 0U, buffer_size };
      uav_desc.stride = kClusterGridStride;
      registry.RegisterView(
        *cluster_grid_buffer, std::move(uav_handle), uav_desc);

      auto srv_handle
        = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
          graphics::DescriptorVisibility::kShaderVisible);
      if (!srv_handle.IsValid()) {
        throw std::runtime_error("Failed to allocate cluster grid SRV handle");
      }
      cluster_grid_srv = allocator.GetShaderVisibleIndex(srv_handle);

      graphics::BufferViewDescription srv_desc;
      srv_desc.view_type = ResourceViewType::kStructuredBuffer_SRV;
      srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      srv_desc.range = { 0U, buffer_size };
      srv_desc.stride = kClusterGridStride;
      registry.RegisterView(
        *cluster_grid_buffer, std::move(srv_handle), srv_desc);

      cluster_buffer_capacity = total_clusters;
      LOG_F(1, "Created cluster grid buffer for {} clusters", total_clusters);
      ScheduleRetireBuffer(std::move(old_buffer));
    }

    if (!light_index_list_buffer
      || light_list_buffer_capacity < required_light_list_capacity) {
      ++telemetry_.light_list_buffer_recreate_count;
      auto old_buffer = light_index_list_buffer;
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
        throw std::runtime_error("Failed to create light index list buffer");
      }
      light_index_list_buffer->SetName(desc.debug_name);
      registry.Register(light_index_list_buffer);

      auto uav_handle
        = allocator.Allocate(ResourceViewType::kStructuredBuffer_UAV,
          graphics::DescriptorVisibility::kShaderVisible);
      if (!uav_handle.IsValid()) {
        throw std::runtime_error(
          "Failed to allocate light index list UAV handle");
      }
      light_index_list_uav = allocator.GetShaderVisibleIndex(uav_handle);

      graphics::BufferViewDescription uav_desc;
      uav_desc.view_type = ResourceViewType::kStructuredBuffer_UAV;
      uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      uav_desc.range = { 0U, buffer_size };
      uav_desc.stride = kLightIndexStride;
      registry.RegisterView(
        *light_index_list_buffer, std::move(uav_handle), uav_desc);

      auto srv_handle
        = allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
          graphics::DescriptorVisibility::kShaderVisible);
      if (!srv_handle.IsValid()) {
        throw std::runtime_error(
          "Failed to allocate light index list SRV handle");
      }
      light_index_list_srv = allocator.GetShaderVisibleIndex(srv_handle);

      graphics::BufferViewDescription srv_desc;
      srv_desc.view_type = ResourceViewType::kStructuredBuffer_SRV;
      srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      srv_desc.range = { 0U, buffer_size };
      srv_desc.stride = kLightIndexStride;
      registry.RegisterView(
        *light_index_list_buffer, std::move(srv_handle), srv_desc);

      light_list_buffer_capacity = required_light_list_capacity;
      LOG_F(1, "Created light index list buffer for {} entries",
        required_light_list_capacity);
      ScheduleRetireBuffer(std::move(old_buffer));
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
  impl_->resources_prepared = false;
  ++impl_->telemetry_.frames_prepared;
  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  if (depth_pass == nullptr) {
    LOG_F(WARNING, "No DepthPrePass found, skipping");
    co_return;
  }

  const auto& depth_tex = depth_pass->GetDepthTexture();
  const auto& depth_desc = depth_tex.GetDescriptor();

  const auto& cluster_cfg = impl_->config->cluster;
  impl_->grid_dims = cluster_cfg.ComputeGridDimensions(
    { .width = depth_desc.width, .height = depth_desc.height });

  if (impl_->grid_dims.z != impl_->last_logged_z) {
    LOG_F(INFO, "config={} grid_dims={}x{}x{} ",
      static_cast<const void*>(impl_->config.get()), impl_->grid_dims.x,
      impl_->grid_dims.y, impl_->grid_dims.z);
    impl_->last_logged_z = impl_->grid_dims.z;
  }

  impl_->UpdateRetireQueue(recorder);

  impl_->EnsureClusterBuffers(
    impl_->grid_dims.total_clusters, cluster_cfg.max_lights_per_cluster);

  impl_->depth_texture_srv = impl_->EnsureDepthTextureSrv(depth_tex);
  if (impl_->depth_texture_srv == kInvalidShaderVisibleIndex) {
    LOG_F(ERROR, "Failed to create depth texture SRV");
    co_return;
  }

  auto& renderer = Context().GetRenderer();
  if (const auto light_manager = renderer.GetLightManager()) {
    light_manager->EnsureFrameResources();
    impl_->positional_lights_srv = light_manager->GetPositionalLightsSrvIndex();
    impl_->positional_lights_buffer
      = light_manager->GetPositionalLightsBuffer();
    impl_->num_positional_lights
      = static_cast<uint32_t>(light_manager->GetPositionalLights().size());
  } else {
    impl_->positional_lights_srv = kInvalidShaderVisibleIndex;
    impl_->positional_lights_buffer = nullptr;
    impl_->num_positional_lights = 0;
  }

  impl_->EnsurePassConstantsBuffer();
  SetPassConstantsIndex(impl_->pass_constants_index);

  recorder.BeginTrackingResourceState(
    *impl_->cluster_grid_buffer, graphics::ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    *impl_->light_index_list_buffer, graphics::ResourceStates::kCommon, true);
  recorder.BeginTrackingResourceState(
    depth_tex, graphics::ResourceStates::kCommon, true);
  if (impl_->num_positional_lights > 0) {
    CHECK_NOTNULL_F(impl_->positional_lights_buffer,
      "LightCullingPass expected positional light buffer for {} lights",
      impl_->num_positional_lights);
    recorder.BeginTrackingResourceState(*impl_->positional_lights_buffer,
      graphics::ResourceStates::kGenericRead, true);
  }

  recorder.RequireResourceState(
    depth_tex, graphics::ResourceStates::kShaderResource);
  if (impl_->num_positional_lights > 0) {
    recorder.RequireResourceState(
      *impl_->positional_lights_buffer, graphics::ResourceStates::kGenericRead);
  }

  recorder.RequireResourceState(
    *impl_->cluster_grid_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*impl_->light_index_list_buffer,
    graphics::ResourceStates::kUnorderedAccess);

  recorder.FlushBarriers();
  impl_->resources_prepared = true;

  co_return;
}

auto LightCullingPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  if (!impl_->resources_prepared) {
    DLOG_F(2, "resources were not prepared this frame, skipping");
    co_return;
  }

  ++impl_->telemetry_.frames_executed;
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

  const float z_scale
    = cluster_cfg.ComputeZScale(effective_z_near, effective_z_far);
  const float z_bias = cluster_cfg.ComputeZBias();

  LightCullingConfig cull_data = cluster_cfg;
  cull_data.bindless_cluster_grid_slot
    = ClusterGridSlot { impl_->cluster_grid_srv };
  cull_data.bindless_cluster_index_list_slot
    = ClusterIndexListSlot { impl_->light_index_list_srv };
  cull_data.cluster_dim_x = impl_->grid_dims.x;
  cull_data.cluster_dim_y = impl_->grid_dims.y;
  cull_data.cluster_dim_z = impl_->grid_dims.z;
  cull_data.z_near = effective_z_near;
  cull_data.z_far = effective_z_far;
  cull_data.z_scale = z_scale;
  cull_data.z_bias = z_bias;

  Context().GetRenderer().UpdateCurrentViewLightCullingConfig(
    Context(), cull_data);

  if (impl_->cluster_grid_uav == kInvalidShaderVisibleIndex
    || impl_->light_index_list_uav == kInvalidShaderVisibleIndex) {
    LOG_F(WARNING, "UAV resources not prepared, skipping");
    co_return;
  }

  const auto* depth_pass = Context().GetPass<DepthPrePass>();
  if (depth_pass == nullptr) {
    co_return;
  }

  const auto& depth_tex = depth_pass->GetDepthTexture();
  const auto& depth_desc = depth_tex.GetDescriptor();

  const ShaderVisibleIndex positional_lights_srv = impl_->positional_lights_srv;
  const uint32_t num_lights = impl_->num_positional_lights;

  if (num_lights == 0) {
    recorder.RequireResourceState(
      *impl_->cluster_grid_buffer, graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceState(*impl_->light_index_list_buffer,
      graphics::ResourceStates::kShaderResource);
    recorder.FlushBarriers();
    co_return;
  }

  const ShaderVisibleIndex depth_srv_index = impl_->depth_texture_srv;

  LightCullingPassConstants constants {
    .depth_texture_index = depth_srv_index,
    .light_buffer_index = positional_lights_srv,
    .light_list_uav_index = impl_->light_index_list_uav,
    .light_count_uav_index = impl_->cluster_grid_uav,
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

  recorder.Dispatch(impl_->grid_dims.x, impl_->grid_dims.y, impl_->grid_dims.z);

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

auto LightCullingPass::GetClusterGridBuffer() const noexcept
  -> std::shared_ptr<const graphics::Buffer>
{
  return impl_->cluster_grid_buffer;
}

auto LightCullingPass::GetLightIndexListBuffer() const noexcept
  -> std::shared_ptr<const graphics::Buffer>
{
  return impl_->light_index_list_buffer;
}

auto LightCullingPass::GetClusterConfig() const noexcept
  -> const LightCullingConfig&
{
  return impl_->config->cluster;
}

auto LightCullingPass::GetGridDimensions() const noexcept
  -> LightCullingConfig::GridDimensions
{
  return impl_->grid_dims;
}

//=== ComputeRenderPass Virtual Methods ===---------------------------------//

auto LightCullingPass::ValidateConfig() -> void
{
  const auto& cluster = impl_->config->cluster;
  if (cluster.tile_size_px == 0) {
    throw std::runtime_error("tile_size_px must be > 0");
  }
  if (cluster.cluster_dim_z == 0) {
    throw std::runtime_error("cluster_dim_z must be > 0");
  }
  if (cluster.z_near > 0.0F && cluster.z_far > 0.0F
    && cluster.z_near >= cluster.z_far) {
    throw std::runtime_error("z_near must be < z_far");
  }
}

auto LightCullingPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  graphics::ShaderRequest shader_request {
    .stage = oxygen::ShaderType::kCompute,
    .source_path = "Lighting/LightCulling.hlsl",
    .entry_point = "CS",
    .defines = { { .name = "CLUSTERED", .value = "1" } },
  };

  return ComputePipelineDesc::Builder()
    .SetComputeShader(std::move(shader_request))
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("LightCulling_Clustered_PSO")
    .Build();
}

auto LightCullingPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
