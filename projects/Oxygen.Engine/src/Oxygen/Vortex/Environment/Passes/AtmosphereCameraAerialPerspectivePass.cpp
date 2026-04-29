//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Passes/AtmosphereCameraAerialPerspectivePass.h>

#include <algorithm>
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
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>

namespace oxygen::vortex::environment {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

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
          table.count = range.num_descriptors
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
    if (recorder.IsResourceTracked(texture)
      || recorder.AdoptKnownResourceState(texture)) {
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
        .source_path
        = "Vortex/Services/Environment/AtmosphereCameraAerialPerspective.hlsl",
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

  auto SetVec4(float (&target)[4], const glm::vec3 value, const float w = 0.0F)
    -> void
  {
    target[0] = value.x;
    target[1] = value.y;
    target[2] = value.z;
    target[3] = w;
  }

  auto PopulateLight(float (&direction_enabled)[4], float (&illuminance_rgb)[4],
    const environment::AtmosphereLightModel& light) -> void
  {
    auto direction = glm::vec3 { 0.0F, 0.0F, 1.0F };
    if (light.enabled) {
      const auto length_sq
        = glm::dot(light.direction_to_light_ws, light.direction_to_light_ws);
      if (length_sq > 1.0e-6F) {
        direction = glm::normalize(light.direction_to_light_ws);
      }
    }

    SetVec4(direction_enabled, direction, light.enabled ? 1.0F : 0.0F);
    // UE passes GetOuterSpaceIlluminance() here. In Vortex the atmosphere
    // light payload already stores intensity-scaled RGB illuminance in lux,
    // which is the equivalent physical quantity for the LUT builders.
    SetVec4(illuminance_rgb,
      light.enabled ? light.illuminance_rgb_lux
                    : glm::vec3 { 0.0F, 0.0F, 0.0F });
  }

  auto ReleaseLiveTextures(Graphics& gfx,
    std::vector<std::shared_ptr<graphics::Texture>>& live_textures) -> void
  {
    auto& registry = gfx.GetResourceRegistry();
    for (auto& texture : live_textures) {
      if (texture == nullptr) {
        continue;
      }
      if (registry.Contains(*texture)) {
        registry.UnRegisterResource(*texture);
      }
      gfx.RegisterDeferredRelease(std::move(texture));
    }
    live_textures.clear();
  }

} // namespace

AtmosphereCameraAerialPerspectivePass::AtmosphereCameraAerialPerspectivePass(
  Renderer& renderer)
  : renderer_(renderer)
  , pass_constants_buffer_(observer_ptr { renderer.GetGraphics().get() },
      renderer.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(PassConstants)),
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

  ReleaseLiveTextures(*gfx, live_textures_);
}

auto AtmosphereCameraAerialPerspectivePass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  auto gfx = renderer_.GetGraphics();
  if (gfx != nullptr) {
    ReleaseLiveTextures(*gfx, live_textures_);
  } else {
    live_textures_.clear();
  }
  pass_constants_buffer_.OnFrameStart(sequence, slot);
}

auto AtmosphereCameraAerialPerspectivePass::Record(RenderContext& ctx,
  const EnvironmentViewData& view_data,
  const internal::StableAtmosphereState& stable_state,
  const internal::AtmosphereLutCache& cache) -> RecordState
{
  const auto& cache_state = cache.GetState();
  auto state = RecordState {
    .requested = ctx.current_view.view_id != kInvalidViewId
      && ctx.current_view.with_atmosphere
      && stable_state.view_products.atmosphere.enabled
      && cache_state.transmittance_lut_valid
      && cache_state.multi_scattering_lut_valid,
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

  const auto width = cache_state.internal_parameters.camera_aerial_width;
  const auto height = cache_state.internal_parameters.camera_aerial_height;
  const auto depth
    = cache_state.internal_parameters.camera_aerial_depth_resolution;
  auto texture = gfx->CreateTexture({
    .width = width,
    .height = height,
    .depth = depth,
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
  auto srv_handle = allocator.AllocateBindless(
    bindless::generated::kTexturesDomain,
    graphics::ResourceViewType::kTexture_SRV);
  if (!srv_handle.IsValid()) {
    throw std::runtime_error("AtmosphereCameraAerialPerspectivePass: failed to "
                             "allocate SRV descriptor");
  }
  const auto aerial_srv = allocator.GetShaderVisibleIndex(srv_handle);
  registry.RegisterView(*texture, std::move(srv_handle),
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_SRV));

  auto uav_handle = allocator.AllocateBindless(
    bindless::generated::kTexturesDomain,
    graphics::ResourceViewType::kTexture_UAV);
  if (!uav_handle.IsValid()) {
    throw std::runtime_error("AtmosphereCameraAerialPerspectivePass: failed to "
                             "allocate UAV descriptor");
  }
  const auto aerial_uav = allocator.GetShaderVisibleIndex(uav_handle);
  registry.RegisterView(*texture, std::move(uav_handle),
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_UAV));

  const auto& atmosphere = stable_state.view_products.atmosphere;
  auto constants = PassConstants {};
  constants.output_header.output_texture_uav = aerial_uav.get();
  constants.output_header.output_width = width;
  constants.output_header.output_height = height;
  constants.output_header.output_depth = depth;
  constants.lut_header0.transmittance_lut_srv
    = cache_state.transmittance_lut_srv.get();
  constants.lut_header0.multi_scattering_lut_srv
    = cache_state.multi_scattering_lut_srv.get();
  constants.lut_header0.transmittance_width
    = cache_state.internal_parameters.transmittance_width;
  constants.lut_header0.transmittance_height
    = cache_state.internal_parameters.transmittance_height;
  constants.lut_header1.multi_scattering_width
    = cache_state.internal_parameters.multi_scattering_width;
  constants.lut_header1.multi_scattering_height
    = cache_state.internal_parameters.multi_scattering_height;
  constants.lut_header1.active_light_count
    = stable_state.view_products.atmosphere_light_count;
  constants.lut_header1.depth_resolution = depth;
  constants.depth_control0.fog_show_flag_factor
    = ctx.current_view.with_atmosphere ? 1.0F : 0.0F;
  constants.depth_control0.real_time_reflection_360_mode = 0.0F;
  constants.depth_control0.depth_resolution_inv
    = depth > 0U ? 1.0F / static_cast<float>(depth) : 0.0F;
  constants.depth_control0.depth_slice_length_km
    = cache_state.internal_parameters.camera_aerial_depth_slice_length_km;
  constants.depth_control1.depth_slice_length_km_inv
    = constants.depth_control0.depth_slice_length_km > 1.0e-6F
    ? 1.0F / constants.depth_control0.depth_slice_length_km
    : 0.0F;
  constants.depth_control1.sample_count_per_slice
    = cache_state.internal_parameters.camera_aerial_sample_count_per_slice;
  constants.depth_control1.start_depth_km
    = std::max(view_data.sky_aerial_luminance_aerial_start_depth_km.w, 0.0F);
  constants.depth_control1.planet_radius_km
    = engine::atmos::MetersToSkyUnit(atmosphere.planet_radius_m);
  constants.atmosphere_scales0.atmosphere_height_km
    = engine::atmos::MetersToSkyUnit(atmosphere.atmosphere_height_m);
  constants.atmosphere_scales0.aerial_perspective_distance_scale
    = view_data.aerial_perspective_distance_scale;
  constants.atmosphere_scales0.rayleigh_scale_height_km
    = engine::atmos::MetersToSkyUnit(atmosphere.rayleigh_scale_height_m);
  constants.atmosphere_scales1.mie_scale_height_km
    = engine::atmos::MetersToSkyUnit(atmosphere.mie_scale_height_m);
  constants.atmosphere_scales1.multi_scattering_factor
    = atmosphere.multi_scattering_factor;
  constants.atmosphere_scales1.mie_anisotropy = atmosphere.mie_anisotropy;
  SetVec4(constants.ground_albedo_rgb, atmosphere.ground_albedo_rgb);
  SetVec4(constants.rayleigh_scattering_per_km_rgb,
    atmosphere.rayleigh_scattering_rgb * engine::atmos::kSkyUnitToM);
  SetVec4(constants.mie_scattering_per_km_rgb,
    atmosphere.mie_scattering_rgb * engine::atmos::kSkyUnitToM);
  SetVec4(constants.mie_absorption_per_km_rgb,
    atmosphere.mie_absorption_rgb * engine::atmos::kSkyUnitToM);
  SetVec4(constants.ozone_absorption_per_km_rgb,
    atmosphere.ozone_absorption_rgb * engine::atmos::kSkyUnitToM);
  constants.ozone_density_layer0[0]
    = engine::atmos::MetersToSkyUnit(
      atmosphere.ozone_density_profile.layers[0].width_m);
  constants.ozone_density_layer0[1]
    = atmosphere.ozone_density_profile.layers[0].exp_term;
  constants.ozone_density_layer0[2]
    = atmosphere.ozone_density_profile.layers[0].linear_term
    * engine::atmos::kSkyUnitToM;
  constants.ozone_density_layer0[3]
    = atmosphere.ozone_density_profile.layers[0].constant_term;
  constants.ozone_density_layer1[0]
    = engine::atmos::MetersToSkyUnit(
      atmosphere.ozone_density_profile.layers[1].width_m);
  constants.ozone_density_layer1[1]
    = atmosphere.ozone_density_profile.layers[1].exp_term;
  constants.ozone_density_layer1[2]
    = atmosphere.ozone_density_profile.layers[1].linear_term
    * engine::atmos::kSkyUnitToM;
  constants.ozone_density_layer1[3]
    = atmosphere.ozone_density_profile.layers[1].constant_term;
  constants.camera_planet_position_km[0]
    = view_data.sky_camera_translated_world_origin_km_pad.x
    - view_data.sky_planet_translated_world_center_km_and_view_height_km.x;
  constants.camera_planet_position_km[1]
    = view_data.sky_camera_translated_world_origin_km_pad.y
    - view_data.sky_planet_translated_world_center_km_and_view_height_km.y;
  constants.camera_planet_position_km[2]
    = view_data.sky_camera_translated_world_origin_km_pad.z
    - view_data.sky_planet_translated_world_center_km_and_view_height_km.z;
  SetVec4(constants.sky_and_aerial_luminance_factor_rgb,
    atmosphere.sky_and_aerial_perspective_luminance_factor_rgb);
  PopulateLight(constants.light0_direction_enabled,
    constants.light0_illuminance_rgb,
    stable_state.view_products.atmosphere_lights[0]);
  PopulateLight(constants.light1_direction_enabled,
    constants.light1_illuminance_rgb,
    stable_state.view_products.atmosphere_lights[1]);

  auto constants_alloc = pass_constants_buffer_.Allocate(1U);
  if (!constants_alloc.has_value()
    || !constants_alloc->TryWriteObject(constants)) {
    return state;
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(
    queue_key, "EnvironmentLightingService AtmosphereCameraAerialPerspective");
  if (!recorder) {
    return state;
  }

  TrackTextureFromKnownOrInitial(*recorder, *texture);
  recorder->RequireResourceState(
    *texture, graphics::ResourceStates::kUnorderedAccess);
  recorder->FlushBarriers();

  recorder->SetPipelineState(BuildPipelineDesc());
  recorder->SetComputeRootConstantBufferView(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
    ctx.view_constants->GetGPUVirtualAddress());
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    constants_alloc->srv.get(), 1U);

  graphics::GpuEventScope pass_scope(*recorder,
    "Vortex.Environment.AtmosphereCameraAerialPerspective",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);
  const auto dispatch_x
    = (width + (kThreadGroupSizeX - 1U)) / kThreadGroupSizeX;
  const auto dispatch_y
    = (height + (kThreadGroupSizeY - 1U)) / kThreadGroupSizeY;
  const auto dispatch_z
    = (depth + (kThreadGroupSizeZ - 1U)) / kThreadGroupSizeZ;
  recorder->Dispatch(dispatch_x, dispatch_y, dispatch_z);
  recorder->RequireResourceStateFinal(
    *texture, graphics::ResourceStates::kShaderResource);

  live_textures_.push_back(texture);
  state.executed = true;
  state.camera_aerial_perspective_srv = aerial_srv;
  state.camera_aerial_perspective_uav = aerial_uav;
  state.width = width;
  state.height = height;
  state.depth = depth;
  state.dispatch_count_x = dispatch_x;
  state.dispatch_count_y = dispatch_y;
  state.dispatch_count_z = dispatch_z;
  return state;
}

} // namespace oxygen::vortex::environment
