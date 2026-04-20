//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Passes/AtmosphereCameraAerialPerspectivePass.h>

#include <limits>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
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
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>

namespace oxygen::vortex::environment {

namespace {

namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

constexpr std::uint32_t kAerialWidth = 32U;
constexpr std::uint32_t kAerialHeight = 32U;
constexpr std::uint32_t kAerialDepth = 16U;
constexpr std::uint32_t kThreadGroupSizeX = 4U;
constexpr std::uint32_t kThreadGroupSizeY = 4U;
constexpr std::uint32_t kThreadGroupSizeZ = 4U;

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

auto BuildPipelineDesc() -> graphics::ComputePipelineDesc
{
  auto root_bindings = BuildVortexRootBindings();
  return graphics::ComputePipelineDesc::Builder {}
    .SetComputeShader(graphics::ShaderRequest {
      .stage = ShaderType::kCompute,
      .source_path = "Vortex/Services/Environment/AtmosphereCameraAerialPerspective.hlsl",
      .entry_point = "VortexAtmosphereCameraAerialPerspectiveCS",
    })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size()))
    .SetDebugName("Vortex.Environment.AtmosphereCameraAerialPerspective")
    .Build();
}

auto MakeTextureViewDesc(const graphics::ResourceViewType view_type)
  -> graphics::TextureViewDescription
{
  return graphics::TextureViewDescription {
    .view_type = view_type,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = Format::kRGBA16Float,
    .dimension = TextureType::kTexture3D,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .is_read_only_dsv = false,
  };
}

} // namespace

AtmosphereCameraAerialPerspectivePass::AtmosphereCameraAerialPerspectivePass(
  Renderer& renderer)
  : renderer_(renderer)
  , pass_constants_buffer_(observer_ptr { renderer.GetGraphics().get() },
      renderer.GetStagingProvider(), static_cast<std::uint32_t>(sizeof(PassConstants)),
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "Environment.AtmosphereCameraAerialPerspective.PassConstants")
{
}

AtmosphereCameraAerialPerspectivePass::~AtmosphereCameraAerialPerspectivePass()
{
  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return;
  }

  auto& registry = gfx->GetResourceRegistry();
  for (const auto& texture : live_textures_) {
    if (texture != nullptr && registry.Contains(*texture)) {
      registry.UnRegisterResource(*texture);
    }
  }
}

auto AtmosphereCameraAerialPerspectivePass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  auto gfx = renderer_.GetGraphics();
  if (gfx != nullptr) {
    auto& registry = gfx->GetResourceRegistry();
    for (const auto& texture : live_textures_) {
      if (texture != nullptr && registry.Contains(*texture)) {
        registry.UnRegisterResource(*texture);
      }
    }
  }
  live_textures_.clear();
  pass_constants_buffer_.OnFrameStart(sequence, slot);
}

auto AtmosphereCameraAerialPerspectivePass::Record(RenderContext& ctx,
  const EnvironmentViewData& view_data,
  const internal::StableAtmosphereState& stable_state,
  const ShaderVisibleIndex sky_view_lut_srv) -> RecordState
{
  auto state = RecordState {
    .requested = ctx.current_view.view_id != kInvalidViewId
      && ctx.current_view.with_atmosphere
      && stable_state.view_products.atmosphere.enabled
      && sky_view_lut_srv.IsValid(),
  };
  if (!state.requested
    || !renderer_.HasCapability(RendererCapabilityFamily::kEnvironmentLighting)
    || ctx.view_constants == nullptr) {
    return state;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return state;
  }

  auto texture = gfx->CreateTexture({
    .width = kAerialWidth,
    .height = kAerialHeight,
    .depth = kAerialDepth,
    .array_size = 1U,
    .mip_levels = 1U,
    .sample_count = 1U,
    .sample_quality = 0U,
    .format = Format::kRGBA16Float,
    .texture_type = TextureType::kTexture3D,
    .debug_name = "Vortex.Environment.AtmosphereCameraAerialPerspective",
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
  if (texture == nullptr) {
    return state;
  }
  texture->SetName("Vortex.Environment.AtmosphereCameraAerialPerspective");

  auto& registry = gfx->GetResourceRegistry();
  if (!registry.Contains(*texture)) {
    registry.Register(texture);
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  auto srv_handle = allocator.AllocateRaw(
    graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!srv_handle.IsValid()) {
    throw std::runtime_error(
      "AtmosphereCameraAerialPerspectivePass: failed to allocate SRV descriptor");
  }
  const auto aerial_srv = allocator.GetShaderVisibleIndex(srv_handle);
  registry.RegisterView(*texture, std::move(srv_handle),
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_SRV));

  auto uav_handle = allocator.AllocateRaw(
    graphics::ResourceViewType::kTexture_UAV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!uav_handle.IsValid()) {
    throw std::runtime_error(
      "AtmosphereCameraAerialPerspectivePass: failed to allocate UAV descriptor");
  }
  const auto aerial_uav = allocator.GetShaderVisibleIndex(uav_handle);
  registry.RegisterView(*texture, std::move(uav_handle),
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_UAV));

  const auto& atmosphere = stable_state.view_products.atmosphere;
  auto sun_direction_ws = glm::vec3 { 0.0F, 0.8660254F, 0.5F };
  auto sun_illuminance_rgb_lux = glm::vec3 { 0.0F, 0.0F, 0.0F };
  if (stable_state.view_products.atmosphere_lights[0].enabled) {
    const auto& slot0 = stable_state.view_products.atmosphere_lights[0];
    const auto length_sq
      = glm::dot(slot0.direction_to_light_ws, slot0.direction_to_light_ws);
    if (length_sq > 1.0e-6F) {
      sun_direction_ws = glm::normalize(slot0.direction_to_light_ws);
    }
    sun_illuminance_rgb_lux = slot0.illuminance_rgb_lux;
  }

  const auto constants = PassConstants {
    .output_texture_uav = aerial_uav.get(),
    .output_width = kAerialWidth,
    .output_height = kAerialHeight,
    .output_depth = kAerialDepth,
    .sky_view_lut_srv = sky_view_lut_srv.get(),
    ._pad0 = 0U,
    ._pad1 = 0U,
    ._pad2 = 0U,
    .sky_aerial_luminance_aerial_start_depth_m = {
      view_data.sky_aerial_luminance_aerial_start_depth_m.x,
      view_data.sky_aerial_luminance_aerial_start_depth_m.y,
      view_data.sky_aerial_luminance_aerial_start_depth_m.z,
      view_data.sky_aerial_luminance_aerial_start_depth_m.w,
    },
    .aerial_distance_scale_strength_camera_altitude = {
      view_data.aerial_perspective_distance_scale,
      view_data.aerial_scattering_strength,
      view_data.planet_up_ws_camera_altitude_m.w,
      atmosphere.trace_sample_count_scale,
    },
    .planet_radius_atmosphere_height_height_fog_contribution_pad = {
      atmosphere.planet_radius_m,
      atmosphere.atmosphere_height_m,
      atmosphere.height_fog_contribution,
      0.0F,
    },
    .sun_direction_ws_pad = {
      sun_direction_ws.x,
      sun_direction_ws.y,
      sun_direction_ws.z,
      0.0F,
    },
    .sun_illuminance_rgb_pad = {
      sun_illuminance_rgb_lux.x,
      sun_illuminance_rgb_lux.y,
      sun_illuminance_rgb_lux.z,
      0.0F,
    },
  };
  auto constants_alloc = pass_constants_buffer_.Allocate(1U);
  if (!constants_alloc.has_value() || !constants_alloc->TryWriteObject(constants)) {
    return state;
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(
    queue_key, "EnvironmentLightingService AtmosphereCameraAerialPerspective");
  if (!recorder) {
    return state;
  }

  TrackTextureFromKnownOrInitial(*recorder, *texture);
  recorder->RequireResourceState(*texture, graphics::ResourceStates::kUnorderedAccess);
  recorder->FlushBarriers();

  recorder->SetPipelineState(BuildPipelineDesc());
  recorder->SetComputeRootConstantBufferView(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
    ctx.view_constants->GetGPUVirtualAddress());
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    0U, 0U);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    constants_alloc->srv.get(), 1U);

  graphics::GpuEventScope pass_scope(*recorder,
    "Vortex.Environment.AtmosphereCameraAerialPerspective",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);
  const auto dispatch_x
    = (kAerialWidth + (kThreadGroupSizeX - 1U)) / kThreadGroupSizeX;
  const auto dispatch_y
    = (kAerialHeight + (kThreadGroupSizeY - 1U)) / kThreadGroupSizeY;
  const auto dispatch_z
    = (kAerialDepth + (kThreadGroupSizeZ - 1U)) / kThreadGroupSizeZ;
  recorder->Dispatch(dispatch_x, dispatch_y, dispatch_z);
  recorder->RequireResourceStateFinal(
    *texture, graphics::ResourceStates::kShaderResource);

  live_textures_.push_back(texture);
  state.executed = true;
  state.camera_aerial_perspective_srv = aerial_srv;
  state.camera_aerial_perspective_uav = aerial_uav;
  state.width = kAerialWidth;
  state.height = kAerialHeight;
  state.depth = kAerialDepth;
  state.dispatch_count_x = dispatch_x;
  state.dispatch_count_y = dispatch_y;
  state.dispatch_count_z = dispatch_z;
  return state;
}

} // namespace oxygen::vortex::environment
