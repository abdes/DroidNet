//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Passes/AtmosphereSkyViewLutPass.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/CommandList.h>
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
#include <Oxygen/Vortex/Internal/RetainedTexturePool.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
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

  auto MakeTextureViewDesc(const graphics::ResourceViewType view_type,
    const Format format) -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
      .view_type = view_type,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = format,
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
  live_textures_.clear();
  if (output_pool_) {
    output_pool_->Clear();
  }
}

auto AtmosphereSkyViewLutPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  live_textures_.clear();
  if (output_pool_) {
    output_pool_->OnFrameStart(sequence);
  }
  pass_constants_buffer_.OnFrameStart(sequence, slot);
}

auto AtmosphereSkyViewLutPass::RemoveViewState(const ViewId view_id) -> void
{
  if (output_pool_) {
    output_pool_->RemoveView(view_id);
  }
}

auto AtmosphereSkyViewLutPass::Record(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const EnvironmentViewData& view_data,
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
    RemoveViewState(ctx.current_view.view_id);
    return state;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    RemoveViewState(ctx.current_view.view_id);
    return state;
  }

  if (!output_pool_) {
    output_pool_
      = std::make_unique<::oxygen::vortex::internal::RetainedTexturePool>(gfx);
    output_pool_->OnFrameStart(ctx.frame_sequence);
  }
  const auto recyclable = ctx.current_view.view_id != kInvalidViewId
    && ctx.current_view.view_state_handle
      != CompositionView::kInvalidViewStateHandle;
  const auto width = cache_state.internal_parameters.sky_view_width;
  const auto height = cache_state.internal_parameters.sky_view_height;
  const auto texture_desc = graphics::TextureDesc {
    .width = width,
    .height = height,
    .depth = 1U,
    .array_size = 1U,
    .mip_levels = 1U,
    .sample_count = 1U,
    .sample_quality = 0U,
    .format = ctx.current_view.hdr_color_format.value_or(Format::kRGBA16Float),
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
  };
  auto texture
    = output_pool_->Acquire(ctx.current_view.view_id, texture_desc, recyclable);
  if (texture == nullptr) {
    RemoveViewState(ctx.current_view.view_id);
    return state;
  }
  auto& registry = gfx->GetResourceRegistry();

  bool texture_committed = false;
  auto retire_unpublished = ScopeGuard([&]() noexcept {
    if (!texture_committed) {
      RemoveViewState(ctx.current_view.view_id);
      texture.reset();
    }
  });

  auto& allocator = gfx->GetDescriptorAllocator();
  auto srv_handle
    = allocator.AllocateBindless(bindless::generated::kTexturesDomain,
      graphics::ResourceViewType::kTexture_SRV);
  if (!srv_handle.IsValid()) {
    throw std::runtime_error(
      "AtmosphereSkyViewLutPass: failed to allocate SRV descriptor");
  }
  const auto sky_view_srv = allocator.GetShaderVisibleIndex(srv_handle);
  registry.RegisterView(*texture, std::move(srv_handle),
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_SRV,
      texture->GetDescriptor().format));

  auto uav_handle
    = allocator.AllocateBindless(bindless::generated::kTexturesDomain,
      graphics::ResourceViewType::kTexture_UAV);
  if (!uav_handle.IsValid()) {
    throw std::runtime_error(
      "AtmosphereSkyViewLutPass: failed to allocate UAV descriptor");
  }
  const auto sky_view_uav = allocator.GetShaderVisibleIndex(uav_handle);
  registry.RegisterView(*texture, std::move(uav_handle),
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_UAV,
      texture->GetDescriptor().format));

  const auto& atmosphere = stable_state.view_products.atmosphere;
  static_assert(offsetof(PassConstants, dispatch_header)
      + offsetof(DispatchHeader, exposure_status_uav)
    == 40U);
  static_assert(offsetof(PassConstants, dispatch_header)
      + offsetof(DispatchHeader, exposure_fp16_store)
    == 44U);
  auto constants = PassConstants {};
  constants.dispatch_header.exposure_status_uav
    = ctx.current_view.frame_exposure
    ? ctx.current_view.frame_exposure->current_state->status_uav_index.get()
    : kInvalidShaderVisibleIndex.get();
  constants.dispatch_header.exposure_fp16_store
    = texture->GetDescriptor().format == Format::kRGBA16Float ? 1U : 0U;
  constants.output_header.output_texture_uav = sky_view_uav.get();
  constants.output_header.output_width = width;
  constants.output_header.output_height = height;
  constants.output_header.transmittance_lut_srv
    = cache_state.transmittance_lut_srv.get();
  constants.lut_header.multi_scattering_lut_srv
    = cache_state.multi_scattering_lut_srv.get();
  constants.lut_header.transmittance_width
    = cache_state.internal_parameters.transmittance_width;
  constants.lut_header.transmittance_height
    = cache_state.internal_parameters.transmittance_height;
  constants.lut_header.multi_scattering_width
    = cache_state.internal_parameters.multi_scattering_width;
  constants.dispatch_header.multi_scattering_height
    = cache_state.internal_parameters.multi_scattering_height;
  constants.dispatch_header.active_light_count
    = stable_state.view_products.atmosphere_light_count;
  constants.sampling_atmosphere0.sample_count_min
    = cache_state.internal_parameters.sky_view_sample_count_min;
  constants.sampling_atmosphere0.sample_count_max
    = cache_state.internal_parameters.sky_view_sample_count_max;
  constants.sampling_atmosphere0.distance_to_sample_count_max_inv
    = cache_state.internal_parameters.sky_view_distance_to_sample_count_max_m
      > 1.0e-6F
    ? 1.0F
      / engine::atmos::MetersToSkyUnit(
        cache_state.internal_parameters.sky_view_distance_to_sample_count_max_m)
    : 0.0F;
  constants.sampling_atmosphere0.planet_radius_km
    = engine::atmos::MetersToSkyUnit(atmosphere.planet_radius_m);
  constants.sampling_atmosphere1.atmosphere_height_km
    = engine::atmos::MetersToSkyUnit(atmosphere.atmosphere_height_m);
  constants.sampling_atmosphere1.camera_altitude_km = std::max(
    view_data.sky_planet_translated_world_center_km_and_view_height_km.w
      - constants.sampling_atmosphere0.planet_radius_km,
    0.0F);
  constants.sampling_atmosphere1.rayleigh_scale_height_km
    = engine::atmos::MetersToSkyUnit(atmosphere.rayleigh_scale_height_m);
  constants.sampling_atmosphere1.mie_scale_height_km
    = engine::atmos::MetersToSkyUnit(atmosphere.mie_scale_height_m);
  constants.phase_factors0.multi_scattering_factor
    = atmosphere.multi_scattering_factor;
  constants.phase_factors0.mie_anisotropy = atmosphere.mie_anisotropy;
  SetVec4(constants.ground_albedo_rgb, atmosphere.ground_albedo_rgb);
  SetVec4(constants.rayleigh_scattering_per_km_rgb,
    atmosphere.rayleigh_scattering_rgb * engine::atmos::kSkyUnitToM);
  SetVec4(constants.mie_scattering_per_km_rgb,
    atmosphere.mie_scattering_rgb * engine::atmos::kSkyUnitToM);
  SetVec4(constants.mie_absorption_per_km_rgb,
    atmosphere.mie_absorption_rgb * engine::atmos::kSkyUnitToM);
  SetVec4(constants.ozone_absorption_per_km_rgb,
    atmosphere.ozone_absorption_rgb * engine::atmos::kSkyUnitToM);
  constants.ozone_density_layer0[0] = engine::atmos::MetersToSkyUnit(
    atmosphere.ozone_density_profile.layers[0].width_m);
  constants.ozone_density_layer0[1]
    = atmosphere.ozone_density_profile.layers[0].exp_term;
  constants.ozone_density_layer0[2]
    = atmosphere.ozone_density_profile.layers[0].linear_term
    * engine::atmos::kSkyUnitToM;
  constants.ozone_density_layer0[3]
    = atmosphere.ozone_density_profile.layers[0].constant_term;
  constants.ozone_density_layer1[0] = engine::atmos::MetersToSkyUnit(
    atmosphere.ozone_density_profile.layers[1].width_m);
  constants.ozone_density_layer1[1]
    = atmosphere.ozone_density_profile.layers[1].exp_term;
  constants.ozone_density_layer1[2]
    = atmosphere.ozone_density_profile.layers[1].linear_term
    * engine::atmos::kSkyUnitToM;
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

  if (ctx.current_view.frame_exposure) {
    const auto& status
      = *ctx.current_view.frame_exposure->current_state->status_buffer;
    if (!recorder.IsResourceTracked(status)
      && !recorder.AdoptKnownResourceState(status)) {
      recorder.BeginTrackingResourceState(
        status, graphics::ResourceStates::kCommon, false);
    }
    recorder.RequireResourceState(
      status, graphics::ResourceStates::kUnorderedAccess);
  }
  TrackTextureFromKnownOrInitial(recorder, *texture);
  recorder.RequireResourceState(
    *texture, graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();

  recorder.SetPipelineState(BuildPipelineDesc());
  recorder.SetComputeRootConstantBufferView(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
    ctx.view_constants->GetGPUVirtualAddress());
  recorder.SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder.SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    constants_alloc->srv.get(), 1U);

  const auto dispatch_x
    = (width + (kThreadGroupSizeX - 1U)) / kThreadGroupSizeX;
  const auto dispatch_y
    = (height + (kThreadGroupSizeY - 1U)) / kThreadGroupSizeY;
  {
    graphics::GpuEventScope pass_scope(recorder,
      "Vortex.Environment.AtmosphereSkyViewLut",
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kPass);
    recorder.Dispatch(dispatch_x, dispatch_y, 1U);
    recorder.RequireResourceState(
      *texture, graphics::ResourceStates::kShaderResource);
  }

  live_textures_.push_back(texture);
  texture_committed = true;
  state.executed = true;
  state.texture = texture;
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
