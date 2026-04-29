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
#include <unordered_set>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

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
  constexpr float kUeVolumetricFogHistoryWeight = 0.9F;
  constexpr std::uint32_t kUeVolumetricFogMaxHistoryMissSamples = 16U;

  auto Halton(std::uint32_t index, const std::uint32_t base) noexcept -> float
  {
    auto result = 0.0F;
    auto fraction = 1.0F / static_cast<float>(base);
    while (index > 0U) {
      result += fraction * static_cast<float>(index % base);
      index /= base;
      fraction /= static_cast<float>(base);
    }
    return result;
  }

  auto VolumetricFogTemporalRandom(
    const std::uint32_t frame_number, const bool jitter_enabled) noexcept
    -> glm::vec4
  {
    if (!jitter_enabled) {
      return { 0.5F, 0.5F, 0.5F, 0.0F };
    }

    const auto halton_index = frame_number & 1023U;
    return { Halton(halton_index, 2U), Halton(halton_index, 3U),
      Halton(halton_index, 5U), 0.0F };
  }

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

  auto ReleaseTexture(Graphics& gfx,
    std::shared_ptr<graphics::Texture>& texture,
    std::unordered_set<const graphics::Texture*>& released) -> void
  {
    if (texture == nullptr) {
      return;
    }

    const auto* raw_texture = texture.get();
    if (!released.insert(raw_texture).second) {
      texture.reset();
      return;
    }

    auto& registry = gfx.GetResourceRegistry();
    if (registry.Contains(*texture)) {
      registry.UnRegisterResource(*texture);
    }
    gfx.RegisterDeferredRelease(std::move(texture));
  }

  auto ReleaseLiveTextures(Graphics& gfx,
    std::vector<std::shared_ptr<graphics::Texture>>& live_textures,
    std::unordered_set<const graphics::Texture*>& released) -> void
  {
    for (auto& texture : live_textures) {
      ReleaseTexture(gfx, texture, released);
    }
    live_textures.clear();
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

  auto NearlyEqual(const float left, const float right) -> bool
  {
    return std::abs(left - right) <= 1.0e-4F;
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
    live_textures_.clear();
    history_by_view_.clear();
    return;
  }

  std::unordered_set<const graphics::Texture*> released;
  ReleaseLiveTextures(*gfx, live_textures_, released);
  for (auto& [_, history] : history_by_view_) {
    ReleaseTexture(*gfx, history.texture, released);
  }
  history_by_view_.clear();
}

auto VolumetricFogPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  auto gfx = renderer_.GetGraphics();
  if (gfx != nullptr) {
    std::unordered_set<const graphics::Texture*> released;
    ReleaseLiveTextures(*gfx, live_textures_, released);
  } else {
    live_textures_.clear();
  }
  pass_constants_buffer_.OnFrameStart(sequence, slot);
}

auto VolumetricFogPass::Record(RenderContext& ctx,
  const internal::StableAtmosphereState& stable_state,
  const ShaderVisibleIndex distant_sky_light_lut_srv,
  const internal::LocalFogVolumeState::ViewProducts* local_fog_products)
  -> RecordState
{
  const auto& volumetric = stable_state.view_products.volumetric_fog;
  auto state = RecordState {
    .requested = ctx.current_view.view_id != kInvalidViewId
      && ctx.current_view.with_height_fog && volumetric.enabled
      && ctx.current_view.resolved_view != nullptr,
  };
  if (!state.requested
    || !renderer_.HasCapability(RendererCapabilityFamily::kEnvironmentLighting)) {
    if (ctx.current_view.view_id != kInvalidViewId) {
      if (auto it = history_by_view_.find(ctx.current_view.view_id);
        it != history_by_view_.end()) {
        if (it->second.texture != nullptr) {
          live_textures_.push_back(it->second.texture);
        }
        history_by_view_.erase(it);
      }
    }
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
  const auto& sky_light = stable_state.view_products.sky_light;
  const auto start_distance = std::max(volumetric.start_distance, 0.0F);
  const auto fallback_distance = ctx.current_view.resolved_view != nullptr
    ? std::max(ctx.current_view.resolved_view->FarPlane(),
        start_distance + 1.0F)
    : kDefaultVolumetricDistanceMeters;
  const auto end_distance = std::max(
    volumetric.distance > start_distance ? volumetric.distance
                                         : fallback_distance,
    start_distance + 1.0F);
  const auto height_fog_media_requested = height_fog.enabled
    && height_fog.enable_height_fog
    && (height_fog.fog_density > 0.0F || height_fog.second_fog_density > 0.0F);
  const auto sky_light_injection_requested = sky_light.enabled
    && sky_light.volumetric_scattering_intensity > 0.0F
    && sky_light.diffuse_intensity > 0.0F;
  const auto sky_light_injection_ready
    = sky_light_injection_requested && distant_sky_light_lut_srv.IsValid();

  auto constants = PassConstants {};
  constants.output_header.output_texture_uav = integrated_uav.get();
  constants.output_header.output_width = width;
  constants.output_header.output_height = height;
  constants.output_header.output_depth = depth;
  constants.grid.start_distance_m = start_distance;
  constants.grid.end_distance_m = end_distance;
  constants.grid.near_fade_in_distance_m
    = std::max(volumetric.near_fade_in_distance, 0.0F);
  constants.grid.global_extinction_scale
    = std::max(volumetric.extinction_scale, 0.0F);
  const auto grid_z_params = CalculateUeGridZParams(start_distance,
    resolved_view.NearPlane(), end_distance, depth);
  const auto temporal_reprojection_enabled
    = renderer_.GetVolumetricFogTemporalReprojectionEnabled();
  const auto temporal_jitter_enabled
    = temporal_reprojection_enabled && renderer_.GetVolumetricFogJitterEnabled();
  const auto history_miss_supersample_count
    = std::min(renderer_.GetVolumetricFogHistoryMissSupersampleCount(),
      kUeVolumetricFogMaxHistoryMissSamples);
  for (std::uint32_t sample_index = 0U;
    sample_index < kUeVolumetricFogMaxHistoryMissSamples; ++sample_index) {
    const auto sample_frame = ctx.frame_sequence.get() > sample_index
      ? static_cast<std::uint32_t>(ctx.frame_sequence.get() - sample_index)
      : 0U;
    const auto jitter = VolumetricFogTemporalRandom(
      sample_frame, temporal_jitter_enabled);
    constants.temporal_history1.frame_jitter_offsets[sample_index][0] = jitter.x;
    constants.temporal_history1.frame_jitter_offsets[sample_index][1] = jitter.y;
    constants.temporal_history1.frame_jitter_offsets[sample_index][2] = jitter.z;
    constants.temporal_history1.frame_jitter_offsets[sample_index][3] = jitter.w;
  }
  auto& history_entry = history_by_view_[ctx.current_view.view_id];
  const auto history_matches = temporal_reprojection_enabled && history_entry.valid
    && history_entry.texture != nullptr && history_entry.srv.IsValid()
    && history_entry.width == width && history_entry.height == height
    && history_entry.depth == depth
    && NearlyEqual(history_entry.start_distance_m, start_distance)
    && NearlyEqual(history_entry.end_distance_m, end_distance)
    && NearlyEqual(history_entry.grid_z_params[0], grid_z_params.x)
    && NearlyEqual(history_entry.grid_z_params[1], grid_z_params.y)
    && NearlyEqual(history_entry.grid_z_params[2], grid_z_params.z);
  if (history_matches) {
    constants.temporal_history0.previous_integrated_light_scattering_srv
      = history_entry.srv.get();
    constants.temporal_history0.enabled = 1U;
    constants.temporal_history0.history_weight
      = kUeVolumetricFogHistoryWeight;
  }
  constants.temporal_history0.history_miss_supersample_count
    = temporal_reprojection_enabled ? history_miss_supersample_count : 1U;
  constants.grid_z.grid_z_params[0] = grid_z_params.x;
  constants.grid_z.grid_z_params[1] = grid_z_params.y;
  constants.grid_z.grid_z_params[2] = grid_z_params.z;
  const auto directional_volumetric_shadows_enabled
    = renderer_.GetVolumetricFogDirectionalShadowsEnabled();
  const auto shadowed_directional_light0_enabled
    = directional_volumetric_shadows_enabled
    && stable_state.view_products.atmosphere_lights[0].enabled;
  constants.grid_z.shadowed_directional_light0_enabled
    = shadowed_directional_light0_enabled ? 1.0F : 0.0F;
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
  if (height_fog_media_requested) {
    constants.height_fog0.primary_density
      = std::max(height_fog.fog_density, 0.0F);
    constants.height_fog0.primary_height_falloff
      = std::max(height_fog.fog_height_falloff, 0.0F);
    constants.height_fog0.primary_height_offset_m
      = height_fog.fog_height_offset;
    constants.height_fog0.secondary_density
      = std::max(height_fog.second_fog_density, 0.0F);
    constants.height_fog1.secondary_height_falloff
      = std::max(height_fog.second_fog_height_falloff, 0.0F);
    constants.height_fog1.secondary_height_offset_m
      = height_fog.second_fog_height_offset;
    constants.height_fog1.match_height_fog_factor = 0.5F;
    constants.height_fog1.enabled = 1U;
  }
  if (sky_light_injection_ready) {
    constants.sky_light0.distant_sky_light_lut_slot
      = distant_sky_light_lut_srv.get();
    constants.sky_light0.enabled = 1U;
    constants.sky_light0.volumetric_scattering_intensity
      = std::max(sky_light.volumetric_scattering_intensity, 0.0F);
    constants.sky_light0.diffuse_intensity
      = std::max(sky_light.diffuse_intensity, 0.0F);
    constants.sky_light1.tint_rgb[0] = sky_light.tint_rgb.x;
    constants.sky_light1.tint_rgb[1] = sky_light.tint_rgb.y;
    constants.sky_light1.tint_rgb[2] = sky_light.tint_rgb.z;
    constants.sky_light1.intensity_mul = std::max(sky_light.intensity_mul, 0.0F);
  }
  const auto local_fog_ready = renderer_.GetLocalFogRenderIntoVolumetricFog()
    && local_fog_products != nullptr && local_fog_products->prepared
    && local_fog_products->buffer_ready && local_fog_products->tile_data_ready
    && local_fog_products->instance_buffer_slot.IsValid()
    && local_fog_products->tile_data_texture_slot.IsValid()
    && local_fog_products->instance_count > 0U
    && local_fog_products->tile_resolution_x > 0U
    && local_fog_products->tile_resolution_y > 0U
    && local_fog_products->max_instances_per_tile > 0U;
  if (local_fog_ready) {
    constants.local_fog0.instance_buffer_slot
      = local_fog_products->instance_buffer_slot.get();
    constants.local_fog0.tile_data_texture_slot
      = local_fog_products->tile_data_texture_slot.get();
    constants.local_fog0.instance_count = local_fog_products->instance_count;
    constants.local_fog0.enabled = 1U;
    constants.local_fog1.tile_resolution_x
      = local_fog_products->tile_resolution_x;
    constants.local_fog1.tile_resolution_y
      = local_fog_products->tile_resolution_y;
    constants.local_fog1.max_instances_per_tile
      = local_fog_products->max_instances_per_tile;
    constants.local_fog1.global_start_distance_m
      = renderer_.GetLocalFogGlobalStartDistanceMeters();
    constants.local_fog2.max_density_into_volumetric_fog
      = renderer_.GetLocalFogMaxDensityIntoVolumetricFog();
  }
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
  state.height_fog_media_requested = height_fog_media_requested;
  state.height_fog_media_executed = constants.height_fog1.enabled != 0U;
  state.sky_light_injection_requested = sky_light_injection_requested;
  state.sky_light_injection_executed = constants.sky_light0.enabled != 0U;
  state.temporal_history_requested = temporal_reprojection_enabled;
  state.temporal_history_reprojection_executed
    = constants.temporal_history0.enabled != 0U;
  state.temporal_history_reset = temporal_reprojection_enabled
    && !state.temporal_history_reprojection_executed;
  state.local_fog_injection_requested = renderer_.GetLocalFogRenderIntoVolumetricFog()
    && local_fog_products != nullptr && local_fog_products->prepared;
  state.local_fog_injection_executed = local_fog_ready;
  state.local_fog_instance_count
    = local_fog_products != nullptr ? local_fog_products->instance_count : 0U;
  state.grid_z_params[0] = grid_z_params.x;
  state.grid_z_params[1] = grid_z_params.y;
  state.grid_z_params[2] = grid_z_params.z;
  if (temporal_reprojection_enabled) {
    if (history_entry.texture != nullptr) {
      live_textures_.push_back(history_entry.texture);
    }
    history_entry.texture = texture;
    history_entry.srv = integrated_srv;
    history_entry.width = width;
    history_entry.height = height;
    history_entry.depth = depth;
    history_entry.start_distance_m = start_distance;
    history_entry.end_distance_m = end_distance;
    history_entry.grid_z_params[0] = grid_z_params.x;
    history_entry.grid_z_params[1] = grid_z_params.y;
    history_entry.grid_z_params[2] = grid_z_params.z;
    history_entry.valid = true;
  } else {
    if (history_entry.texture != nullptr) {
      live_textures_.push_back(history_entry.texture);
    }
    history_entry = HistoryEntry {};
  }
  return state;
}

} // namespace oxygen::vortex::environment
