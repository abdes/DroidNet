//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Forward+ Light Culling Compute Shader
// Performs tiled light culling for Forward+ rendering pipeline
// Processes screen tiles (16x16 pixels) to determine which lights affect each tile

// === Bindless Rendering Contract ===
// - The engine provides a single shader-visible CBV_SRV_UAV heap.
// - Root constant buffer (b0) contains indices for bindless resource access
// - Scene constants (b1) bound directly as root CBV for per-frame data
// - All other resources accessed through bindless arrays using indices from b0
// - UAV buffers for output light lists are also part of the bindless system,
//   accessed via indices from b0

// Tile configuration for Forward+ light culling
#define TILE_SIZE 16
#define MAX_LIGHTS_PER_TILE 64

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

// Constant buffer for bindless resource access
cbuffer ResourceIndices : register(b0) {
    uint g_DepthTextureIndex;       // Global index into g_BindlessTextures[] for depth buffer
    uint g_LightBufferIndex;        // Global index into g_BindlessStructuredBuffers[] for light data
    uint g_LightListBufferIndex;    // Global index into g_BindlessRWBuffers[] for per-tile light lists
    uint g_LightCountBufferIndex;   // Global index into g_BindlessRWBuffers[] for per-tile light counts
};

// Scene constants bound directly as root CBV for performance
cbuffer SceneConstants : register(b1) {
    matrix viewMatrix;              // View matrix
    matrix projectionMatrix;        // Projection matrix
    matrix invProjectionMatrix;     // Inverse projection matrix
    float2 screenDimensions;        // Screen width and height in pixels
    uint numLights;                 // Total number of lights in the scene
    uint padding;                   // Padding for alignment
};

// Bindless resource declarations
Texture2D g_BindlessTextures[] : register(t0, space0);
StructuredBuffer<GPULight> g_BindlessStructuredBuffers[] : register(t0, space1);
RWStructuredBuffer<uint> g_BindlessRWBuffers[] : register(u0, space2);

// Group shared memory for tile-based processing
groupshared uint s_TileLightCount;
groupshared uint s_TileLightIndices[MAX_LIGHTS_PER_TILE];
groupshared float s_TileDepthMin;
groupshared float s_TileDepthMax;
groupshared float s_DepthTileMin[TILE_SIZE * TILE_SIZE];
groupshared float s_DepthTileMax[TILE_SIZE * TILE_SIZE];

// Convert screen coordinates to view space position
float3 ScreenToView(float2 screenPos, float depth) {
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
void CalculateTileFrustum(uint2 tileID, out float4 frustumPlanes[6]) {
    // Calculate tile bounds in screen space
    float2 tileMin = tileID * TILE_SIZE;
    float2 tileMax = (tileID + 1) * TILE_SIZE;

    // Clamp to screen bounds
    tileMin = max(tileMin, float2(0, 0));
    tileMax = min(tileMax, screenDimensions);

    // Use shared memory depth bounds (these are normalized 0-1 depth values)
    float normMinDepth = s_TileDepthMin;
    float normMaxDepth = s_TileDepthMax;

    // Calculate frustum corner points in view space
    float3 frustumCorners[8];
    frustumCorners[0] = ScreenToView(float2(tileMin.x, tileMin.y), normMinDepth); // Near bottom-left (NBL)
    frustumCorners[1] = ScreenToView(float2(tileMax.x, tileMin.y), normMinDepth); // Near bottom-right (NBR)
    frustumCorners[2] = ScreenToView(float2(tileMax.x, tileMax.y), normMinDepth); // Near top-right (NTR)
    frustumCorners[3] = ScreenToView(float2(tileMin.x, tileMax.y), normMinDepth); // Near top-left (NTL)
    frustumCorners[4] = ScreenToView(float2(tileMin.x, tileMin.y), normMaxDepth); // Far bottom-left (FBL)
    frustumCorners[5] = ScreenToView(float2(tileMax.x, tileMin.y), normMaxDepth); // Far bottom-right (FBR)
    frustumCorners[6] = ScreenToView(float2(tileMax.x, tileMax.y), normMaxDepth); // Far top-right (FTR)
    frustumCorners[7] = ScreenToView(float2(tileMin.x, tileMax.y), normMaxDepth); // Far top-left (FTL)

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
    float3 lightPosView = mul(viewMatrix, float4(light.position, 1.0)).xyz;

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
    if (all(pixelCoord < uint2(screenDimensions))) { // Boundary check
        depth = g_BindlessTextures[g_DepthTextureIndex].Load(int3(pixelCoord, 0)).r;
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
    CalculateTileFrustum(tileID, frustumPlanes);

    // Process lights in batches (each thread processes multiple lights)
    uint lightsPerThread = (numLights + (TILE_SIZE * TILE_SIZE) - 1) / (TILE_SIZE * TILE_SIZE);
    uint startLightIndex = groupIndex * lightsPerThread;
    uint endLightIndex = min(startLightIndex + lightsPerThread, numLights);

    // Test lights against tile frustum
    for (uint lightIndex = startLightIndex; lightIndex < endLightIndex; lightIndex++) {
        GPULight light = g_BindlessStructuredBuffers[g_LightBufferIndex][lightIndex];

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
        uint tileIndex = tileID.y * ((uint(screenDimensions.x) + TILE_SIZE - 1) / TILE_SIZE) + tileID.x;

        // Clamp light count to maximum
        uint finalLightCount = min(s_TileLightCount, MAX_LIGHTS_PER_TILE);

        // Write light count for this tile
        g_BindlessRWBuffers[g_LightCountBufferIndex][tileIndex] = finalLightCount;

        // Write light indices for this tile
        uint baseIndex = tileIndex * MAX_LIGHTS_PER_TILE;
        for (uint i = 0; i < finalLightCount; i++) {
            g_BindlessRWBuffers[g_LightListBufferIndex][baseIndex + i] = s_TileLightIndices[i];
        }
    }
}
