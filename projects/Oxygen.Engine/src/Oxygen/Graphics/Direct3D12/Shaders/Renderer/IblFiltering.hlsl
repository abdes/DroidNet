//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/SceneConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli" // Required by BindlessHelpers.hlsl.
#include "Renderer/MaterialConstants.hlsli" // Required by BindlessHelpers.hlsl.

#include "Renderer/EnvironmentHelpers.hlsli"
#include "Passes/Forward/ForwardPbr.hlsli"

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

// Pass constants for IBL filtering.
// This CBV is fetched via ResourceDescriptorHeap[g_PassConstantsIndex].
// Layout must match C++ IblFilteringPassConstants.
struct IblFilteringPassConstants {
    uint source_cubemap_slot;
    uint target_uav_slot; // Texture2DArray UAV (faces as array slices)
    float roughness;
    uint face_size; // Dimensions of the target face
    float source_intensity;
    float3 _pad0;
};

// -----------------------------------------------------------------------------
// Constants & Helper Math
// -----------------------------------------------------------------------------

static const float PI = 3.14159265359;

// Hammersley Sequence
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);

    // Tangent space H vector
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Tangent space to World space
    float3 Up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 Tangent = normalize(cross(Up, N));
    float3 Bitangent = cross(N, Tangent);

    float3 sampleVec = Tangent * H.x + Bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// Map DispatchThreadID to a direction on the cubemap face.
// Oxygen follows Z-up (right-handed), but standard D3D cubemaps are usually:
// Face 0: +X, Face 1: -X, Face 2: +Y, Face 3: -Y, Face 4: +Z, Face 5: -Z
// We need to verify if we are writing into a Texture2DArray representing these faces directly.
// And crucially, we need to sample the SOURCE cubemap using the correct coordinate system.
float3 GetCubemapDirection(float2 uv, uint face_idx)
{
    // Match the cubemap cooking convention used by ConvertEquirectangularToCube
    // (ComputeCubeDirectionD3D / kGpuCubeFaceBases):
    // - u in [0,1] maps to s in [-1,1] left->right
    // - v in [0,1] is TOP->BOTTOM in texture space, so map to t in [+1,-1]
    const float s = uv.x * 2.0 - 1.0;
    const float t = 1.0 - uv.y * 2.0;

    float3 dir = 0.0;
    switch (face_idx) {
        case 0: dir = float3(+1.0,  t,   -s); break; // +X
        case 1: dir = float3(-1.0,  t,   +s); break; // -X
        case 2: dir = float3(  s,  +1.0, -t); break; // +Y
        case 3: dir = float3(  s,  -1.0, +t); break; // -Y
        case 4: dir = float3(  s,   t,  +1.0); break; // +Z
        case 5: dir = float3( -s,   t,  -1.0); break; // -Z
    }
    return normalize(dir);
}

// -----------------------------------------------------------------------------
// Compute Shaders
// -----------------------------------------------------------------------------

[numthreads(8, 8, 1)]
void CS_IrradianceConvolution(uint3 DTid : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) return;

    StructuredBuffer<IblFilteringPassConstants> pass_buffer = ResourceDescriptorHeap[g_PassConstantsIndex];
    IblFilteringPassConstants pass = pass_buffer[g_DrawIndex];

    uint width, height, elements;
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[pass.target_uav_slot];
    output.GetDimensions(width, height, elements);

    if (DTid.x >= width || DTid.y >= height || DTid.z >= 6) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(width, height);
    float3 N = GetCubemapDirection(uv, DTid.z);

    // Tangent space for the hemisphere
    float3 Up = float3(0.0, 1.0, 0.0);
    float3 Right = cross(Up, N);

    // Handle singularity
    if (length(Right) < 0.001) {
        Up = float3(1.0, 0.0, 0.0);
        Right = cross(Up, N);
    }
    Right = normalize(Right);
    Up = normalize(cross(N, Right));

    TextureCube<float4> source = ResourceDescriptorHeap[pass.source_cubemap_slot];
    SamplerState linearSampler = SamplerDescriptorHeap[0];

    const float source_scale = pass.source_intensity;

    float3 irradiance = 0.0;
    uint samples = 0;

    // Stride for convolution to keep performance reasonable for real-time
    float sampleDelta = 0.025;
    float nrSamples = 0.0;

    // Basic convolution: integrate over hemisphere
    for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // Spherical to Cartesian (in tangent space)
            float3 tangentSample = float3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));

            // Tangent to World
            float3 sampleVec = tangentSample.x * Right + tangentSample.y * Up + tangentSample.z * N;

            // Sample Source
            // Both source and target cubemaps are now consistently in D3D sampling space.
            irradiance += source_scale
                * source.SampleLevel(linearSampler, sampleVec, 0).rgb
                * cos(theta) * sin(theta);
            nrSamples++;
        }
    }

    // Standard normalization for cosine-weighted hemisphere integration:
    // Result = (1/N) * sum(radiance * cos(theta))
    irradiance = irradiance * (1.0 / float(nrSamples));

    output[DTid] = float4(irradiance, 1.0);
}

[numthreads(8, 8, 1)]
void CS_SpecularPrefilter(uint3 DTid : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) return;

    StructuredBuffer<IblFilteringPassConstants> pass_buffer = ResourceDescriptorHeap[g_PassConstantsIndex];
    IblFilteringPassConstants pass = pass_buffer[g_DrawIndex];

    uint width, height, elements;
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[pass.target_uav_slot];
    output.GetDimensions(width, height, elements);

    if (DTid.x >= width || DTid.y >= height || DTid.z >= 6) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(width, height);
    float3 N = GetCubemapDirection(uv, DTid.z);
    float3 R = N;
    float3 V = R;

    TextureCube<float4> source = ResourceDescriptorHeap[pass.source_cubemap_slot];
    SamplerState linearSampler = SamplerDescriptorHeap[0];

    const float source_scale = pass.source_intensity;

    const uint SAMPLE_COUNT = 1024u;
    float totalWeight = 0.0;
    float3 prefilteredColor = 0.0;

    float roughness = pass.roughness;

    // Mip level logic to reduce artifacts (Chebychev's inequality approximation)
    uint cubeWidth, cubeHeight, cubeLevels;
    source.GetDimensions(0, cubeWidth, cubeHeight, cubeLevels);
    float resolution = float(cubeWidth);
    float saTexel = 4.0 * PI / (6.0 * resolution * resolution);

    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H  = ImportanceSampleGGX(Xi, N, roughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if(NdotL > 0.0)
        {
            // Sample from the environment's mip level based on PDF
            float D   = DistributionGGX(max(dot(N, H), 0.0), roughness);
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float pdf = D * NdotH / (4.0 * HdotV) + 0.0001;

            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);
            float mipLevel = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);

            prefilteredColor += source_scale
                * source.SampleLevel(linearSampler, L, mipLevel).rgb
                * NdotL;
            totalWeight      += NdotL;
        }
    }

    prefilteredColor = prefilteredColor / totalWeight;
    output[DTid] = float4(prefilteredColor, 1.0);
}
