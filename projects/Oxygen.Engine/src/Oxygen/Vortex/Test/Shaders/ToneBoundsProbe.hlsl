//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Native arithmetic qualification; this shader is not part of the runtime archive.
#include "Vortex/Services/PostProcess/ToneMappingBounds.hlsli"
#include "Vortex/Services/PostProcess/ToneMappingFastBounds.hlsli"
#include "Vortex/Contracts/View/HdrHardwareSampling.hlsli"
#include "Vortex/Contracts/View/HdrRadianceRange.hlsli"
#include "Vortex/Contracts/View/HdrErrorBounds.hlsli"
#include "Vortex/Services/PostProcess/HdrSceneComposition.hlsli"
#include "Vortex/Services/Environment/TransmittanceMath.hlsli"
#include "Vortex/Services/Environment/VolumetricFog.hlsl"

struct ProbeConstants { uint inputs; uint output; uint count; uint reserved; };

[numthreads(64, 1, 1)]
void CS(uint3 thread : SV_DispatchThreadID)
{
    StructuredBuffer<ProbeConstants> constants = ResourceDescriptorHeap[g_PassConstantsIndex];
    const ProbeConstants pass = constants[0];
    StructuredBuffer<uint4> input = ResourceDescriptorHeap[pass.inputs];
    RWByteAddressBuffer output = ResourceDescriptorHeap[pass.output];
    if (pass.reserved == 16384u) {
        // One padded 64-lane group: status [80,128), scalar coefficients
        // [128,1152), lane metadata [1152,1664), in a 2048-byte output.
        if (pass.count != 64u) {
            return;
        }
        const uint4 settings = input[0];
        if (thread.x == 0u) {
            output.Store4(0u, uint4(WaveGetLaneCount(), 64u, settings.xy));
            output.Store4(16u, 0x13579bdfu.xxxx);
            output.Store4(32u, 0x2468ace0u.xxxx);
            output.Store4(48u, 0x55aa55aau.xxxx);
            output.Store4(64u, 0xaa55aa55u.xxxx);
            output.Store4(80u, input[1]);
            output.Store4(96u, input[2]);
            output.Store4(112u, input[3]);
        }
        DeviceMemoryBarrierWithGroupSync();
        const uint base = 4u + thread.x * 4u;
        const float4 value = asfloat(input[base]);
        const float4 low = asfloat(input[base + 1u]);
        const float4 high = asfloat(input[base + 2u]);
        const uint control = input[base + 3u].x;
        const uint wave_lane = WaveGetLaneIndex();
        const bool active = (control & 1u) != 0u
            && ((control & 2u) == 0u || wave_lane != 0u);
        const float inverse_p = asfloat(settings.z);

        // Scalar coefficients are computed independently of publication.
        // The CPU serial unsigned-max oracle consumes these raw words.
        const float4 stored = settings.y != 0u ? HdrRoundToHalf(value) : value;
        float2 rgb = 0.0.xx;
        [unroll] for (uint c = 0u; c < 3u; ++c) {
            rgb = max(rgb, HdrEnclosureBound(stored[c], float2(low[c], high[c])));
        }
        rgb.y = HdrUpperProduct(rgb.y, inverse_p);
        const float2 transmission = HdrEnclosureBound(stored.a, float2(low.a, high.a));
        output.Store4(128u + thread.x * 16u, asuint(float4(rgb, transmission)));
        output.Store2(1152u + thread.x * 8u, uint2(wave_lane, active ? 1u : 0u));
        if (active) {
            RecordHdrStoreBounds(value, low, high, inverse_p,
                settings.x, pass.output, settings.y);
        }
        // This barrier is outside the divergent production-helper call.
        DeviceMemoryBarrierWithGroupSync();
        return;
    }
    if (thread.x >= pass.count) {
        return;
    }
    if (pass.reserved == 32768u) {
        // Read the shader-visible R32 view of a depth-array texel. The generic
        // texture readback API intentionally does not copy D32 resources.
        const uint4 settings = input[thread.x];
        Texture2DArray<float> source = ResourceDescriptorHeap[NonUniformResourceIndex(settings.x)];
        const float depth = source.Load(int4(settings.yzw, 0));
        output.Store4(thread.x * 32u, asuint(float4(depth, 0, 0, 0)));
        output.Store4(thread.x * 32u + 16u, 0u.xxxx);
        return;
    }
    if (pass.reserved == 8192u || pass.reserved == 4096u) {
        const uint4 settings = input[thread.x * 2u];
        const float4 ray = asfloat(input[thread.x * 2u + 1u]);
        StructuredBuffer<LocalFogVolumeInstanceData> instances = ResourceDescriptorHeap[settings.x];
        const LocalFogVolumeInstanceData encoded = instances[settings.y];
        const DecodedLocalFogVolumeInstanceData fog = DecodeLocalFogVolumeInstanceData(encoded);
        if (pass.reserved == 4096u) {
            output.Store4(thread.x * 32u, asuint(float4(fog.radial_fog_extinction,
                fog.height_fog_extinction, fog.height_fog_falloff, fog.height_fog_offset)));
            output.Store4(thread.x * 32u + 16u, asuint(float4(fog.emissive, fog.uniform_scale)));
        } else {
            const LocalFogVolumeIntegralData integral = EvaluateLocalFogVolumeIntegral(
                fog, float3(0, 0, ray.x), float3(0, 0, ray.y), ray.z);
            SamplerState linear_sampler = SamplerDescriptorHeap[VORTEX_SAMPLER_LINEAR_CLAMP];
            const float3 scattering = EvaluateLocalFogVolumeInScattering(
                fog, integral, linear_sampler, float3(0, 0, ray.y));
            const VolumetricLocalFogMedia media = EvaluateLocalFogVolumeFroxelMedia(
                encoded, float3(0, 0, ray.w), 1.0, 100.0);
            output.Store4(thread.x * 32u, asuint(float4(scattering, integral.coverage)));
            output.Store4(thread.x * 32u + 16u, asuint(float4(media.emissive, media.extinction)));
        }
        return;
    }
    if (pass.reserved == 2048u) {
        const float4 data = asfloat(input[thread.x]);
        const float integral = IntegratedTransmittance(data.x, data.y);
        output.Store4(thread.x * 32u, asuint(float4(integral, integral * data.z, 0, 0)));
        output.Store4(thread.x * 32u + 16u, 0u.xxxx);
        return;
    }
    if (pass.reserved == 1024u) {
        const float4 data = asfloat(input[thread.x]);
        const float opacity = OneMinusExpNegative(data.x);
        output.Store4(thread.x * 32u, asuint(float4(opacity,
            OpticalDepthFromOpacity(abs(data.x)), opacity * data.y,
            (1.0 - exp(-data.x)) * data.y)));
        output.Store4(thread.x * 32u + 16u, 0u.xxxx);
        return;
    }
    if (pass.reserved == 512u) {
        const uint4 settings = input[thread.x * 2u];
        const float gain = asfloat(input[thread.x * 2u + 1u].x);
        Texture2D<float4> source = ResourceDescriptorHeap[settings.x];
        SamplerState linear_sampler = SamplerDescriptorHeap[settings.y];
        const float4 value = source.SampleLevel(linear_sampler, asfloat(settings.zw), 0.0);
        output.Store4(thread.x * 32u, asuint(value * gain));
        output.Store4(thread.x * 32u + 16u, asuint(value));
        return;
    }
    if (pass.reserved == 256u) {
        const uint4 settings = input[thread.x];
        Texture2D<float4> source = ResourceDescriptorHeap[settings.x];
        const float4 value = source.Load(int3(0, 0, 0));
        output.Store4(thread.x * 32u, asuint(value * asfloat(settings.y)));
        output.Store4(thread.x * 32u + 16u, asuint(value));
        return;
    }
    if (pass.reserved == 128u) {
        const float4 value = asfloat(input[thread.x * 2u]);
        const float4 settings = asfloat(input[thread.x * 2u + 1u]);
        output.Store4(thread.x * 32u, asuint(float4(
            float(ClassifyHdrStoreRange(value, settings.x, uint(settings.y))), 0, 0, 0)));
        output.Store4(thread.x * 32u + 16u, 0u.xxxx);
        return;
    }
    if (pass.reserved == 64u) {
        const float4 bounds = asfloat(input[thread.x * 2u]);
        const float4 settings = asfloat(input[thread.x * 2u + 1u]);
        const float2 attenuation = SceneBoundAttenuate(bounds.xy, bounds.zw, settings.x);
        output.Store4(thread.x * 32u, asuint(float4(attenuation,
            SceneArithmeticBound(attenuation, uint(settings.y), settings.z))));
        output.Store4(thread.x * 32u + 16u, asuint(float4(
            SceneTransmittanceArithmetic(bounds.zw), SceneBoundAtMaximum(bounds.xy, settings.x))));
        return;
    }
    if (pass.reserved == 32u) {
        const float4 value = asfloat(input[thread.x * 3u]);
        const float4 gradient = asfloat(input[thread.x * 3u + 1u]);
        const uint3 extent = uint3(asfloat(input[thread.x * 3u + 2u]).xyz);
        const float2 interval = HdrHardwareFilterInterval(value.x, value.yz, gradient.xyz, extent, value.w != 0.0);
        output.Store4(thread.x * 32u, asuint(float4(interval, HdrFilterArithmetic(3u, value.w != 0.0))));
        output.Store4(thread.x * 32u + 16u, 0u.xxxx);
        return;
    }
    if (pass.reserved != 0u) {
        ToneRgbBounds bounds;
        const uint mapper = (pass.reserved >> 1u) & 3u;
        const float gamma = (pass.reserved & 16u) != 0u ? .8 : 2.2;
        const bool valid = ToneDisplayQuickBounds(asfloat(input[thread.x * 2u]),
            asfloat(input[thread.x * 2u + 1u]), 1.0, mapper, gamma, true, .25.xxx,
            uint2(1, 0), bounds);
        output.Store4(thread.x * 32u, asuint(float4(bounds.low, valid ? 1.0 : 0.0)));
        output.Store4(thread.x * 32u + 16u, asuint(float4(bounds.high, 0.0)));
        return;
    }
    const float4 value = asfloat(input[thread.x]);
    output.Store4(thread.x * 32u, asuint(float4(ToneDown(value.x), ToneUp(value.x),
        ToneAdd(value.xy, value.zw))));
    output.Store4(thread.x * 32u + 16u, asuint(float4(ToneMultiply(value.xy, value.zw),
        TonePower(value.xy, 2.0.xx))));
}
