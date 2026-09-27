//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Passes/DistantSkyLightLutPass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::vortex::environment {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  constexpr auto kMinimumDirectionLengthSquared = 1.0e-6F;
  constexpr std::uint64_t kCapturedConstantsTag = 0x49424c434f4e5354ULL;

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
          table.count = range.num_descriptors;
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

  auto BuildPipelineDesc() -> graphics::ComputePipelineDesc
  {
    auto root_bindings = BuildVortexRootBindings();
    return graphics::ComputePipelineDesc::Builder {}
      .SetComputeShader(graphics::ShaderRequest {
        .stage = ShaderType::kCompute,
        .source_path = "Vortex/Services/Environment/DistantSkyLightLut.hlsl",
        .entry_point = "VortexDistantSkyLightLutCS",
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName("Vortex.Environment.DistantSkyLightLut")
      .Build();
  }

  auto TrackBufferFromKnownOrInitial(
    graphics::CommandRecorder& recorder, const graphics::Buffer& buffer) -> void
  {
    if (recorder.IsResourceTracked(buffer)
      || recorder.AdoptKnownResourceState(buffer)) {
      return;
    }
    recorder.BeginTrackingResourceState(
      buffer, graphics::ResourceStates::kCommon, true);
  }

  auto NormalizeLightDirection(const glm::vec3& vector) -> glm::vec3
  {
    const auto length_sq = glm::dot(vector, vector);
    if (length_sq <= kMinimumDirectionLengthSquared) {
      return { 0.0F, 0.0F, 1.0F };
    }
    return glm::normalize(vector);
  }

} // namespace

DistantSkyLightLutPass::DistantSkyLightLutPass(Renderer& renderer)
  : renderer_(renderer)
  , pass_constants_buffer_(std::make_shared<upload::TransientStructuredBuffer>(
      observer_ptr { renderer.GetGraphics().get() },
      renderer.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(PassConstants)),
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "Environment.DistantSkyLightLut.PassConstants"))
{
}

DistantSkyLightLutPass::~DistantSkyLightLutPass() = default;

auto DistantSkyLightLutPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  pass_constants_buffer_->OnFrameStart(sequence, slot);
}

auto DistantSkyLightLutPass::Record(RenderContext& ctx,
  const internal::StableAtmosphereState& stable_state,
  internal::AtmosphereLutCache& cache, const bool capture) -> RecordState
{
  auto state = RecordState {
    .requested = (capture || ctx.current_view.view_id != kInvalidViewId)
      && (capture || ctx.current_view.with_atmosphere)
      && stable_state.view_products.atmosphere.enabled
      && cache.NeedsDistantSkyLightBuild(),
  };
  if (!state.requested
    || !renderer_.HasCapability(RendererCapabilityFamily::kEnvironmentLighting)
    || !cache.EnsureResources()) {
    return state;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr || cache.GetDistantSkyLightBuffer() == nullptr) {
    return state;
  }
  if (!capture && ctx.view_constants == nullptr) {
    return state;
  }

  const auto& atmosphere = stable_state.view_products.atmosphere;
  const auto& lights = stable_state.view_products.atmosphere_lights;
  const auto light0_dir = lights.at(0).enabled
    ? NormalizeLightDirection(lights.at(0).direction_to_light_ws)
    : glm::vec3 { 0.0F, 0.0F, 1.0F };
  const auto light1_dir = lights.at(1).enabled
    ? NormalizeLightDirection(lights.at(1).direction_to_light_ws)
    : glm::vec3 { 0.0F, 0.0F, 1.0F };
  const auto light0_illuminance = lights.at(0).illuminance_rgb_lux
    * atmosphere.sky_and_aerial_perspective_luminance_factor_rgb;
  const auto light1_illuminance = lights.at(1).illuminance_rgb_lux
    * atmosphere.sky_and_aerial_perspective_luminance_factor_rgb;
  const auto& cache_state = cache.GetState();
  const auto constants = PassConstants {
    .output_buffer_uav = cache_state.distant_sky_light_lut_uav.get(),
    .transmittance_lut_srv = cache_state.transmittance_lut_srv.get(),
    .multi_scattering_lut_srv = cache_state.multi_scattering_lut_srv.get(),
    .transmittance_width = cache_state.internal_parameters.transmittance_width,
    .transmittance_height = cache_state.internal_parameters.transmittance_height,
    .multi_scattering_width = cache_state.internal_parameters.multi_scattering_width,
    .multi_scattering_height = cache_state.internal_parameters.multi_scattering_height,
    .light0_enabled = lights.at(0).enabled ? 1U : 0U,
    .light1_enabled = lights.at(1).enabled ? 1U : 0U,
    .planet_radius_km = engine::atmos::MetersToSkyUnit(
      atmosphere.planet_radius_m),
    .atmosphere_height_km = engine::atmos::MetersToSkyUnit(
      atmosphere.atmosphere_height_m),
    .sample_altitude_km = cache_state.internal_parameters.distant_sky_light_sample_altitude_km,
    .multi_scattering_factor = atmosphere.multi_scattering_factor,
    .rayleigh_scale_height_km = engine::atmos::MetersToSkyUnit(
      atmosphere.rayleigh_scale_height_m),
    .mie_scale_height_km = engine::atmos::MetersToSkyUnit(
      atmosphere.mie_scale_height_m),
    .mie_anisotropy = atmosphere.mie_anisotropy,
    ._pad0 = 0.0F,
    ._pad1 = 0.0F,
    ._pad2 = 0.0F,
    ._pad3 = 0.0F,
    .light0_direction_ws = { light0_dir.x, light0_dir.y, light0_dir.z, 0.0F },
    .light1_direction_ws = { light1_dir.x, light1_dir.y, light1_dir.z, 0.0F },
    .light0_illuminance_rgb = {
      light0_illuminance.x,
      light0_illuminance.y,
      light0_illuminance.z,
      0.0F,
    },
    .light1_illuminance_rgb = {
      light1_illuminance.x,
      light1_illuminance.y,
      light1_illuminance.z,
      0.0F,
    },
    .sky_luminance_factor_rgb = {
      cache_state.internal_parameters.sky_luminance_factor_rgb.x,
      cache_state.internal_parameters.sky_luminance_factor_rgb.y,
      cache_state.internal_parameters.sky_luminance_factor_rgb.z,
      0.0F,
    },
    .ground_albedo_rgb = {
      atmosphere.ground_albedo_rgb.x,
      atmosphere.ground_albedo_rgb.y,
      atmosphere.ground_albedo_rgb.z,
      0.0F,
    },
    .rayleigh_scattering_per_km_rgb = {
      atmosphere.rayleigh_scattering_rgb.x * engine::atmos::kSkyUnitToM,
      atmosphere.rayleigh_scattering_rgb.y * engine::atmos::kSkyUnitToM,
      atmosphere.rayleigh_scattering_rgb.z * engine::atmos::kSkyUnitToM,
      0.0F,
    },
    .mie_scattering_per_km_rgb = {
      atmosphere.mie_scattering_rgb.x * engine::atmos::kSkyUnitToM,
      atmosphere.mie_scattering_rgb.y * engine::atmos::kSkyUnitToM,
      atmosphere.mie_scattering_rgb.z * engine::atmos::kSkyUnitToM,
      0.0F,
    },
    .mie_absorption_per_km_rgb = {
      atmosphere.mie_absorption_rgb.x * engine::atmos::kSkyUnitToM,
      atmosphere.mie_absorption_rgb.y * engine::atmos::kSkyUnitToM,
      atmosphere.mie_absorption_rgb.z * engine::atmos::kSkyUnitToM,
      0.0F,
    },
    .ozone_absorption_per_km_rgb = {
      atmosphere.ozone_absorption_rgb.x * engine::atmos::kSkyUnitToM,
      atmosphere.ozone_absorption_rgb.y * engine::atmos::kSkyUnitToM,
      atmosphere.ozone_absorption_rgb.z * engine::atmos::kSkyUnitToM,
      0.0F,
    },
    .ozone_density_layer0 = {
      engine::atmos::MetersToSkyUnit(
        atmosphere.ozone_density_profile.layers.at(0).width_m),
      atmosphere.ozone_density_profile.layers.at(0).exp_term,
      atmosphere.ozone_density_profile.layers.at(0).linear_term
        * engine::atmos::kSkyUnitToM,
      atmosphere.ozone_density_profile.layers.at(0).constant_term,
    },
    .ozone_density_layer1 = {
      engine::atmos::MetersToSkyUnit(
        atmosphere.ozone_density_profile.layers.at(1).width_m),
      atmosphere.ozone_density_profile.layers.at(1).exp_term,
      atmosphere.ozone_density_profile.layers.at(1).linear_term
        * engine::atmos::kSkyUnitToM,
      atmosphere.ozone_density_profile.layers.at(1).constant_term,
    },
  };
  auto constants_alloc = pass_constants_buffer_->Allocate(1U);
  if (!constants_alloc.has_value()
    || !constants_alloc->TryWriteObject(constants)) {
    return state;
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key,
    "EnvironmentLightingService DistantSkyLightLut",
    graphics::SubmissionPolicy::kExplicit);
  if (!recorder) {
    return state;
  }
  renderer_.GetDiagnosticsService().AttachGpuTimelineCollector(*recorder);
  if (capture) {
    recorder->RetainOpaqueUse(pass_constants_buffer_, kCapturedConstantsTag);
  }

  const auto& buffer = *cache.GetDistantSkyLightBuffer();
  TrackBufferFromKnownOrInitial(*recorder, buffer);
  recorder->RequireResourceState(
    buffer, graphics::ResourceStates::kUnorderedAccess);

  recorder->SetPipelineState(BuildPipelineDesc());
  if (!capture) {
    recorder->SetComputeRootConstantBufferView(
      static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants),
      ctx.view_constants->GetGPUVirtualAddress());
  }
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants), 0U,
    0U);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    constants_alloc->srv.get(), 1U);

  {
    graphics::GpuEventScope pass_scope(*recorder,
      capture ? "Vortex.Environment.IBL.DistantSkyLightLut"
              : "Vortex.Environment.DistantSkyLightLut",
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kPass);
    recorder->FlushBarriers();
    recorder->Dispatch(1U, 1U, 1U);
    recorder->RequireResourceStateFinal(
      buffer, graphics::ResourceStates::kShaderResource);
    recorder->FlushBarriers();
  }
  const auto submission = recorder.SubmitWithReceipt();
  if (submission.outcome != graphics::SubmissionOutcome::kSubmitted
    || !submission.receipt) {
    return state;
  }
  cache.MarkDistantSkyLightValid();
  state.executed = true;
  state.distant_sky_light_lut_srv = cache.GetState().distant_sky_light_lut_srv;
  state.distant_sky_light_lut_uav = cache.GetState().distant_sky_light_lut_uav;
  state.dispatch_count_x = 1U;
  state.dispatch_count_y = 1U;
  state.dispatch_count_z = 1U;
  return state;
}

} // namespace oxygen::vortex::environment
