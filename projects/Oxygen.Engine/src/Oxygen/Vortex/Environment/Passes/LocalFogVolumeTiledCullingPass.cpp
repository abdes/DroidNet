//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Passes/LocalFogVolumeTiledCullingPass.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>

namespace oxygen::vortex::environment {

namespace {

namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

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
          = range.num_descriptors
              == (std::numeric_limits<std::uint32_t>::max)()
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

auto TrackBufferFromKnownOrInitial(
  graphics::CommandRecorder& recorder, const graphics::Buffer& buffer) -> void
{
  if (recorder.IsResourceTracked(buffer) || recorder.AdoptKnownResourceState(buffer)) {
    return;
  }
  recorder.BeginTrackingResourceState(buffer, graphics::ResourceStates::kCommon,
    true);
}

auto TrackTextureFromKnownOrInitial(graphics::CommandRecorder& recorder,
  const graphics::Texture& texture) -> void
{
  if (recorder.IsResourceTracked(texture) || recorder.AdoptKnownResourceState(texture)) {
    return;
  }

  auto initial = texture.GetDescriptor().initial_state;
  if (initial == graphics::ResourceStates::kUnknown
    || initial == graphics::ResourceStates::kUndefined) {
    initial = graphics::ResourceStates::kCommon;
  }
  recorder.BeginTrackingResourceState(texture, initial, true);
}

auto MakeStructuredViewDesc(const graphics::ResourceViewType view_type,
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

auto MakeTileTextureViewDesc(const graphics::ResourceViewType view_type)
  -> graphics::TextureViewDescription
{
  return graphics::TextureViewDescription {
    .view_type = view_type,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = Format::kR8UInt,
    .dimension = TextureType::kTexture2DArray,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .is_read_only_dsv = false,
  };
}

auto BuildPipelineDesc() -> graphics::ComputePipelineDesc
{
  auto root_bindings = BuildVortexRootBindings();
  return graphics::ComputePipelineDesc::Builder {}
    .SetComputeShader(graphics::ShaderRequest {
      .stage = ShaderType::kCompute,
      .source_path = "Vortex/Services/Environment/LocalFogVolumeTiledCulling.hlsl",
      .entry_point = "VortexLocalFogVolumeTiledCullingCS",
    })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size()))
    .SetDebugName("Vortex.Environment.LocalFogTiledCulling")
    .Build();
}

[[nodiscard]] auto MakeShaderPlane(
  const Frustum::Plane& plane, const bool flip_plane = false)
  -> std::array<float, 4>
{
  auto shader_plane = std::array<float, 4> {
    -plane.normal.x,
    -plane.normal.y,
    -plane.normal.z,
    plane.d,
  };
  if (flip_plane) {
    for (auto& value : shader_plane) {
      value = -value;
    }
  }
  return shader_plane;
}

} // namespace

LocalFogVolumeTiledCullingPass::LocalFogVolumeTiledCullingPass(Renderer& renderer)
  : renderer_(renderer)
{
}

LocalFogVolumeTiledCullingPass::~LocalFogVolumeTiledCullingPass()
{
  if (indirect_count_clear_buffer_ != nullptr && indirect_count_clear_buffer_->IsMapped()) {
    indirect_count_clear_buffer_->UnMap();
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return;
  }

  auto& registry = gfx->GetResourceRegistry();
  const auto unregister_buffer_if_present =
    [&registry](const std::shared_ptr<graphics::Buffer>& resource) {
      if (resource != nullptr && registry.Contains(*resource)) {
        registry.UnRegisterResource(*resource);
      }
    };
  const auto unregister_texture_if_present =
    [&registry](const std::shared_ptr<graphics::Texture>& resource) {
      if (resource != nullptr && registry.Contains(*resource)) {
        registry.UnRegisterResource(*resource);
      }
    };

  unregister_texture_if_present(tile_data_texture_);
  unregister_buffer_if_present(occupied_tile_buffer_);
  unregister_buffer_if_present(indirect_args_buffer_);
  unregister_buffer_if_present(indirect_count_clear_buffer_);
  for (const auto& retired : retired_textures_) {
    unregister_texture_if_present(retired);
  }
  for (const auto& retired : retired_buffers_) {
    unregister_buffer_if_present(retired);
  }
}

auto LocalFogVolumeTiledCullingPass::EnsurePassConstantsBuffer() -> bool
{
  if (pass_constants_buffer_.has_value()) {
    return true;
  }
  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }
  pass_constants_buffer_.emplace(observer_ptr { gfx.get() },
    renderer_.GetStagingProvider(), static_cast<std::uint32_t>(sizeof(PassConstants)),
    observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
    "Environment.LocalFogTiledCulling.PassConstants");
  return true;
}

auto LocalFogVolumeTiledCullingPass::EnsureTileDataTexture(
  const std::uint32_t tile_resolution_x, const std::uint32_t tile_resolution_y,
  const std::uint32_t slice_count) -> bool
{
  if (tile_resolution_x == 0U || tile_resolution_y == 0U || slice_count == 0U) {
    return false;
  }
  if (tile_data_texture_ != nullptr
    && tile_data_resolution_x_ >= tile_resolution_x
    && tile_data_resolution_y_ >= tile_resolution_y
    && tile_data_slice_count_ >= slice_count
    && tile_data_texture_srv_.IsValid() && tile_data_texture_uav_.IsValid()) {
    return true;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  auto new_texture = gfx->CreateTexture({
    .width = tile_resolution_x,
    .height = tile_resolution_y,
    .depth = 1U,
    .array_size = slice_count,
    .mip_levels = 1U,
    .sample_count = 1U,
    .sample_quality = 0U,
    .format = Format::kR8UInt,
    .texture_type = TextureType::kTexture2DArray,
    .debug_name = "Vortex.Environment.LocalFogTileData",
    .is_shader_resource = true,
    .is_render_target = false,
    .is_uav = true,
    .is_typeless = false,
    .is_shading_rate_surface = false,
    .clear_value = {},
    .use_clear_value = false,
    .initial_state = graphics::ResourceStates::kCommon,
    .cpu_access = graphics::ResourceAccessMode::kImmutable,
  });
  if (new_texture == nullptr) {
    return false;
  }
  new_texture->SetName("Vortex.Environment.LocalFogTileData");
  RegisterResourceIfNeeded(*gfx, new_texture);

  auto& allocator = gfx->GetDescriptorAllocator();
  auto& registry = gfx->GetResourceRegistry();

  const auto srv_desc
    = MakeTileTextureViewDesc(graphics::ResourceViewType::kTexture_SRV);
  const auto uav_desc
    = MakeTileTextureViewDesc(graphics::ResourceViewType::kTexture_UAV);

  auto srv_handle = allocator.AllocateRaw(
    graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!srv_handle.IsValid()) {
    throw std::runtime_error(
      "LocalFogVolumeTiledCullingPass: failed to allocate tile-data texture SRV");
  }
  const auto new_srv_index = allocator.GetShaderVisibleIndex(srv_handle);
  registry.RegisterView(*new_texture, std::move(srv_handle), srv_desc);

  auto uav_handle = allocator.AllocateRaw(
    graphics::ResourceViewType::kTexture_UAV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!uav_handle.IsValid()) {
    throw std::runtime_error(
      "LocalFogVolumeTiledCullingPass: failed to allocate tile-data texture UAV");
  }
  const auto new_uav_index = allocator.GetShaderVisibleIndex(uav_handle);
  registry.RegisterView(*new_texture, std::move(uav_handle), uav_desc);

  if (tile_data_texture_ != nullptr) {
    if (registry.Contains(*tile_data_texture_)) {
      registry.UnRegisterResource(*tile_data_texture_);
    }
    gfx->RegisterDeferredRelease(std::move(tile_data_texture_));
  }

  tile_data_texture_ = std::move(new_texture);
  tile_data_texture_srv_ = new_srv_index;
  tile_data_texture_uav_ = new_uav_index;
  tile_data_resolution_x_ = tile_resolution_x;
  tile_data_resolution_y_ = tile_resolution_y;
  tile_data_slice_count_ = slice_count;
  return true;
}

auto LocalFogVolumeTiledCullingPass::EnsureOccupiedTileDrawBuffers(
  const std::uint32_t tile_count) -> bool
{
  if (tile_count == 0U) {
    return false;
  }
  if (occupied_tile_buffer_ != nullptr && indirect_args_buffer_ != nullptr
    && occupied_tile_capacity_ >= tile_count
    && occupied_tile_buffer_uav_.IsValid() && occupied_tile_buffer_srv_.IsValid()
    && indirect_args_buffer_uav_.IsValid()
    && indirect_count_clear_buffer_ != nullptr) {
    return true;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  auto& registry = gfx->GetResourceRegistry();

  auto create_device_buffer = [gfx](std::string_view name,
                               const std::uint64_t size_bytes) {
    auto buffer = gfx->CreateBuffer({
      .size_bytes = size_bytes,
      .usage = graphics::BufferUsage::kStorage | graphics::BufferUsage::kIndirect,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(name),
    });
    if (buffer != nullptr) {
      buffer->SetName(name);
    }
    return buffer;
  };

  auto new_occupied_tiles = create_device_buffer(
    "Vortex.Environment.LocalFogOccupiedTiles",
    static_cast<std::uint64_t>(tile_count) * sizeof(std::uint32_t));
  auto new_indirect_args = create_device_buffer(
    "Vortex.Environment.LocalFogTileDrawArgs",
    sizeof(std::uint32_t) * 4U);
  if (new_occupied_tiles == nullptr || new_indirect_args == nullptr
    || tile_count == 0U) {
    return false;
  }

  RegisterResourceIfNeeded(*gfx, new_occupied_tiles);
  RegisterResourceIfNeeded(*gfx, new_indirect_args);

  auto occupied_uav_handle = allocator.AllocateRaw(
    graphics::ResourceViewType::kStructuredBuffer_UAV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!occupied_uav_handle.IsValid()) {
    throw std::runtime_error(
      "LocalFogVolumeTiledCullingPass: failed to allocate occupied-tile UAV");
  }
  const auto occupied_uav_index
    = allocator.GetShaderVisibleIndex(occupied_uav_handle);
  registry.RegisterView(*new_occupied_tiles, std::move(occupied_uav_handle),
    MakeStructuredViewDesc(graphics::ResourceViewType::kStructuredBuffer_UAV,
      new_occupied_tiles->GetSize(), sizeof(std::uint32_t)));

  auto occupied_srv_handle = allocator.AllocateRaw(
    graphics::ResourceViewType::kStructuredBuffer_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!occupied_srv_handle.IsValid()) {
    throw std::runtime_error(
      "LocalFogVolumeTiledCullingPass: failed to allocate occupied-tile SRV");
  }
  const auto occupied_srv_index
    = allocator.GetShaderVisibleIndex(occupied_srv_handle);
  registry.RegisterView(*new_occupied_tiles, std::move(occupied_srv_handle),
    MakeStructuredViewDesc(graphics::ResourceViewType::kStructuredBuffer_SRV,
      new_occupied_tiles->GetSize(), sizeof(std::uint32_t)));

  auto args_uav_handle = allocator.AllocateRaw(
    graphics::ResourceViewType::kStructuredBuffer_UAV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!args_uav_handle.IsValid()) {
    throw std::runtime_error(
      "LocalFogVolumeTiledCullingPass: failed to allocate indirect-args UAV");
  }
  const auto args_uav_index = allocator.GetShaderVisibleIndex(args_uav_handle);
  registry.RegisterView(*new_indirect_args, std::move(args_uav_handle),
    MakeStructuredViewDesc(graphics::ResourceViewType::kStructuredBuffer_UAV,
      new_indirect_args->GetSize(), sizeof(std::uint32_t)));

  if (occupied_tile_buffer_ != nullptr) {
    retired_buffers_.push_back(std::move(occupied_tile_buffer_));
  }
  if (indirect_args_buffer_ != nullptr) {
    retired_buffers_.push_back(std::move(indirect_args_buffer_));
  }

  occupied_tile_buffer_ = std::move(new_occupied_tiles);
  indirect_args_buffer_ = std::move(new_indirect_args);
  occupied_tile_buffer_uav_ = occupied_uav_index;
  occupied_tile_buffer_srv_ = occupied_srv_index;
  indirect_args_buffer_uav_ = args_uav_index;
  occupied_tile_capacity_ = tile_count;

  if (indirect_count_clear_buffer_ == nullptr) {
    indirect_count_clear_buffer_ = gfx->CreateBuffer({
      .size_bytes = sizeof(std::uint32_t) * 4U,
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = "Vortex.Environment.LocalFogTileDrawArgs.Clear",
    });
    if (indirect_count_clear_buffer_ == nullptr) {
      return false;
    }
    auto* mapped = static_cast<std::uint32_t*>(
      indirect_count_clear_buffer_->Map(0U, sizeof(std::uint32_t) * 4U));
    if (mapped == nullptr) {
      return false;
    }
    mapped[0] = 6U;
    mapped[1] = 0U;
    mapped[2] = 0U;
    mapped[3] = 0U;
  }

  return true;
}

auto LocalFogVolumeTiledCullingPass::Record(RenderContext& ctx,
  const SceneTextures& scene_textures,
  internal::LocalFogVolumeState::ViewProducts& products) -> RecordState
{
  products.tile_data_ready = false;
  products.tile_data_texture_slot = kInvalidShaderVisibleIndex;
  products.occupied_tile_buffer_slot = kInvalidShaderVisibleIndex;
  products.tile_resolution_x = 0U;
  products.tile_resolution_y = 0U;
  products.tile_capacity = 0U;
  products.occupied_tile_draw_args_buffer = nullptr;

  auto state = RecordState {
    .requested = renderer_.GetLocalFogEnabled() && ctx.current_view.with_local_fog
      && products.buffer_ready && products.instance_count > 0U,
    .executed = false,
  };
  if (!state.requested
    || !renderer_.HasCapability(RendererCapabilityFamily::kEnvironmentLighting)
    || !products.instance_culling_buffer_slot.IsValid()) {
    return state;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr || !EnsurePassConstantsBuffer()) {
    return state;
  }

  constexpr std::uint32_t kThreadGroupSize = 8U;
  const auto tile_pixel_size = renderer_.GetLocalFogTilePixelSize();
  auto left_plane = std::array<float, 4> { 0.0F, 0.0F, 0.0F, 0.0F };
  auto right_plane = std::array<float, 4> { 0.0F, 0.0F, 0.0F, 0.0F };
  auto top_plane = std::array<float, 4> { 0.0F, 0.0F, 0.0F, 0.0F };
  auto bottom_plane = std::array<float, 4> { 0.0F, 0.0F, 0.0F, 0.0F };
  auto near_plane = std::array<float, 4> { 0.0F, 0.0F, 0.0F, 0.0F };
  const auto extent = scene_textures.GetExtent();
  auto view_width = static_cast<float>((std::max)(extent.x, 1U));
  auto view_height = static_cast<float>((std::max)(extent.y, 1U));
  if (const auto* resolved_view = ctx.current_view.resolved_view.get();
    resolved_view != nullptr) {
    const auto frustum = resolved_view->GetFrustum();
    left_plane = MakeShaderPlane(frustum.planes[0]);
    right_plane = MakeShaderPlane(frustum.planes[1], true);
    top_plane = MakeShaderPlane(frustum.planes[3], true);
    bottom_plane = MakeShaderPlane(frustum.planes[2]);
    near_plane = MakeShaderPlane(frustum.planes[4]);

    const auto viewport = resolved_view->Viewport();
    if (viewport.IsValid()) {
      view_width = (std::max)(viewport.width, 1.0F);
      view_height = (std::max)(viewport.height, 1.0F);
    }
  }
  const auto tile_resolution_x = std::max(1U,
    (static_cast<std::uint32_t>(std::ceil(view_width)) + (tile_pixel_size - 1U))
      / tile_pixel_size);
  const auto tile_resolution_y = std::max(1U,
    (static_cast<std::uint32_t>(std::ceil(view_height))
      + (tile_pixel_size - 1U))
      / tile_pixel_size);
  const auto tile_count = tile_resolution_x * tile_resolution_y;
  if (!EnsureTileDataTexture(tile_resolution_x, tile_resolution_y,
        products.max_instances_per_tile + 1U)
    || !EnsureOccupiedTileDrawBuffers(tile_count)) {
    return state;
  }

  const auto use_hzb = renderer_.GetLocalFogUseHzb()
    && ctx.current_view.screen_hzb_available
    && ctx.current_view.screen_hzb_frame_slot.IsValid()
    && ctx.current_view.screen_hzb_width > 0U
    && ctx.current_view.screen_hzb_height > 0U
    && ctx.current_view.screen_hzb_mip_count > 0U;
  const auto tile_covered_resolution_x
    = static_cast<float>(tile_pixel_size * tile_resolution_x);
  const auto tile_covered_resolution_y
    = static_cast<float>(tile_pixel_size * tile_resolution_y);

  pass_constants_buffer_->OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
  const auto constants = PassConstants {
    .instance_buffer_slot = products.instance_buffer_slot.get(),
    .instance_culling_buffer_slot = products.instance_culling_buffer_slot.get(),
    .tile_data_texture_slot = tile_data_texture_uav_.get(),
    .occupied_tile_buffer_slot = occupied_tile_buffer_uav_.get(),
    .indirect_args_buffer_slot = indirect_args_buffer_uav_.get(),
    .instance_count = products.instance_count,
    .tile_resolution_x = tile_resolution_x,
    .tile_resolution_y = tile_resolution_y,
    .max_instances_per_tile = products.max_instances_per_tile,
    .use_hzb = use_hzb ? 1U : 0U,
    .left_plane = left_plane,
    .right_plane = right_plane,
    .top_plane = top_plane,
    .bottom_plane = bottom_plane,
    .near_plane = near_plane,
    .view_to_tile_space_ratio = {
      tile_covered_resolution_x / view_width,
      tile_covered_resolution_y / view_height,
    },
  };
  auto constants_alloc = pass_constants_buffer_->Allocate(1U);
  if (!constants_alloc.has_value() || !constants_alloc->TryWriteObject(constants)) {
    return state;
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(
    queue_key, "EnvironmentLightingService LocalFogTiledCulling");
  if (!recorder) {
    return state;
  }

  TrackTextureFromKnownOrInitial(*recorder, *tile_data_texture_);
  TrackBufferFromKnownOrInitial(*recorder, *occupied_tile_buffer_);
  TrackBufferFromKnownOrInitial(*recorder, *indirect_args_buffer_);
  TrackBufferFromKnownOrInitial(*recorder, *indirect_count_clear_buffer_);

  recorder->RequireResourceState(
    *indirect_count_clear_buffer_, graphics::ResourceStates::kCopySource);
  recorder->RequireResourceState(
    *indirect_args_buffer_, graphics::ResourceStates::kCopyDest);
  recorder->FlushBarriers();
  recorder->CopyBuffer(
    *indirect_args_buffer_, 0U, *indirect_count_clear_buffer_, 0U,
    sizeof(std::uint32_t) * 4U);

  recorder->RequireResourceState(
    *tile_data_texture_, graphics::ResourceStates::kUnorderedAccess);
  recorder->RequireResourceState(
    *occupied_tile_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder->RequireResourceState(
    *indirect_args_buffer_, graphics::ResourceStates::kUnorderedAccess);
  recorder->FlushBarriers();

  recorder->SetPipelineState(BuildPipelineDesc());
  if (ctx.view_constants != nullptr) {
    recorder->SetComputeRootConstantBufferView(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
      ctx.view_constants->GetGPUVirtualAddress());
  }
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    0U, 0U);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    constants_alloc->srv.get(), 1U);

  graphics::GpuEventScope pass_scope(*recorder, "Vortex.Stage14.LocalFogTiledCulling",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);
  const auto dispatch_x = std::max(
    1U, (tile_resolution_x + (kThreadGroupSize - 1U)) / kThreadGroupSize);
  const auto dispatch_y = std::max(
    1U, (tile_resolution_y + (kThreadGroupSize - 1U)) / kThreadGroupSize);
  recorder->Dispatch(dispatch_x, dispatch_y, 1U);

  recorder->RequireResourceStateFinal(
    *tile_data_texture_, graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceStateFinal(
    *occupied_tile_buffer_, graphics::ResourceStates::kShaderResource);
  recorder->RequireResourceStateFinal(
    *indirect_args_buffer_, graphics::ResourceStates::kIndirectArgument);

  products.tile_data_ready = true;
  products.tile_data_texture_slot = tile_data_texture_srv_;
  products.occupied_tile_buffer_slot = occupied_tile_buffer_srv_;
  products.tile_resolution_x = tile_resolution_x;
  products.tile_resolution_y = tile_resolution_y;
  products.tile_capacity = tile_count;
  products.occupied_tile_draw_args_buffer
    = observer_ptr<const graphics::Buffer> { indirect_args_buffer_.get() };

  LOG_F(INFO,
    "local_fog_hzb_consumed={} screen_hzb={}x{} mips={}",
    use_hzb ? "true" : "false", ctx.current_view.screen_hzb_width,
    ctx.current_view.screen_hzb_height, ctx.current_view.screen_hzb_mip_count);

  state.executed = true;
  state.consumed_instance_buffer = products.buffer_ready;
  state.consumed_published_screen_hzb = use_hzb;
  state.dispatch_count_x = dispatch_x;
  state.dispatch_count_y = dispatch_y;
  state.dispatch_count_z = 1U;
  return state;
}

} // namespace oxygen::vortex::environment
