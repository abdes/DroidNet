//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"
#include "Vortex/Contracts/View/HdrStoreChecks.hlsli"
#include "Vortex/Contracts/View/HdrErrorBounds.hlsli"
#include "Vortex/Contracts/View/HdrHardwareSampling.hlsli"
#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"
#include "Vortex/Contracts/View/ViewHistoryFrameBindings.hlsli"
#include "Vortex/Services/Environment/LocalFogVolumeCommon.hlsli"
#include "Vortex/Services/Environment/TransmittanceMath.hlsli"
#include "Vortex/Services/Shadows/DirectionalShadowCommon.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"
#include "Vortex/Services/Environment/Generated.FogContracts.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VolumetricLocalFogMedia
{
    float extinction;
    float3 scattering;
    float3 emissive;
};

static float3 EvaluateDirectionalContribution(
    float4 direction_enabled,
    float4 illuminance_rgb,
    float scattering_distribution,
    float3 view_direction_to_camera,
    float shadow_visibility)
{
    if (direction_enabled.w <= 0.0f) {
        return 0.0f.xxx;
    }

    const float3 light_direction = normalize(direction_enabled.xyz);
    // Both vectors point away from the voxel; HG uses incident propagation.
    const float phase = HenyeyGreensteinPhase(
        -dot(view_direction_to_camera, light_direction),
        scattering_distribution);
    return max(illuminance_rgb.xyz, 0.0f.xxx) * phase * saturate(shadow_visibility);
}

static float3 EvaluateSkyLightContribution(VolumetricFogPassConstants pass)
{
    if (pass.sky_light0.enabled == 0u
        || pass.sky_light0.distant_sky_light_lut_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(pass.sky_light0.distant_sky_light_lut_slot))
    {
        return 0.0f.xxx;
    }

    StructuredBuffer<float4> distant_sky_light_lut =
        ResourceDescriptorHeap[pass.sky_light0.distant_sky_light_lut_slot];
    return max(distant_sky_light_lut[0].rgb, 0.0f.xxx)
        * max(pass.sky_light1.tint_rgb, 0.0f.xxx)
        * max(pass.sky_light1.intensity_mul, 0.0f)
        * max(pass.sky_light0.diffuse_intensity, 0.0f)
        * max(pass.sky_light0.volumetric_scattering_intensity, 0.0f);
}

static float ComputeDepthFromZSlice(float z_slice, float3 grid_z_params)
{
    const float scale = max(abs(grid_z_params.x), 1.0e-8f);
    const float distribution = max(abs(grid_z_params.z), 1.0e-4f);
    return (exp2(z_slice / distribution) - grid_z_params.y) / scale;
}

static float ComputeZSliceFromViewDepth(float view_depth, float3 grid_z_params)
{
    const float scale = max(abs(grid_z_params.x), 1.0e-8f);
    const float distribution = max(abs(grid_z_params.z), 1.0e-4f);
    return log2(max(view_depth * scale + grid_z_params.y, 1.0e-8f))
        * distribution;
}

static float ComputeDeviceDepthFromViewDepth(float view_depth)
{
    const float safe_depth = max(view_depth, 1.0e-4f);
    const float4 clip_position =
        mul(projection_matrix, float4(0.0f, 0.0f, -safe_depth, 1.0f));
    return abs(clip_position.w) > 1.0e-6f
        ? clip_position.z / clip_position.w
        : (reverse_z != 0u ? 0.0f : 1.0f);
}

static float ComputeCellViewDepth(VolumetricFogPassConstants pass, float z_slice)
{
    return clamp(ComputeDepthFromZSlice(z_slice, pass.grid_z.grid_z_params),
        pass.grid.start_distance_m, pass.grid.end_distance_m);
}

static float3 ComputeCellWorldPosition(
    VolumetricFogPassConstants pass, uint3 cell, float3 offset)
{
    const float2 uv = (float2(cell.xy) + saturate(offset.xy))
        / max(float2(pass.output_header.output_width, pass.output_header.output_height), 1.0f.xx);
    const float depth = ComputeCellViewDepth(pass, float(cell.z) + saturate(offset.z));
    return ReconstructWorldPosition(uv, ComputeDeviceDepthFromViewDepth(depth),
        inverse_view_projection_matrix);
}

static float EvaluateHeightFogLayerDensity(
    float density,
    float height_falloff,
    float height_offset_m,
    float world_position_z)
{
    if (density <= 0.0f)
    {
        return 0.0f;
    }

    const float exponent = clamp(
        -max(height_falloff, 0.0f) * (world_position_z - height_offset_m),
        -125.0f,
        126.0f);
    return density * exp2(exponent);
}

static float EvaluateHeightFogMediaDensity(
    VolumetricFogPassConstants pass,
    float world_position_z)
{
    if (pass.height_fog1.enabled == 0u)
    {
        return 0.0f;
    }

    const float primary_density = EvaluateHeightFogLayerDensity(
        pass.height_fog0.primary_density,
        pass.height_fog0.primary_height_falloff,
        pass.height_fog0.primary_height_offset_m,
        world_position_z);
    const float secondary_density = EvaluateHeightFogLayerDensity(
        pass.height_fog0.secondary_density,
        pass.height_fog1.secondary_height_falloff,
        pass.height_fog1.secondary_height_offset_m,
        world_position_z);
    return max(primary_density + secondary_density, 0.0f)
        * pass.height_fog1.match_height_fog_factor;
}

static bool TrySampleTemporalHistory(
    VolumetricFogPassConstants pass,
    float3 world_position,
    out float4 history_value)
{
    history_value = float4(0.0f, 0.0f, 0.0f, 1.0f);
    if (pass.temporal_history0.enabled == 0u
        || pass.temporal_history0.previous_light_scattering_srv
            == K_INVALID_BINDLESS_INDEX
        || !BX_IN_TEXTURES(
            pass.temporal_history0.previous_light_scattering_srv))
    {
        return false;
    }

    const ViewFrameBindings view_bindings =
        LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    ViewHistoryFrameBindings view_history;
    if (!TryLoadViewHistoryFrameBindings(view_bindings.history_frame_slot, view_history)
        || (view_history.validity_flags & VIEW_HISTORY_FLAG_PREVIOUS_VIEW_VALID) == 0u)
    {
        return false;
    }

    const float4 previous_view_position =
        mul(view_history.previous_view_matrix, float4(world_position, 1.0f));
    const float previous_view_depth = max(-previous_view_position.z, 1.0e-4f);
    const float previous_z_slice =
        ComputeZSliceFromViewDepth(previous_view_depth, pass.grid_z.grid_z_params);
    if (previous_z_slice < 0.0f
        || previous_z_slice >= float(pass.output_header.output_depth))
    {
        return false;
    }

    const float4 previous_clip_position =
        mul(view_history.previous_projection_matrix, previous_view_position);
    if (previous_clip_position.w <= 1.0e-5f)
    {
        return false;
    }

    const float2 previous_ndc =
        previous_clip_position.xy / previous_clip_position.w;
    const float2 previous_uv =
        previous_ndc * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
    if (any(previous_uv < 0.0f.xx) || any(previous_uv > 1.0f.xx))
    {
        return false;
    }

    Texture3D<float4> history_texture =
        ResourceDescriptorHeap[
            pass.temporal_history0.previous_light_scattering_srv];
    const SamplerState linear_sampler = SamplerDescriptorHeap[K_SAMPLER_LINEAR_CLAMP];
    const float previous_w =
        previous_z_slice / max(float(pass.output_header.output_depth), 1.0f);
    history_value =
        history_texture.SampleLevel(
            linear_sampler, float3(previous_uv, saturate(previous_w)), 0.0f);
    return true;
}

static VolumetricLocalFogMedia EvaluateLocalFogVolumeFroxelMedia(
    LocalFogVolumeInstanceData encoded_instance,
    float3 translated_world_position,
    float soft_density_scale,
    float max_density)
{
    VolumetricLocalFogMedia media = (VolumetricLocalFogMedia)0;
    const DecodedLocalFogVolumeInstanceData instance
        = DecodeLocalFogVolumeInstanceData(encoded_instance);
    const float3 unit_space_position
        = TransformTranslatedWorldPositionToLocal(instance, translated_world_position);
    const float sphere_fade = saturate(1.0f - length(unit_space_position));
    if (sphere_fade <= 0.0f)
    {
        return media;
    }

    const float height_extinction = instance.height_fog_extinction
        * exp(-instance.height_fog_falloff
            * (unit_space_position.z - instance.height_fog_offset));
    const float radial_extinction = instance.radial_fog_extinction
        * pow(sphere_fade, 0.82f);
    const float opacity = OneMinusExpNegative(max(height_extinction, 0.0f))
        * OneMinusExpNegative(max(radial_extinction, 0.0f));

    const float extinction = min(max(max_density, 0.0f),
        OpticalDepthFromOpacity(opacity)) * saturate(soft_density_scale);
    media.extinction = extinction;
    media.scattering = extinction * max(instance.albedo, 0.0f.xxx);
    media.emissive = extinction * max(instance.emissive, 0.0f.xxx);
    return media;
}

static VolumetricLocalFogMedia EvaluateLocalFogVolumesForFroxel(
    VolumetricFogPassConstants pass,
    float2 screen_uv,
    float3 translated_world_position,
    float front_distance,
    float back_distance)
{
    VolumetricLocalFogMedia accumulated = (VolumetricLocalFogMedia)0;
    if (pass.local_fog0.enabled == 0u
        || pass.local_fog0.instance_buffer_slot == K_INVALID_BINDLESS_INDEX
        || pass.local_fog0.tile_data_texture_slot == K_INVALID_BINDLESS_INDEX
        || pass.local_fog0.instance_count == 0u
        || pass.local_fog1.tile_resolution_x == 0u
        || pass.local_fog1.tile_resolution_y == 0u
        || pass.local_fog1.max_instances_per_tile == 0u
        || back_distance <= pass.local_fog1.global_start_distance_m)
    {
        return accumulated;
    }

    const float froxel_depth = max(back_distance - front_distance, 1.0e-4f);
    const float soft_density_scale
        = front_distance < pass.local_fog1.global_start_distance_m
        ? saturate((back_distance - pass.local_fog1.global_start_distance_m)
            / froxel_depth)
        : 1.0f;
    if (soft_density_scale <= 0.0f)
    {
        return accumulated;
    }

    const uint2 tile_resolution = uint2(
        pass.local_fog1.tile_resolution_x, pass.local_fog1.tile_resolution_y);
    const uint2 tile_coord = min(
        (uint2)floor(saturate(screen_uv) * float2(tile_resolution)),
        tile_resolution - 1u);

    StructuredBuffer<LocalFogVolumeInstanceData> instances
        = ResourceDescriptorHeap[pass.local_fog0.instance_buffer_slot];
    Texture2DArray<uint> tile_data_texture
        = ResourceDescriptorHeap[pass.local_fog0.tile_data_texture_slot];
    const uint tile_count = min(
        tile_data_texture[uint3(tile_coord, 0u)],
        pass.local_fog1.max_instances_per_tile);

    [loop]
    for (uint tile_index = 0u; tile_index < tile_count; ++tile_index)
    {
        const uint instance_index
            = tile_data_texture[uint3(tile_coord, 1u + tile_index)];
        if (instance_index >= pass.local_fog0.instance_count)
        {
            continue;
        }
        const VolumetricLocalFogMedia media = EvaluateLocalFogVolumeFroxelMedia(
            instances[instance_index],
            translated_world_position,
            soft_density_scale,
            pass.local_fog2.max_density_into_volumetric_fog);
        accumulated.extinction += media.extinction;
        accumulated.scattering += media.scattering;
        accumulated.emissive += media.emissive;
    }

    return accumulated;
}

// Light scattering of one froxel: the luminance the slice scatters toward the
// camera (pre-exposed) and the slice's own transmittance. The media are
// evaluated at the cell and taken as constant across the slice's segment of
// the view ray; accumulation along the ray is the integration pass's job.
static float4 EvaluateVolumetricFogSample(
    VolumetricFogPassConstants pass,
    uint3 dispatch_id,
    float3 cell_offset)
{
    const float z_base = float(dispatch_id.z);
    const float front_distance = ComputeCellViewDepth(pass, z_base);
    const float back_distance = ComputeCellViewDepth(pass, z_base + 1.0f);
    const float slice_distance = ComputeCellViewDepth(pass, z_base + 0.5f);
    const float fade_distance = max(slice_distance - pass.grid.start_distance_m, 0.0f);
    const float near_fade = pass.grid.near_fade_in_distance_m > 1.0e-3f
        ? saturate(fade_distance / pass.grid.near_fade_in_distance_m)
        : 1.0f;

    const float2 screen_uv =
        (float2(dispatch_id.xy) + saturate(cell_offset.xy))
        / max(float2(pass.output_header.output_width, pass.output_header.output_height),
            float2(1.0f, 1.0f));
    const float3 sample_world_position = ComputeCellWorldPosition(pass, dispatch_id, cell_offset);
    // The view ray's length across this slice at the sample's screen position.
    const float slice_ray_length = length(
        ComputeCellWorldPosition(pass, dispatch_id, float3(cell_offset.xy, 1.0f))
        - ComputeCellWorldPosition(pass, dispatch_id, float3(cell_offset.xy, 0.0f)));
    const float3 translated_world_position = sample_world_position - camera_position;
    const float3 camera_delta = camera_position - sample_world_position;
    const float camera_delta_length_sq = dot(camera_delta, camera_delta);
    const float3 view_direction_to_camera = camera_delta_length_sq > 1.0e-8f
        ? camera_delta * rsqrt(camera_delta_length_sq)
        : float3(0.0f, 0.0f, 1.0f);

    const VolumetricLocalFogMedia local_fog_media =
        EvaluateLocalFogVolumesForFroxel(
            pass,
            screen_uv,
            translated_world_position,
            front_distance,
            back_distance);
    const float height_fog_density =
        EvaluateHeightFogMediaDensity(pass, sample_world_position.z);
    const float height_extinction =
        height_fog_density * max(pass.grid.global_extinction_scale, 0.0f);
    const float extinction = height_extinction + max(local_fog_media.extinction, 0.0f);

    float3 directional_lighting = 0.0f.xxx;
    for (uint atmosphere_slot = 0u; atmosphere_slot < 2u; ++atmosphere_slot) {
        const float4 direction_enabled = atmosphere_slot == 0u
            ? pass.light0_direction_enabled : pass.light1_direction_enabled;
        if (direction_enabled.w <= 0.0f) continue;
        const float4 illuminance_rgb = atmosphere_slot == 0u
            ? pass.light0_illuminance_rgb : pass.light1_illuminance_rgb;
        float shadow_visibility = 1.0f;
        if (pass.grid_z.directional_shadows_enabled != 0u) {
            DirectionalLightForwardData shadow_light;
            if (TryLoadAtmosphereDirectionalLight(atmosphere_slot, shadow_light)) {
                shadow_visibility = ComputeDirectionalVolumetricShadowVisibility(
                    shadow_light.selection_index, sample_world_position, direction_enabled.xyz);
            }
        }
        directional_lighting += EvaluateDirectionalContribution(
            direction_enabled, illuminance_rgb, pass.media0.scattering_distribution,
            view_direction_to_camera, shadow_visibility);
    }

    const float3 sky_lighting = EvaluateSkyLightContribution(pass);
    // Luminance in cd/m^2 per unit phase-weighted scattering; the result is
    // pre-exposed like every other scene-colour producer.
    const float3 bounded_lighting =
        directional_lighting * pass.media1.static_lighting_scattering_intensity
        + sky_lighting;
    const float3 height_source =
        max(pass.media0.albedo_rgb, 0.0f.xxx) * bounded_lighting
        + max(pass.media1.emissive_rgb, 0.0f.xxx);
    // Local injection publishes extinction-weighted coefficients. Mix source
    // functions before applying Beer-Lambert opacity; multiplying those
    // coefficients by opacity directly would apply density twice. Form the
    // height weight first to avoid multiplying bright RGB by a large density.
    const float3 source_function = extinction > 0.0f
        ? height_source * (height_extinction / extinction)
            + (local_fog_media.scattering * bounded_lighting + local_fog_media.emissive) / extinction
        : 0.0f.xxx;
    // Energy-conserving integration across the slice ("Physically Based and
    // Unified Volumetric Rendering in Frostbite"). Near the camera the slice
    // fades in as in UE: the fade scales its scattering and its extinction.
    // 1 - exp(-x) without cancellation, so thin media keep their scattering.
    const float slice_opacity =
        saturate(OneMinusExpNegative(extinction * slice_ray_length)) * near_fade;
    const float3 slice_luminance = source_function * slice_opacity;
    return float4(max(slice_luminance, 0.0f.xxx) * GetPreExposure(),
        saturate(1.0f - slice_opacity));
}

// Light scattering per froxel, blended with the reprojected previous frame.
// The light-scattering texture and its history are FP32, so reprojection
// carries no storage error; the integrated product is certified when the
// integration pass stores it.
[shader("compute")]
[numthreads(4, 4, 4)]
void VortexVolumetricFogCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    StructuredBuffer<VolumetricFogPassConstants> pass_buffer =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    const VolumetricFogPassConstants pass = pass_buffer[0];
    if (pass.output_header.output_texture_uav == K_INVALID_BINDLESS_INDEX
        || dispatch_id.x >= pass.output_header.output_width
        || dispatch_id.y >= pass.output_header.output_height
        || dispatch_id.z >= pass.output_header.output_depth) {
        return;
    }

    RWTexture3D<float4> output_texture =
        ResourceDescriptorHeap[pass.output_header.output_texture_uav];

    float4 output_value = EvaluateVolumetricFogSample(
        pass,
        dispatch_id,
        pass.temporal_history1.frame_jitter_offsets[0].xyz);
    // History is stored on the fixed grid, independently of this frame's sample.
    const float3 history_world_position = ComputeCellWorldPosition(pass, dispatch_id, 0.5f.xxx);
    float4 history_value = output_value;
    bool certified_history = true;
    if (pass.previous_error_bounds_srv != K_INVALID_BINDLESS_INDEX) {
        // A producer-origin failure (bit 16) in the previous frame makes its
        // light scattering unusable as history.
        ByteAddressBuffer previous_status = ResourceDescriptorHeap[pass.previous_error_bounds_srv];
        certified_history = (previous_status.Load(EXPOSURE_STATUS_COMPLETED_FLAGS_OFFSET) & 16u) == 0u;
    }
    if (certified_history && TrySampleTemporalHistory(pass, history_world_position, history_value))
    {
        float inverse_previous_p = 1.0f;
        if (pass.previous_frame_exposure_srv != K_INVALID_BINDLESS_INDEX) {
            StructuredBuffer<FrameExposureData> previous_frame = ResourceDescriptorHeap[pass.previous_frame_exposure_srv];
            inverse_previous_p = previous_frame[0].one_over_pre_exposure;
        }
        history_value.rgb *= GetPreExposure() * inverse_previous_p;
        output_value = lerp(
            output_value,
            max(history_value, 0.0f.xxxx),
            saturate(pass.temporal_history0.history_weight));
        output_value.a = saturate(output_value.a);
    }
    else
    {
        const uint sample_count = clamp(
            pass.temporal_history0.history_miss_supersample_count, 1u, 16u);
        if (sample_count > 1u)
        {
            float4 accumulated = output_value;
            [loop]
            for (uint sample_index = 1u; sample_index < sample_count; ++sample_index)
            {
                accumulated += EvaluateVolumetricFogSample(
                    pass,
                    dispatch_id,
                    pass.temporal_history1.frame_jitter_offsets[sample_index].xyz);
            }
            output_value = accumulated / float(sample_count);
            output_value.a = saturate(output_value.a);
        }
    }

    CheckHdrStoreRange(output_value, 10u, GetExposureStatusUav(), 0u, GetPreExposure());
    output_texture[dispatch_id] = output_value;
}

// Accumulates the light scattering front to back along each froxel column
// into the integrated product: at every slice, the luminance scattered toward
// the camera by all the slices up to it and the transmittance through them.
// Fog consumers sample this product at a surface's depth.
[shader("compute")]
[numthreads(8, 8, 1)]
void VortexVolumetricFogIntegrateCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    StructuredBuffer<VolumetricFogPassConstants> pass_buffer =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    const VolumetricFogPassConstants pass = pass_buffer[0];
    if (pass.output_header.output_texture_uav == K_INVALID_BINDLESS_INDEX
        || pass.light_scattering_srv == K_INVALID_BINDLESS_INDEX
        || !BX_IN_TEXTURES(pass.light_scattering_srv)
        || dispatch_id.x >= pass.output_header.output_width
        || dispatch_id.y >= pass.output_header.output_height) {
        return;
    }

    Texture3D<float4> light_scattering = ResourceDescriptorHeap[pass.light_scattering_srv];
    RWTexture3D<float4> output_texture =
        ResourceDescriptorHeap[pass.output_header.output_texture_uav];

    const bool fp32_only = IsFp32OnlyExposure();
    float3 accumulated_luminance = 0.0f.xxx;
    float accumulated_transmittance = 1.0f;
    [loop]
    for (uint z = 0u; z < pass.output_header.output_depth; ++z)
    {
        const uint3 cell = uint3(dispatch_id.xy, z);
        // Slices are already nonnegative and in [0, 1] when valid. No clamp
        // here: a non-finite slice must reach the range check and the bounds
        // below instead of turning into plausible black fog.
        const float4 slice = light_scattering[cell];
        accumulated_luminance += accumulated_transmittance * slice.rgb;
        accumulated_transmittance *= slice.a;

        const float4 integrated = float4(accumulated_luminance, accumulated_transmittance);
        // The inputs are exact FP32, so the FP32 result is its own reference:
        // the only store error is the narrowing to the product's format.
        if (!fp32_only) {
            RecordHdrStoreBounds(integrated, integrated, integrated, GetOneOverPreExposure(),
                10u, GetExposureStatusUav(), pass.exposure_fp16_store);
        }
        CheckHdrStoreRange(integrated, 10u, GetExposureStatusUav(), pass.exposure_fp16_store,
            GetPreExposure());
        output_texture[cell] = pass.exposure_fp16_store != 0u
            ? HdrRoundToHalf(integrated) : integrated;
    }
}
