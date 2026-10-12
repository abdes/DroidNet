//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereGpuPacking.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereCameraAerialPerspectivePass.h>
#include <Oxygen/Vortex/Environment/Types/AtmosphereLightModel.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/RetainedTexturePool.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>

namespace oxygen::vortex::environment {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;
  using internal::PackAtmosphereLight;
  using internal::PackFloat4;
  using internal::PackOzoneDensityLayer;

  //! Byte offset of `exposure_fp16_store` in the HLSL pass constants.
  constexpr std::size_t kHlslExposureFp16StoreOffset = 108U;
  //! Shorter depth slices are degenerate; their reciprocal is published as 0.
  constexpr float kMinDepthSliceLengthKm = 1.0e-6F;

  constexpr std::uint32_t kThreadGroupSizeX = 4U;
  constexpr std::uint32_t kThreadGroupSizeY = 4U;
  constexpr std::uint32_t kThreadGroupSizeZ = 4U;

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
    auto root_bindings = vortex::internal::BuildVortexRootBindings();
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

  auto MakeTextureViewDesc(const graphics::ResourceViewType view_type,
    const Format format) -> graphics::TextureViewDescription
  {
    return graphics::TextureViewDescription {
      .view_type = view_type,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = format,
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
      renderer.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(PassConstants)),
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "Environment.AtmosphereCameraAerialPerspective.PassConstants")
{
}

AtmosphereCameraAerialPerspectivePass::~AtmosphereCameraAerialPerspectivePass()
{
  live_textures_.clear();
  if (output_pool_) {
    output_pool_->Clear();
  }
}

auto AtmosphereCameraAerialPerspectivePass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  live_textures_.clear();
  if (output_pool_) {
    output_pool_->OnFrameStart(sequence);
  }
  pass_constants_buffer_.OnFrameStart(sequence, slot);
}

auto AtmosphereCameraAerialPerspectivePass::RemoveViewState(
  const ViewId view_id) -> void
{
  if (output_pool_) {
    output_pool_->RemoveView(view_id);
  }
}

auto AtmosphereCameraAerialPerspectivePass::Record(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const EnvironmentViewData& view_data,
  const internal::StableAtmosphereState& stable_state,
  const internal::AtmosphereLutCache& cache) -> RecordState
{
  const auto& cache_state = cache.GetState();
  auto state = RecordState {};
  state.requested = ctx.current_view.view_id != kInvalidViewId
    && ctx.current_view.with_atmosphere
    && stable_state.view_products.atmosphere.enabled
    && cache_state.transmittance_lut_valid
    && cache_state.multi_scattering_lut_valid;
  if (!state.requested
    || !renderer_.HasCapability(RendererCapabilityFamily::kEnvironmentLighting)
    || !ctx.view_constants.IsValid()) {
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
  const auto width = cache_state.internal_parameters.camera_aerial_width;
  const auto height = cache_state.internal_parameters.camera_aerial_height;
  const auto depth
    = cache_state.internal_parameters.camera_aerial_depth_resolution;
  const auto texture_desc = graphics::TextureDesc {
    .width = width,
    .height = height,
    .depth = depth,
    .array_size = 1U,
    .mip_levels = 1U,
    .sample_count = 1U,
    .sample_quality = 0U,
    .format = ctx.current_view.hdr_color_format.value_or(Format::kRGBA16Float),
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
  };
  auto texture
    = output_pool_->Acquire(ctx.current_view.view_id, texture_desc, recyclable);
  if (texture == nullptr) {
    RemoveViewState(ctx.current_view.view_id);
    return state;
  }
  auto& registry = gfx->GetResourceRegistry();

  bool texture_committed = false;
  auto retire_unpublished = ScopeGuard([&] noexcept -> void {
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
    throw std::runtime_error("AtmosphereCameraAerialPerspectivePass: failed to "
                             "allocate SRV descriptor");
  }
  const auto aerial_srv = allocator.GetShaderVisibleIndex(srv_handle);
  registry.RegisterView(*texture, std::move(srv_handle),
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_SRV,
      texture->GetDescriptor().format));

  auto uav_handle
    = allocator.AllocateBindless(bindless::generated::kTexturesDomain,
      graphics::ResourceViewType::kTexture_UAV);
  if (!uav_handle.IsValid()) {
    throw std::runtime_error("AtmosphereCameraAerialPerspectivePass: failed to "
                             "allocate UAV descriptor");
  }
  const auto aerial_uav = allocator.GetShaderVisibleIndex(uav_handle);
  registry.RegisterView(*texture, std::move(uav_handle),
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_UAV,
      texture->GetDescriptor().format));

  const auto& atmosphere = stable_state.view_products.atmosphere;
  static_assert(offsetof(PassConstants, atmosphere_scales1)
      + offsetof(AtmosphereScales1, exposure_fp16_store)
    == kHlslExposureFp16StoreOffset);
  auto constants = PassConstants {};
  constants.atmosphere_scales1.exposure_fp16_store
    = texture->GetDescriptor().format == Format::kRGBA16Float ? 1U : 0U;
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
    = constants.depth_control0.depth_slice_length_km > kMinDepthSliceLengthKm
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
  constants.atmosphere_scales0.holdout
    = view_data
        .trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass.z
      > 0.5F
    ? 1U
    : 0U;
  constants.atmosphere_scales0.rayleigh_scale_height_km
    = engine::atmos::MetersToSkyUnit(atmosphere.rayleigh_scale_height_m);
  constants.atmosphere_scales1.mie_scale_height_km
    = engine::atmos::MetersToSkyUnit(atmosphere.mie_scale_height_m);
  constants.atmosphere_scales1.multi_scattering_factor
    = atmosphere.multi_scattering_factor;
  constants.atmosphere_scales1.mie_anisotropy = atmosphere.mie_anisotropy;
  constants.ground_albedo_rgb = PackFloat4(atmosphere.ground_albedo_rgb);
  constants.rayleigh_scattering_per_km_rgb = PackFloat4(
    atmosphere.rayleigh_scattering_rgb * engine::atmos::kSkyUnitToM);
  constants.mie_scattering_per_km_rgb
    = PackFloat4(atmosphere.mie_scattering_rgb * engine::atmos::kSkyUnitToM);
  constants.mie_absorption_per_km_rgb
    = PackFloat4(atmosphere.mie_absorption_rgb * engine::atmos::kSkyUnitToM);
  constants.ozone_absorption_per_km_rgb
    = PackFloat4(atmosphere.ozone_absorption_rgb * engine::atmos::kSkyUnitToM);
  constants.ozone_density_layer0
    = PackOzoneDensityLayer(atmosphere.ozone_density_profile.layers.at(0));
  constants.ozone_density_layer1
    = PackOzoneDensityLayer(atmosphere.ozone_density_profile.layers.at(1));
  constants.camera_planet_position_km = PackFloat4(
    glm::vec3 { view_data.sky_camera_translated_world_origin_km_pad }
    - glm::vec3 {
      view_data.sky_planet_translated_world_center_km_and_view_height_km });
  constants.sky_and_aerial_luminance_factor_rgb
    = PackFloat4(atmosphere.sky_and_aerial_perspective_luminance_factor_rgb);
  const auto light0
    = PackAtmosphereLight(stable_state.view_products.atmosphere_lights.at(0));
  constants.light0_direction_enabled = light0.direction_enabled;
  constants.light0_illuminance_rgb = light0.illuminance_rgb;
  const auto light1
    = PackAtmosphereLight(stable_state.view_products.atmosphere_lights.at(1));
  constants.light1_direction_enabled = light1.direction_enabled;
  constants.light1_illuminance_rgb = light1.illuminance_rgb;

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
    ctx.view_constants.GpuAddress());
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
  const auto dispatch_z
    = (depth + (kThreadGroupSizeZ - 1U)) / kThreadGroupSizeZ;
  {
    graphics::GpuEventScope pass_scope(recorder,
      "Vortex.Environment.AtmosphereCameraAerialPerspective",
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kPass);
    recorder.Dispatch(dispatch_x, dispatch_y, dispatch_z);
    recorder.RequireResourceState(
      *texture, graphics::ResourceStates::kShaderResource);
  }

  live_textures_.push_back(texture);
  texture_committed = true;
  state.executed = true;
  state.texture = texture;
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
