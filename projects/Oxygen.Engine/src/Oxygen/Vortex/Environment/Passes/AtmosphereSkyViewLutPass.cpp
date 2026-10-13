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
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Diagnostics/Internal/GpuWorkStageTable.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereGpuPacking.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereSkyViewLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/Generated.AtmosphereContracts.h>
#include <Oxygen/Vortex/Environment/Types/AtmosphereLightModel.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/RetainedTexturePool.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::environment {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;
  using internal::PackAtmosphereLight;
  using internal::PackFloat4;
  using internal::PackOzoneDensityLayer;

  //! Byte offset of `exposure_fp16_store` in the HLSL pass constants.
  constexpr std::size_t kHlslExposureFp16StoreOffset = 44U;
  //! Opaque-use tag that keeps capture pass constants alive with the capture.
  constexpr std::uint64_t kCapturePassConstantsUse = 0x49424c434f4e5354ULL;
  //! Shorter sample-count distances are degenerate; their reciprocal is 0.
  constexpr float kMinSampleCountDistanceM = 1.0e-6F;

  constexpr std::uint32_t kThreadGroupSizeX = 8U;
  constexpr std::uint32_t kThreadGroupSizeY = 8U;

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

  auto BuildPipelineDesc(const bool capture = false)
    -> graphics::ComputePipelineDesc
  {
    auto root_bindings = vortex::internal::BuildVortexRootBindings();
    return graphics::ComputePipelineDesc::Builder {}
      .SetComputeShader(graphics::ShaderRequest {
        .stage = ShaderType::kCompute,
        .source_path = "Vortex/Services/Environment/AtmosphereSkyViewLut.hlsl",
        .entry_point = capture ? "VortexAtmosphereCaptureSkyViewLutCS"
                               : "VortexAtmosphereSkyViewLutCS",
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

} // namespace

AtmosphereSkyViewLutPass::AtmosphereSkyViewLutPass(Renderer& renderer)
  : renderer_(renderer)
  , pass_constants_buffer_(std::make_shared<upload::TransientStructuredBuffer>(
      observer_ptr { renderer.GetGraphics().get() },
      renderer.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(PassConstants)),
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "Environment.AtmosphereSkyViewLut.PassConstants"))
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
  pass_constants_buffer_->OnFrameStart(sequence, slot);
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

  state = RecordToTarget(ctx, recorder, view_data, stable_state, cache, texture,
    sky_view_srv, sky_view_uav, false);
  if (state.executed) {
    live_textures_.push_back(texture);
    texture_committed = true;
  }
  return state;
}

auto AtmosphereSkyViewLutPass::RecordCapture(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const EnvironmentViewData& view_data,
  const internal::StableAtmosphereState& stable_state,
  const internal::AtmosphereLutCache& cache,
  const std::shared_ptr<graphics::Texture>& target,
  const graphics::RegistrationLease& registration) -> RecordState
{
  const auto& state = cache.GetState();
  auto gfx = renderer_.GetGraphics();
  if (!gfx || !target || !registration
    || !stable_state.view_products.atmosphere.enabled
    || !state.transmittance_lut_valid || !state.multi_scattering_lut_valid) {
    return {};
  }
  const auto& desc = target->GetDescriptor();
  auto& registry = gfx->GetResourceRegistry();
  if (desc.format != Format::kRGBA32Float
    || desc.texture_type != TextureType::kTexture2D
    || desc.width != state.internal_parameters.sky_view_width
    || desc.height != state.internal_parameters.sky_view_height || !desc.is_uav
    || !desc.is_shader_resource
    || registry.InspectManagedIdentity(*target) != registration.Identity()) {
    return {};
  }
  const auto srv = registry.AcquireManagedView<graphics::Texture>(registration,
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_SRV, desc.format),
    bindless::generated::kTexturesDomain);
  const auto uav = registry.AcquireManagedView<graphics::Texture>(registration,
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_UAV, desc.format));
  if (!srv || !uav || !recorder.RetainRegistration(registry, registration)) {
    return {};
  }
  return RecordToTarget(ctx, recorder, view_data, stable_state, cache, target,
    srv->shader_visible_index, uav->shader_visible_index, true);
}

auto AtmosphereSkyViewLutPass::RecordToTarget(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const EnvironmentViewData& view_data,
  const internal::StableAtmosphereState& stable_state,
  const internal::AtmosphereLutCache& cache,
  const std::shared_ptr<graphics::Texture>& texture,
  const ShaderVisibleIndex sky_view_srv, const ShaderVisibleIndex sky_view_uav,
  const bool capture) -> RecordState
{
  auto state = RecordState {};
  state.requested = true;
  if (capture) {
    recorder.RetainOpaqueUse(pass_constants_buffer_, kCapturePassConstantsUse);
  }
  const auto& cache_state = cache.GetState();
  const auto width = texture->GetDescriptor().width;
  const auto height = texture->GetDescriptor().height;
  const auto& atmosphere = stable_state.view_products.atmosphere;
  static_assert(offsetof(PassConstants, dispatch_header)
      + offsetof(SkyViewDispatchHeader, exposure_fp16_store)
    == kHlslExposureFp16StoreOffset);
  auto constants = PassConstants {};
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
      > kMinSampleCountDistanceM
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
  constants.sky_view_lut_referential_row0
    = PackFloat4(glm::vec3 { view_data.sky_view_lut_referential_row0 });
  constants.sky_view_lut_referential_row1
    = PackFloat4(glm::vec3 { view_data.sky_view_lut_referential_row1 });
  constants.sky_view_lut_referential_row2
    = PackFloat4(glm::vec3 { view_data.sky_view_lut_referential_row2 });
  constants.sky_luminance_factor_rgb
    = PackFloat4(atmosphere.sky_luminance_factor_rgb);
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

  auto constants_alloc = pass_constants_buffer_->Allocate(1U);
  if (!constants_alloc.has_value()
    || !constants_alloc->TryWriteObject(constants)) {
    return state;
  }

  if (!capture && ctx.current_view.frame_exposure) {
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

  recorder.SetPipelineState(BuildPipelineDesc(capture));
  if (!capture) {
    recorder.SetComputeRootConstantBufferView(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
      ctx.view_constants.GpuAddress());
  }
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
      capture ? gpu_work::kIblAtmosphereSkyViewLutLabel
              : gpu_work::kAtmosphereSkyViewLutLabel,
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kPass);
    recorder.FlushBarriers();
    recorder.Dispatch(dispatch_x, dispatch_y, 1U);
    recorder.RequireResourceStateFinal(
      *texture, graphics::ResourceStates::kShaderResource);
    recorder.FlushBarriers();
  }

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
