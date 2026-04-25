//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Passes/VolumetricFogPass.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Types/ResolvedView.h>
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

  constexpr std::uint32_t kThreadGroupSizeX = 4U;
  constexpr std::uint32_t kThreadGroupSizeY = 4U;
  constexpr std::uint32_t kThreadGroupSizeZ = 4U;
  constexpr std::uint32_t kTileSizePixels = 8U;
  constexpr std::uint32_t kDepthResolution = 32U;
  constexpr float kDefaultVolumetricDistanceMeters = 100000.0F;
  constexpr float kUeVolumetricFogDepthDistributionScale = 32.0F;
  constexpr float kUeFroxelNearOffsetMeters = 9.5F;

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

  auto BuildPipelineDesc() -> graphics::ComputePipelineDesc
  {
    auto root_bindings = BuildVortexRootBindings();
    return graphics::ComputePipelineDesc::Builder {}
      .SetComputeShader(graphics::ShaderRequest {
        .stage = ShaderType::kCompute,
        .source_path = "Vortex/Services/Environment/VolumetricFog.hlsl",
        .entry_point = "VortexVolumetricFogCS",
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName("Vortex.Environment.VolumetricFog")
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

  auto SetVec4(float (&target)[4], const glm::vec3 value, const float w = 0.0F)
    -> void
  {
    target[0] = value.x;
    target[1] = value.y;
    target[2] = value.z;
    target[3] = w;
  }

  auto PopulateLight(float (&direction_enabled)[4], float (&illuminance_rgb)[4],
    const AtmosphereLightModel& light) -> void
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
    SetVec4(illuminance_rgb,
      light.enabled ? light.illuminance_rgb_lux
                    : glm::vec3 { 0.0F, 0.0F, 0.0F });
  }

  auto ResolveGridWidth(const ResolvedView& view) -> std::uint32_t
  {
    return std::max(1U,
      (static_cast<std::uint32_t>(std::ceil(view.Viewport().width))
        + (kTileSizePixels - 1U))
        / kTileSizePixels);
  }

  auto ResolveGridHeight(const ResolvedView& view) -> std::uint32_t
  {
    return std::max(1U,
      (static_cast<std::uint32_t>(std::ceil(view.Viewport().height))
        + (kTileSizePixels - 1U))
        / kTileSizePixels);
  }

  auto CalculateUeGridZParams(const float start_distance_m,
    const float near_plane_m, const float far_plane_m,
    const std::uint32_t grid_size_z) -> glm::vec3
  {
    const auto near_plane = static_cast<double>(
      std::max(near_plane_m, start_distance_m));
    const auto slice_count = static_cast<double>(std::max(grid_size_z, 1U));
    const auto depth_distribution
      = static_cast<double>(kUeVolumetricFogDepthDistributionScale);
    const auto near_with_offset
      = near_plane + static_cast<double>(kUeFroxelNearOffsetMeters);
    const auto far_plane = static_cast<double>(
      std::max(far_plane_m, static_cast<float>(near_with_offset + 1.0)));
    const auto far_minus_near = std::max(far_plane - near_with_offset, 1.0e-6);
    const auto depth_exp = std::exp2(slice_count / depth_distribution);
    const auto offset = (far_plane - near_with_offset * depth_exp)
      / far_minus_near;
    const auto scale = (1.0 - offset) / near_with_offset;

    return glm::vec3 { static_cast<float>(scale),
      static_cast<float>(offset),
      kUeVolumetricFogDepthDistributionScale };
  }

} // namespace

VolumetricFogPass::VolumetricFogPass(Renderer& renderer)
  : renderer_(renderer)
  , pass_constants_buffer_(observer_ptr { renderer.GetGraphics().get() },
      renderer.GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(PassConstants)),
      observer_ptr { &renderer.GetInlineTransfersCoordinator() },
      "Environment.VolumetricFog.PassConstants")
{
}

VolumetricFogPass::~VolumetricFogPass()
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

auto VolumetricFogPass::OnFrameStart(
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

auto VolumetricFogPass::Record(RenderContext& ctx,
  const internal::StableAtmosphereState& stable_state) -> RecordState
{
  const auto& volumetric = stable_state.view_products.volumetric_fog;
  auto state = RecordState {
    .requested = ctx.current_view.view_id != kInvalidViewId
      && ctx.current_view.with_height_fog && volumetric.enabled
      && ctx.current_view.resolved_view != nullptr,
  };
  if (!state.requested
    || !renderer_.HasCapability(RendererCapabilityFamily::kEnvironmentLighting)) {
    return state;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return state;
  }
  if (ctx.view_constants == nullptr) {
    return state;
  }

  const auto& resolved_view = *ctx.current_view.resolved_view;
  const auto width = ResolveGridWidth(resolved_view);
  const auto height = ResolveGridHeight(resolved_view);
  const auto depth = kDepthResolution;
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
    .debug_name = "Vortex.Environment.IntegratedLightScattering",
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
  texture->SetName("Vortex.Environment.IntegratedLightScattering");

  auto& registry = gfx->GetResourceRegistry();
  if (!registry.Contains(*texture)) {
    registry.Register(texture);
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  auto srv_handle = allocator.AllocateBindless(
    bindless::generated::kTexturesDomain,
    graphics::ResourceViewType::kTexture_SRV);
  if (!srv_handle.IsValid()) {
    throw std::runtime_error(
      "VolumetricFogPass: failed to allocate SRV descriptor");
  }
  const auto integrated_srv = allocator.GetShaderVisibleIndex(srv_handle);
  registry.RegisterView(*texture, std::move(srv_handle),
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_SRV));

  auto uav_handle = allocator.AllocateBindless(
    bindless::generated::kTexturesDomain,
    graphics::ResourceViewType::kTexture_UAV);
  if (!uav_handle.IsValid()) {
    throw std::runtime_error(
      "VolumetricFogPass: failed to allocate UAV descriptor");
  }
  const auto integrated_uav = allocator.GetShaderVisibleIndex(uav_handle);
  registry.RegisterView(*texture, std::move(uav_handle),
    MakeTextureViewDesc(graphics::ResourceViewType::kTexture_UAV));

  const auto& height_fog = stable_state.view_products.height_fog;
  const auto start_distance = std::max(volumetric.start_distance, 0.0F);
  const auto fallback_distance = ctx.current_view.resolved_view != nullptr
    ? std::max(ctx.current_view.resolved_view->FarPlane(),
        start_distance + 1.0F)
    : kDefaultVolumetricDistanceMeters;
  const auto end_distance = std::max(
    volumetric.distance > start_distance ? volumetric.distance
                                         : fallback_distance,
    start_distance + 1.0F);
  const auto base_extinction = std::max(height_fog.fog_density, 0.000001F)
    * std::max(volumetric.extinction_scale, 0.0F);

  auto constants = PassConstants {};
  constants.output_header.output_texture_uav = integrated_uav.get();
  constants.output_header.output_width = width;
  constants.output_header.output_height = height;
  constants.output_header.output_depth = depth;
  constants.grid.start_distance_m = start_distance;
  constants.grid.end_distance_m = end_distance;
  constants.grid.near_fade_in_distance_m
    = std::max(volumetric.near_fade_in_distance, 0.0F);
  constants.grid.base_extinction_per_m = base_extinction;
  const auto grid_z_params = CalculateUeGridZParams(start_distance,
    resolved_view.NearPlane(), end_distance, depth);
  constants.grid_z.grid_z_params[0] = grid_z_params.x;
  constants.grid_z.grid_z_params[1] = grid_z_params.y;
  constants.grid_z.grid_z_params[2] = grid_z_params.z;
  constants.grid_z.shadowed_directional_light0_enabled
    = stable_state.view_products.atmosphere_lights[0].enabled ? 1.0F : 0.0F;
  constants.media0.albedo_rgb[0] = volumetric.albedo.x;
  constants.media0.albedo_rgb[1] = volumetric.albedo.y;
  constants.media0.albedo_rgb[2] = volumetric.albedo.z;
  constants.media0.scattering_distribution
    = std::clamp(volumetric.scattering_distribution, -0.99F, 0.99F);
  constants.media1.emissive_rgb[0] = volumetric.emissive.x;
  constants.media1.emissive_rgb[1] = volumetric.emissive.y;
  constants.media1.emissive_rgb[2] = volumetric.emissive.z;
  constants.media1.static_lighting_scattering_intensity
    = std::max(volumetric.static_lighting_scattering_intensity, 0.0F);
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
  auto recorder
    = gfx->AcquireCommandRecorder(queue_key, "EnvironmentLightingService VolumetricFog");
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
    "Vortex.Stage14.VolumetricFog",
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
  state.integrated_light_scattering_srv = integrated_srv;
  state.integrated_light_scattering_uav = integrated_uav;
  state.width = width;
  state.height = height;
  state.depth = depth;
  state.dispatch_count_x = dispatch_x;
  state.dispatch_count_y = dispatch_y;
  state.dispatch_count_z = dispatch_z;
  state.start_distance_m = start_distance;
  state.end_distance_m = end_distance;
  state.view_constants_bound = true;
  state.ue_log_depth_distribution = true;
  state.directional_shadowed_light_injection_requested
    = constants.grid_z.shadowed_directional_light0_enabled > 0.0F;
  state.grid_z_params[0] = grid_z_params.x;
  state.grid_z_params[1] = grid_z_params.y;
  state.grid_z_params[2] = grid_z_params.z;
  return state;
}

} // namespace oxygen::vortex::environment
