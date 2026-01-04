//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Forward+ Light Culling Compute Shader
// Performs tiled light culling for Forward+ rendering.
//
// === Bindless-only Discipline (final) ===
// - SceneConstants is bound as a root CBV at b1, space0 (authoritative include).
// - RootConstants is bound at b2, space0 (fixed ABI).
// - All SRVs/UAVs are accessed via SM 6.6 descriptor heaps using heap indices
//   stored in pass constants.
// - This shader declares zero register-bound SRVs/UAVs/samplers.

#include "Renderer/SceneConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli" // Required by BindlessHelpers.hlsl.
#include "Renderer/MaterialConstants.hlsli" // Required by BindlessHelpers.hlsl.

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

// Root constants b2 (shared root param index with engine)
// ABI layout:
//   g_DrawIndex          : unused for dispatch
//   g_PassConstantsIndex : heap index of a CBV holding pass constants
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Tile configuration for Forward+ light culling
static const uint TILE_SIZE = 16;
static const uint MAX_LIGHTS_PER_TILE = 64;

// Matches the CPU-side GPULight structure (80 bytes, 16-byte aligned)
struct GPULight {
    float3 position;            // Light position in world space
    uint type;                  // Light type: 0=directional, 1=point, 2=spot
    float3 direction;           // Light direction (for directional/spot lights)
    float range;                // Light range (for point/spot lights)
    float3 color;               // Light color (RGB)
    float intensity;            // Light intensity multiplier
    float3 attenuation;         // Attenuation coefficients (constant, linear, quadratic)
    float spotInnerCone;        // Inner cone angle (cosine) for spot lights
    float spotOuterCone;        // Outer cone angle (cosine) for spot lights
    float padding[3];           // Padding to reach 80 bytes (16-byte alignment)
};

// Pass constants for the light culling dispatch.
// This CBV is fetched via ResourceDescriptorHeap[g_PassConstantsIndex].
struct LightCullingPassConstants
{
    // Resources (heap indices)
    uint depth_texture_index;     // TEXTURES domain: Texture2D<float> depth
    uint light_buffer_index;      // GLOBAL_SRV domain: StructuredBuffer<GPULight>
    uint light_list_uav_index;    // GLOBAL_SRV domain: RWStructuredBuffer<uint>
    uint light_count_uav_index;   // GLOBAL_SRV domain: RWStructuredBuffer<uint>

    // Dispatch parameters
    float4x4 inv_projection_matrix;
    float2 screen_dimensions;     // pixels
    uint num_lights;
    uint _pad0;
};

// Group shared memory for tile-based processing
groupshared uint s_TileLightCount;
groupshared uint s_TileLightIndices[MAX_LIGHTS_PER_TILE];
groupshared float s_TileDepthMin;
groupshared float s_TileDepthMax;
groupshared float s_DepthTileMin[TILE_SIZE * TILE_SIZE];
groupshared float s_DepthTileMax[TILE_SIZE * TILE_SIZE];

// Convert screen coordinates to view space position
float3 ScreenToView(float2 screenPos, float depth, float2 screenDimensions,
                    float4x4 invProjectionMatrix) {
    // Convert screen coordinates to normalized device coordinates
    float2 ndc = (screenPos / screenDimensions) * 2.0 - 1.0;
    ndc.y = -ndc.y; // Flip Y coordinate for typical DirectX-style screen coords

    // Convert NDC to view space
    float4 viewPos = mul(invProjectionMatrix, float4(ndc, depth, 1.0));
    return viewPos.xyz / viewPos.w;
}

// Helper to compute a plane from three points (pA, pB, pC) on the plane.
// The normal is calculated from cross(pB-pA, pC-pA).
static float4 ComputeOrientedPlane(float3 pA, float3 pB, float3 pC, float3 frustumCenter) {
    float3 candNormal = normalize(cross(pB - pA, pC - pA));
    float candD = -dot(candNormal, pA);
    // If frustumCenter is on the negative side of the plane (i.e., outside), flip normal and d.
    if (dot(candNormal, frustumCenter) + candD < 0.0f) {
        return float4(-candNormal, -candD);
    }
    return float4(candNormal, candD);
}

// Calculate tile frustum bounds in view space
// Outputs 6 view-space planes with normals pointing inward into the frustum.
void CalculateTileFrustum(uint2 tileID, float2 screen_dimensions,
                          float4x4 inv_projection_matrix,
                          out float4 frustumPlanes[6]) {
    // Calculate tile bounds in screen space
    float2 tileMin = tileID * TILE_SIZE;
    float2 tileMax = (tileID + 1) * TILE_SIZE;

    // Clamp to screen bounds
    tileMin = max(tileMin, float2(0, 0));
    tileMax = min(tileMax, screen_dimensions);

    // Use shared memory depth bounds (these are normalized 0-1 depth values)
    float normMinDepth = s_TileDepthMin;
    float normMaxDepth = s_TileDepthMax;

    // Calculate frustum corner points in view space
    float3 frustumCorners[8];
    frustumCorners[0] = ScreenToView(float2(tileMin.x, tileMin.y), normMinDepth,
                                     screen_dimensions, inv_projection_matrix); // NBL
    frustumCorners[1] = ScreenToView(float2(tileMax.x, tileMin.y), normMinDepth,
                                     screen_dimensions, inv_projection_matrix); // NBR
    frustumCorners[2] = ScreenToView(float2(tileMax.x, tileMax.y), normMinDepth,
                                     screen_dimensions, inv_projection_matrix); // NTR
    frustumCorners[3] = ScreenToView(float2(tileMin.x, tileMax.y), normMinDepth,
                                     screen_dimensions, inv_projection_matrix); // NTL
    frustumCorners[4] = ScreenToView(float2(tileMin.x, tileMin.y), normMaxDepth,
                                     screen_dimensions, inv_projection_matrix); // FBL
    frustumCorners[5] = ScreenToView(float2(tileMax.x, tileMin.y), normMaxDepth,
                                     screen_dimensions, inv_projection_matrix); // FBR
    frustumCorners[6] = ScreenToView(float2(tileMax.x, tileMax.y), normMaxDepth,
                                     screen_dimensions, inv_projection_matrix); // FTR
    frustumCorners[7] = ScreenToView(float2(tileMin.x, tileMax.y), normMaxDepth,
                                     screen_dimensions, inv_projection_matrix); // FTL

    // Calculate the geometric center of the frustum, used to ensure normals point inward.
    float3 frustumCenter = (frustumCorners[0] + frustumCorners[1] + frustumCorners[2] + frustumCorners[3] +
                            frustumCorners[4] + frustumCorners[5] + frustumCorners[6] + frustumCorners[7]) * 0.125f;

    // Left plane: (NBL, FBL, NTL)
    frustumPlanes[0] = ComputeOrientedPlane(frustumCorners[0], frustumCorners[4], frustumCorners[3], frustumCenter);
    // Right plane: (NBR, NTR, FBR)
    frustumPlanes[1] = ComputeOrientedPlane(frustumCorners[1], frustumCorners[2], frustumCorners[5], frustumCenter);
    // Bottom plane: (NBL, NBR, FBL)
    frustumPlanes[2] = ComputeOrientedPlane(frustumCorners[0], frustumCorners[1], frustumCorners[4], frustumCenter);
    // Top plane: (NTL, FTL, NTR)
    frustumPlanes[3] = ComputeOrientedPlane(frustumCorners[3], frustumCorners[7], frustumCorners[2], frustumCenter);
    // Near plane: (NBL, NBR, NTL)
    frustumPlanes[4] = ComputeOrientedPlane(frustumCorners[0], frustumCorners[1], frustumCorners[3], frustumCenter);
    // Far plane: (FBL, FTL, FBR)
    frustumPlanes[5] = ComputeOrientedPlane(frustumCorners[4], frustumCorners[7], frustumCorners[5], frustumCenter);
}

// Test if a light intersects with the tile frustum
bool TestLightInFrustum(GPULight light, float4 frustumPlanes[6]) { // Removed minDepth, maxDepth parameters
    // Transform light position to view space
    float3 lightPosView = mul(view_matrix, float4(light.position, 1.0)).xyz;

    // Test light type
    if (light.type == 0) { // Directional light
        return true; // Directional lights affect all tiles
    }

    // FUTURE IMPROVEMENT: For spotlights (light.type == 2), implement more
    // accurate cone-frustum intersection instead of treating them as point
    // lights (spheres). This would involve using light.direction,
    // light.spotInnerCone, and light.spotOuterCone for a more precise culling
    // against the tile frustum.

    float lightRadius = light.range;

    // Test against all six frustum planes (sphere vs. plane).
    // Normals of frustumPlanes are assumed to point inward towards the frustum's interior.
    // A sphere is outside a plane if its signed distance from the plane is less than -radius.
    // distance = dot(plane_normal, sphere_center) + plane_d.
    for (int i = 0; i < 6; i++) {
        float distance = dot(frustumPlanes[i].xyz, lightPosView) + frustumPlanes[i].w;
        if (distance < -lightRadius) {
            return false; // Sphere is entirely outside this plane, thus outside the frustum
        }
    }

    return true; // Light intersects or is inside the frustum
}

// Main compute shader entry point
[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CS(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex) {
    // Fetch pass constants from the bindless heap.
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return;
    }
    if (!BX_IN_GLOBAL_SRV(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<LightCullingPassConstants> pass_constants =
        ResourceDescriptorHeap[g_PassConstantsIndex];

    const float2 screen_dimensions = pass_constants.screen_dimensions;
    const float4x4 inv_projection_matrix = pass_constants.inv_projection_matrix;
    const uint num_lights = pass_constants.num_lights;

    // Validate required resources.
    const uint depth_texture_index = pass_constants.depth_texture_index;
    const uint light_buffer_index = pass_constants.light_buffer_index;
    const uint light_list_uav_index = pass_constants.light_list_uav_index;
    const uint light_count_uav_index = pass_constants.light_count_uav_index;

    if (!BX_IN_TEXTURES(depth_texture_index) ||
        !BX_IN_GLOBAL_SRV(light_buffer_index) ||
        !BX_IN_GLOBAL_SRV(light_list_uav_index) ||
        !BX_IN_GLOBAL_SRV(light_count_uav_index)) {
        return;
    }

    // Calculate tile coordinates
    uint2 tileID = groupID.xy;
    uint2 pixelCoord = groupID.xy * TILE_SIZE + groupThreadID.xy;

    // Initialize shared memory on first thread
    if (groupIndex == 0) {
        s_TileLightCount = 0;
        s_TileDepthMin = 1.0f; // Far plane (assuming depth 0=near, 1=far)
        s_TileDepthMax = 0.0f; // Near plane
    }

    GroupMemoryBarrierWithGroupSync(); // Sync after init

    // Sample depth buffer to determine tile depth bounds
    float depth = 1.0f;
    if (all(pixelCoord < uint2(screen_dimensions))) { // Boundary check
        Texture2D<float> depth_tex = ResourceDescriptorHeap[depth_texture_index];
        depth = depth_tex.Load(int3(pixelCoord, 0)).r;
    }
    s_DepthTileMin[groupIndex] = depth;
    s_DepthTileMax[groupIndex] = depth;

    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction for min/max
    for (uint stride = (TILE_SIZE * TILE_SIZE) / 2; stride > 0; stride >>= 1) {
        if (groupIndex < stride) {
            s_DepthTileMin[groupIndex] = min(s_DepthTileMin[groupIndex], s_DepthTileMin[groupIndex + stride]);
            s_DepthTileMax[groupIndex] = max(s_DepthTileMax[groupIndex], s_DepthTileMax[groupIndex + stride]);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0) {
        s_TileDepthMin = clamp(s_DepthTileMin[0], 0.0f, 1.0f);
        s_TileDepthMax = clamp(s_DepthTileMax[0], 0.0f, 1.0f);
        s_TileLightCount = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    // Calculate tile frustum
    float4 frustumPlanes[6];
    CalculateTileFrustum(tileID, screen_dimensions, inv_projection_matrix, frustumPlanes);

    // Process lights in batches (each thread processes multiple lights)
    uint lightsPerThread = (num_lights + (TILE_SIZE * TILE_SIZE) - 1) / (TILE_SIZE * TILE_SIZE);
    uint startLightIndex = groupIndex * lightsPerThread;
    uint endLightIndex = min(startLightIndex + lightsPerThread, num_lights);

    // Test lights against tile frustum
    for (uint lightIndex = startLightIndex; lightIndex < endLightIndex; lightIndex++) {
        StructuredBuffer<GPULight> lights = ResourceDescriptorHeap[light_buffer_index];
        GPULight light = lights[lightIndex];

        if (TestLightInFrustum(light, frustumPlanes)) {
            uint listIndex;
            InterlockedAdd(s_TileLightCount, 1, listIndex);

            // Store light index if there's space and index is in bounds
            if (listIndex < MAX_LIGHTS_PER_TILE) {
                s_TileLightIndices[listIndex] = lightIndex;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync(); // Sync after all threads in group culled lights

    // Write results to output buffers (first thread in group)
    if (groupIndex == 0) {
        uint tiles_x = ((uint(screen_dimensions.x) + TILE_SIZE - 1) / TILE_SIZE);
        uint tileIndex = tileID.y * tiles_x + tileID.x;

        // Clamp light count to maximum
        uint finalLightCount = min(s_TileLightCount, MAX_LIGHTS_PER_TILE);

        // Write light count for this tile
        RWStructuredBuffer<uint> light_counts = ResourceDescriptorHeap[light_count_uav_index];
        light_counts[tileIndex] = finalLightCount;

        // Write light indices for this tile
        uint baseIndex = tileIndex * MAX_LIGHTS_PER_TILE;
        for (uint i = 0; i < finalLightCount; i++) {
            RWStructuredBuffer<uint> light_list = ResourceDescriptorHeap[light_list_uav_index];
            light_list[baseIndex + i] = s_TileLightIndices[i];
        }
    }
}
