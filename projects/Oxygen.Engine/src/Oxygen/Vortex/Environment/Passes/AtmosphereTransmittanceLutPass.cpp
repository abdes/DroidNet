//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Passes/AtmosphereTransmittanceLutPass.h>

#include <limits>
#include <stdexcept>
#include <vector>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
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

auto BuildPipelineDesc() -> graphics::ComputePipelineDesc
{
  auto root_bindings = BuildVortexRootBindings();
  return graphics::ComputePipelineDesc::Builder {}
    .SetComputeShader(graphics::ShaderRequest {
      .stage = ShaderType::kCompute,
      .source_path = "Vortex/Services/Environment/AtmosphereTransmittanceLut.hlsl",
      .entry_point = "VortexAtmosphereTransmittanceLutCS",
    })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size()))
    .SetDebugName("Vortex.Environment.AtmosphereTransmittanceLut")
    .Build();
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

} // namespace

AtmosphereTransmittanceLutPass::AtmosphereTransmittanceLutPass(
  Renderer& renderer)
  : renderer_(renderer)
  , pass_constants_buffer_(observer_ptr { renderer.GetGraphics().get() },
      renderer.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(PassConstants)),
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "Environment.AtmosphereTransmittanceLut.PassConstants")
{
}

AtmosphereTransmittanceLutPass::~AtmosphereTransmittanceLutPass() = default;

auto AtmosphereTransmittanceLutPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  pass_constants_buffer_.OnFrameStart(sequence, slot);
}

auto AtmosphereTransmittanceLutPass::Record(RenderContext& ctx,
  const internal::StableAtmosphereState& stable_state,
  internal::AtmosphereLutCache& cache) -> RecordState
{
  auto state = RecordState {
    .requested = ctx.current_view.view_id != kInvalidViewId
      && ctx.current_view.with_atmosphere
      && stable_state.view_products.atmosphere.enabled
      && cache.NeedsTransmittanceBuild(),
  };
  if (!state.requested
    || !renderer_.HasCapability(RendererCapabilityFamily::kEnvironmentLighting)
    || !cache.EnsureResources()) {
    return state;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr || cache.GetTransmittanceTexture() == nullptr) {
    return state;
  }

  const auto& atmosphere = stable_state.view_products.atmosphere;
  const auto& cache_state = cache.GetState();
  const auto constants = PassConstants {
    .output_texture_uav = cache_state.transmittance_lut_uav.get(),
    .output_width = cache_state.internal_parameters.transmittance_width,
    .output_height = cache_state.internal_parameters.transmittance_height,
    .integration_sample_count = static_cast<std::uint32_t>(
      cache_state.internal_parameters.transmittance_sample_count),
    .planet_radius_m = atmosphere.planet_radius_m,
    .atmosphere_height_m = atmosphere.atmosphere_height_m,
    .rayleigh_scale_height_m = atmosphere.rayleigh_scale_height_m,
    .mie_scale_height_m = atmosphere.mie_scale_height_m,
    .rayleigh_scattering_rgb = {
      atmosphere.rayleigh_scattering_rgb.x,
      atmosphere.rayleigh_scattering_rgb.y,
      atmosphere.rayleigh_scattering_rgb.z,
      0.0F,
    },
    .mie_scattering_rgb = {
      atmosphere.mie_scattering_rgb.x,
      atmosphere.mie_scattering_rgb.y,
      atmosphere.mie_scattering_rgb.z,
      0.0F,
    },
    .mie_absorption_rgb = {
      atmosphere.mie_absorption_rgb.x,
      atmosphere.mie_absorption_rgb.y,
      atmosphere.mie_absorption_rgb.z,
      0.0F,
    },
    .ozone_absorption_rgb = {
      atmosphere.ozone_absorption_rgb.x,
      atmosphere.ozone_absorption_rgb.y,
      atmosphere.ozone_absorption_rgb.z,
      0.0F,
    },
    .ozone_density_layer0 = {
      atmosphere.ozone_density_profile.layers[0].width_m,
      atmosphere.ozone_density_profile.layers[0].exp_term,
      atmosphere.ozone_density_profile.layers[0].linear_term,
      atmosphere.ozone_density_profile.layers[0].constant_term,
    },
    .ozone_density_layer1 = {
      atmosphere.ozone_density_profile.layers[1].width_m,
      atmosphere.ozone_density_profile.layers[1].exp_term,
      atmosphere.ozone_density_profile.layers[1].linear_term,
      atmosphere.ozone_density_profile.layers[1].constant_term,
    },
  };
  auto constants_alloc = pass_constants_buffer_.Allocate(1U);
  if (!constants_alloc.has_value() || !constants_alloc->TryWriteObject(constants)) {
    return state;
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(
    queue_key, "EnvironmentLightingService AtmosphereTransmittanceLut");
  if (!recorder) {
    return state;
  }

  const auto& texture = *cache.GetTransmittanceTexture();
  TrackTextureFromKnownOrInitial(*recorder, texture);
  recorder->RequireResourceState(texture, graphics::ResourceStates::kUnorderedAccess);
  recorder->FlushBarriers();

  recorder->SetPipelineState(BuildPipelineDesc());
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    0U, 0U);
  recorder->SetComputeRoot32BitConstant(
    static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants),
    constants_alloc->srv.get(), 1U);

  graphics::GpuEventScope pass_scope(*recorder,
    "Vortex.Environment.AtmosphereTransmittanceLut",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);
  const auto dispatch_x = (constants.output_width + (kThreadGroupSizeX - 1U))
    / kThreadGroupSizeX;
  const auto dispatch_y = (constants.output_height + (kThreadGroupSizeY - 1U))
    / kThreadGroupSizeY;
  recorder->Dispatch(dispatch_x, dispatch_y, 1U);
  recorder->RequireResourceStateFinal(
    texture, graphics::ResourceStates::kShaderResource);

  cache.MarkTransmittanceValid();
  state.executed = true;
  state.transmittance_lut_srv = cache.GetState().transmittance_lut_srv;
  state.transmittance_lut_uav = cache.GetState().transmittance_lut_uav;
  state.width = constants.output_width;
  state.height = constants.output_height;
  state.dispatch_count_x = dispatch_x;
  state.dispatch_count_y = dispatch_y;
  state.dispatch_count_z = 1U;
  return state;
}

} // namespace oxygen::vortex::environment
