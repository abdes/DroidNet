//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Passes/AtmosphereSkyViewLutPass.h>

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

  constexpr std::uint32_t kThreadGroupSizeX = 8U;
  constexpr std::uint32_t kThreadGroupSizeY = 8U;

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
        .source_path = "Vortex/Services/Environment/AtmosphereSkyViewLut.hlsl",
        .entry_point = "VortexAtmosphereSkyViewLutCS",
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName("Vortex.Environment.AtmosphereSkyViewLut")
      .Build();
  }

  auto MakeTextureViewDesc(const graphics::ResourceViewType view_type)
    -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
      .view_type = view_type,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = Format::kRGBA16Float,
      .dimension = TextureType::kTexture2D,
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

} // namespace

AtmosphereSkyViewLutPass::AtmosphereSkyViewLutPass(Renderer& renderer)
  : renderer_(renderer)
  , pass_constants_buffer_(observer_ptr { renderer.GetGraphics().get() },
      renderer.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(PassConstants)),
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "Environment.AtmosphereSkyViewLut.PassConstants")
{
}

AtmosphereSkyViewLutPass::~AtmosphereSkyViewLutPass()
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

auto AtmosphereSkyViewLutPass::OnFrameStart(
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

auto AtmosphereSkyViewLutPass::Record(RenderContext& ctx,
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

  const auto width = cache_state.internal_parameters.sky_view_width;
  const auto height = cache_state.internal_parameters.sky_view_height;
  auto texture = gfx->CreateTexture({
    .width = width,
    .height = height,
    .depth = 1U,
    .array_size = 1U,
    .mip_levels = 1U,
    .sample_count = 1U,
    .sample_quality = 0U,
    .format = Format::kRGBA16Float,
    .texture_type = TextureType::kTexture2D,
    .debug_name = "Vortex.Environment.AtmosphereSkyViewLut",
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
  texture->SetName("Vortex.Environment.AtmosphereSkyViewLut");

  auto& registry = gfx->GetResourceRegistry();
  if (!registry.Contains(*texture)) {
    registry.Register(texture);
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  auto srv_handle
    = allocator.AllocateRaw(graphics::ResourceViewType::kTexture_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!srv_handle.IsValid()) {
    throw std::runtime_error(
      "AtmosphereSkyViewLutPass: failed to allocate SRV descriptor");
  }
  const auto sky_view_srv = allocator.GetShaderVisibleIndex(srv_handle);
  registry.RegisterView(*texture, std::move(srv_handle),
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_SRV));

  auto uav_handle
    = allocator.AllocateRaw(graphics::ResourceViewType::kTexture_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!uav_handle.IsValid()) {
    throw std::runtime_error(
      "AtmosphereSkyViewLutPass: failed to allocate UAV descriptor");
  }
  const auto sky_view_uav = allocator.GetShaderVisibleIndex(uav_handle);
  registry.RegisterView(*texture, std::move(uav_handle),
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_UAV));

  const auto& atmosphere = stable_state.view_products.atmosphere;
  auto constants = PassConstants {};
  constants.output_texture_uav = sky_view_uav.get();
  constants.output_width = width;
  constants.output_height = height;
  constants.transmittance_lut_srv = cache_state.transmittance_lut_srv.get();
  constants.multi_scattering_lut_srv
    = cache_state.multi_scattering_lut_srv.get();
  constants.transmittance_width
    = cache_state.internal_parameters.transmittance_width;
  constants.transmittance_height
    = cache_state.internal_parameters.transmittance_height;
  constants.multi_scattering_width
    = cache_state.internal_parameters.multi_scattering_width;
  constants.multi_scattering_height
    = cache_state.internal_parameters.multi_scattering_height;
  constants.active_light_count
    = stable_state.view_products.atmosphere_light_count;
  constants.integration_sample_count = std::clamp(
    static_cast<std::uint32_t>(16.0F * atmosphere.trace_sample_count_scale), 4U,
    64U);
  constants.planet_radius_m = atmosphere.planet_radius_m;
  constants.atmosphere_height_m = atmosphere.atmosphere_height_m;
  constants.camera_altitude_m = view_data.planet_up_ws_camera_altitude_m.w;
  constants.trace_sample_count_scale = atmosphere.trace_sample_count_scale;
  constants.rayleigh_scale_height_m = atmosphere.rayleigh_scale_height_m;
  constants.mie_scale_height_m = atmosphere.mie_scale_height_m;
  constants.multi_scattering_factor = atmosphere.multi_scattering_factor;
  constants.mie_anisotropy = atmosphere.mie_anisotropy;
  SetVec4(constants.ground_albedo_rgb, atmosphere.ground_albedo_rgb);
  SetVec4(
    constants.rayleigh_scattering_rgb, atmosphere.rayleigh_scattering_rgb);
  SetVec4(constants.mie_scattering_rgb, atmosphere.mie_scattering_rgb);
  SetVec4(constants.mie_absorption_rgb, atmosphere.mie_absorption_rgb);
  SetVec4(constants.ozone_absorption_rgb, atmosphere.ozone_absorption_rgb);
  constants.ozone_density_layer0[0]
    = atmosphere.ozone_density_profile.layers[0].width_m;
  constants.ozone_density_layer0[1]
    = atmosphere.ozone_density_profile.layers[0].exp_term;
  constants.ozone_density_layer0[2]
    = atmosphere.ozone_density_profile.layers[0].linear_term;
  constants.ozone_density_layer0[3]
    = atmosphere.ozone_density_profile.layers[0].constant_term;
  constants.ozone_density_layer1[0]
    = atmosphere.ozone_density_profile.layers[1].width_m;
  constants.ozone_density_layer1[1]
    = atmosphere.ozone_density_profile.layers[1].exp_term;
  constants.ozone_density_layer1[2]
    = atmosphere.ozone_density_profile.layers[1].linear_term;
  constants.ozone_density_layer1[3]
    = atmosphere.ozone_density_profile.layers[1].constant_term;
  constants.sky_view_lut_referential_row0[0]
    = view_data.sky_view_lut_referential_row0.x;
  constants.sky_view_lut_referential_row0[1]
    = view_data.sky_view_lut_referential_row0.y;
  constants.sky_view_lut_referential_row0[2]
    = view_data.sky_view_lut_referential_row0.z;
  constants.sky_view_lut_referential_row1[0]
    = view_data.sky_view_lut_referential_row1.x;
  constants.sky_view_lut_referential_row1[1]
    = view_data.sky_view_lut_referential_row1.y;
  constants.sky_view_lut_referential_row1[2]
    = view_data.sky_view_lut_referential_row1.z;
  constants.sky_view_lut_referential_row2[0]
    = view_data.sky_view_lut_referential_row2.x;
  constants.sky_view_lut_referential_row2[1]
    = view_data.sky_view_lut_referential_row2.y;
  constants.sky_view_lut_referential_row2[2]
    = view_data.sky_view_lut_referential_row2.z;
  SetVec4(
    constants.sky_luminance_factor_rgb, atmosphere.sky_luminance_factor_rgb);
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
    queue_key, "EnvironmentLightingService AtmosphereSkyViewLut");
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
    "Vortex.Environment.AtmosphereSkyViewLut",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);
  const auto dispatch_x
    = (width + (kThreadGroupSizeX - 1U)) / kThreadGroupSizeX;
  const auto dispatch_y
    = (height + (kThreadGroupSizeY - 1U)) / kThreadGroupSizeY;
  recorder->Dispatch(dispatch_x, dispatch_y, 1U);
  recorder->RequireResourceStateFinal(
    *texture, graphics::ResourceStates::kShaderResource);

  live_textures_.push_back(texture);
  state.executed = true;
  state.sky_view_lut_srv = sky_view_srv;
  state.sky_view_lut_uav = sky_view_uav;
  state.width = width;
  state.height = height;
  state.dispatch_count_x = dispatch_x;
  state.dispatch_count_y = dispatch_y;
  state.dispatch_count_z = 1U;
  return state;
}

} // namespace oxygen::vortex::environment
