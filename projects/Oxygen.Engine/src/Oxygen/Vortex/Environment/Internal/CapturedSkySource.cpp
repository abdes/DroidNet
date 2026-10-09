//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <new>
#include <utility>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ManagedResource.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereView.h>
#include <Oxygen/Vortex/Environment/Internal/CapturedSkySource.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereMultiScatteringLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereSkyViewLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereTransmittanceLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/DistantSkyLightLutPass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/EnvironmentStaticData.h>

namespace oxygen::vortex::environment::internal {

struct CapturedSkySource::Impl {
  explicit Impl(Renderer& owner)
    : renderer(owner)
    , cache(owner)
    , transmittance(owner)
    , multiple(owner)
    , distant(owner)
    , sky(owner)
  {
  }

  Renderer& renderer;
  AtmosphereLutCache cache;
  AtmosphereTransmittanceLutPass transmittance;
  AtmosphereMultiScatteringLutPass multiple;
  DistantSkyLightLutPass distant;
  AtmosphereSkyViewLutPass sky;
  frame::SequenceNumber sequence { 0U };
  frame::Slot slot { frame::kInvalidSlot };
  std::shared_ptr<graphics::Texture> working_sky;
  graphics::RegistrationOwner registration;
};

CapturedSkySource::CapturedSkySource(Renderer& renderer)
  : impl_(std::make_unique<Impl>(renderer))
{
}
CapturedSkySource::~CapturedSkySource() = default;

auto CapturedSkySource::Process(RenderContext& ctx,
  const StableAtmosphereState& state, const GpuFogParams& fog,
  IblGpuProcessor& processor, const std::shared_ptr<const IblBrdfProduct>& brdf,
  const IblProcessSettings& settings, const std::uint32_t revision,
  const IblCubeView& sky_sphere_cube, const float sky_sphere_scale)
  -> std::expected<std::shared_ptr<const IblGpuProducts>, IblProcessError>
{
  const auto source
    = Prepare(ctx, state, fog, sky_sphere_cube, sky_sphere_scale);
  if (!source) {
    return std::unexpected(source.error());
  }
  return processor.ProcessSky(*source, brdf, settings, revision);
}

auto CapturedSkySource::Begin(RenderContext& ctx,
  const StableAtmosphereState& state, const GpuFogParams& fog,
  IblGpuProcessor& processor, const std::shared_ptr<const IblBrdfProduct>& brdf,
  const IblProcessSettings& settings, const std::uint32_t revision,
  const IblCubeView& sky_sphere_cube, const float sky_sphere_scale)
  -> std::expected<std::shared_ptr<IblGpuJob>, IblProcessError>
{
  const auto source
    = Prepare(ctx, state, fog, sky_sphere_cube, sky_sphere_scale);
  if (!source) {
    return std::unexpected(source.error());
  }
  return processor.BeginSky(*source, brdf, settings, revision);
}

auto CapturedSkySource::Prepare(RenderContext& ctx,
  const StableAtmosphereState& state, const GpuFogParams& fog,
  const IblCubeView& sky_sphere_cube, const float sky_sphere_scale)
  -> std::expected<IblSkySource, IblProcessError>
try {
  auto& p = *impl_;
  bool accepted = false;
  const ScopeGuard timing([&] noexcept -> void {
    if (!accepted) {
      p.renderer.GetDiagnosticsService().InvalidateIblTiming();
    }
  });
  if (p.sequence != ctx.frame_sequence || p.slot != ctx.frame_slot) {
    p.sequence = ctx.frame_sequence;
    p.slot = ctx.frame_slot;
    p.cache.OnFrameStart(p.sequence, p.slot);
    p.transmittance.OnFrameStart(p.sequence, p.slot);
    p.multiple.OnFrameStart(p.sequence, p.slot);
    p.distant.OnFrameStart(p.sequence, p.slot);
    p.sky.OnFrameStart(p.sequence, p.slot);
  }
  p.cache.RefreshForState(state);
  const auto& atmosphere = state.view_products.atmosphere;
  const auto origin = ResolveSkyCaptureOrigin(atmosphere);
  auto source = IblSkySource {};
  source.origin = { origin.x, origin.y, origin.z };
  source.environment.fog = fog;
  source.environment.atmosphere.enabled = atmosphere.enabled ? 1U : 0U;
  source.environment.atmosphere.planet_radius_km
    = engine::atmos::MetersToSkyUnit(atmosphere.planet_radius_m);
  source.environment.atmosphere.atmosphere_height_km
    = engine::atmos::MetersToSkyUnit(atmosphere.atmosphere_height_m);
  source.view = BuildAtmosphereViewData(
    state, p.cache.GetState().internal_parameters, origin, true, true);
  if (!atmosphere.enabled) {
    // The Sky Sphere's imported radiance, without its display intensity/tint.
    const auto& sphere = state.sky_sphere;
    if (sphere.enabled && (sphere.solid_color || sky_sphere_cube.texture)) {
      auto& params = source.environment.sky_sphere;
      params.enabled = 1U;
      params.source = sphere.solid_color ? kSkySphereSourceSolidColor
                                         : kSkySphereSourceCubemap;
      params.solid_color_rgb = { sphere.solid_color_rgb.x,
        sphere.solid_color_rgb.y, sphere.solid_color_rgb.z };
      params.rotation_radians = sphere.rotation_radians;
      params.intensity = sky_sphere_scale;
      if (!sphere.solid_color) {
        source.sky_sphere_cube = sky_sphere_cube;
      }
    }
    accepted = true;
    return source;
  }

  const auto transmittance = p.transmittance.Record(ctx, state, p.cache, true);
  const auto multiple = p.multiple.Record(ctx, state, p.cache, true);
  const auto distant = p.distant.Record(ctx, state, p.cache, true);
  if ((transmittance.requested && !transmittance.executed)
    || (multiple.requested && !multiple.executed)
    || (distant.requested && !distant.executed) || !p.cache.IsFullyValid()) {
    return std::unexpected(IblProcessError::kRecordingFailed);
  }
  auto graphics = p.renderer.GetGraphics();
  if (!graphics) {
    return std::unexpected(IblProcessError::kClosed);
  }
  auto& registry = graphics->GetResourceRegistry();
  const auto& quality = p.cache.GetState().internal_parameters;
  if (!p.working_sky) {
    p.working_sky = graphics->CreateTexture({
      .width = quality.sky_view_width,
      .height = quality.sky_view_height,
      .format = Format::kRGBA32Float,
      .texture_type = TextureType::kTexture2D,
      .debug_name = "Vortex.Environment.CaptureSkyViewLut",
      .is_shader_resource = true,
      .is_uav = true,
      .initial_state = graphics::ResourceStates::kCommon,
    });
    if (!p.working_sky) {
      return std::unexpected(IblProcessError::kAllocationFailed);
    }
    const auto views = std::array {
      graphics::TextureViewRequest {
        .description = { .view_type = graphics::ResourceViewType::kTexture_SRV,
          .format = Format::kRGBA32Float,
          .dimension = TextureType::kTexture2D,
          .sub_resources = graphics::TextureSubResourceSet::EntireTexture(), },
        .domain = bindless::generated::kTexturesDomain, },
      graphics::TextureViewRequest {
        .description = { .view_type = graphics::ResourceViewType::kTexture_UAV,
          .format = Format::kRGBA32Float,
          .dimension = TextureType::kTexture2D,
          .sub_resources = graphics::TextureSubResourceSet::EntireTexture(), },
        .domain = {}, },
    };
    auto managed = registry.RegisterManagedTexture(p.working_sky, views);
    if (!managed) {
      p.working_sky.reset();
      return std::unexpected(IblProcessError::kAllocationFailed);
    }
    p.registration = std::move(managed->registration);
  }
  auto registration = registry.AcquireManaged(p.registration.Identity());
  if (!registration) {
    return std::unexpected(IblProcessError::kAllocationFailed);
  }
  auto recording = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(graphics::QueueRole::kGraphics),
    "Vortex.Environment.IBL.CaptureSky", graphics::SubmissionPolicy::kExplicit);
  if (!recording) {
    return std::unexpected(IblProcessError::kRecordingFailed);
  }
  p.renderer.GetDiagnosticsService().AttachGpuTimelineCollector(*recording);
  const auto sky = p.sky.RecordCapture(
    ctx, *recording, source.view, state, p.cache, p.working_sky, *registration);
  if (!sky.executed) {
    return std::unexpected(IblProcessError::kRecordingFailed);
  }
  const auto submitted = recording.SubmitWithReceipt();
  if (submitted.outcome != graphics::SubmissionOutcome::kSubmitted
    || !submitted.receipt) {
    return std::unexpected(IblProcessError::kSubmissionFailed);
  }
  source.producer = *submitted.receipt;
  source.sky_view = sky.texture;
  source.distant_sky = p.cache.GetDistantSkyLightBuffer();
  accepted = true;
  return source;
} catch (const std::bad_alloc&) {
  return std::unexpected(IblProcessError::kAllocationFailed);
} catch (const std::exception&) {
  return std::unexpected(IblProcessError::kRecordingFailed);
}

} // namespace oxygen::vortex::environment::internal
