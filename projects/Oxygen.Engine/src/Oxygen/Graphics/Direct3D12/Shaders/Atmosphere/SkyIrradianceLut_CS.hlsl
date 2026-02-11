//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Sky Irradiance LUT Compute Shader
//!
//! Precomputes diffuse hemispherical sky irradiance as a function of
//! sun zenith angle and altitude.
//!
//! Output: RGBA16F texture where:
//!   RGB = sky irradiance (diffuse only; excludes direct sun)
//!   A   = unused (0)
//!
//! UV Parameterization:
//!   u = (cos_sun_zenith + 1) / 2
//!   v = altitude / atmosphere_height
//!
//! Notes:
//! - This LUT is intended to be used for Lambertian ground reflection (albedo/pi)
//!   and for diffuse ambient skylight on opaque surfaces.
//! - The integration uses the same single + multi-scattering source terms as the
//!   view-ray integrators, ensuring consistent energy compensation.

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Renderer/EnvironmentDynamicData.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/SceneConstants.hlsli"
#include "Atmosphere/AtmosphereMedium.hlsli"
#include "Atmosphere/AtmospherePhase.hlsli"
#include "Common/Math.hlsli"
#include "Atmosphere/AtmosphereConstants.hlsli"
#include "Common/Geometry.hlsli"
#include "Atmosphere/AtmosphereSampling.hlsli"
#include "Atmosphere/IntegrateScatteredLuminance.hlsli"
#include "Common/Lighting.hlsli"

#include "Atmosphere/AtmospherePassConstants.hlsli"

// Root constants (b2, space0)
cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

#define THREAD_GROUP_SIZE_X 8
#define THREAD_GROUP_SIZE_Y 8

// Number of hemisphere samples used for the irradiance integral.
// 32 is a reasonable compromise for a low-res LUT.
static const uint kHemisphereSamples = 32;

// Number of raymarch steps per sample direction.
static const uint kRaymarchSteps = 32;

//! Uniform hemisphere sampling around +Z (normal).
//! Returns a direction and its cosine term with respect to the normal.
static inline void UniformHemisphereSample(uint i, uint n,
                                          out float3 dir,
                                          out float cos_theta)
{
    // Map i -> (u, v) in [0,1).
    float u = (float(i) + 0.5) / float(n);
    float v = frac((float(i) + 0.5) * 0.61803398875); // golden ratio frac

    // Uniform over hemisphere in solid angle:
    // cos(theta) = 1 - u, phi = 2*pi*v
    cos_theta = 1.0 - u;
    float sin_theta = sqrt(saturate(1.0 - cos_theta * cos_theta));
    float phi = TWO_PI * v;

    dir = float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
}

[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    ConstantBuffer<AtmospherePassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    if (dispatch_thread_id.x >= pass_constants.output_extent.x
        || dispatch_thread_id.y >= pass_constants.output_extent.y)
    {
        return;
    }

    EnvironmentStaticData env_data;
    if (!LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data))
    {
        RWTexture2D<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
        output[dispatch_thread_id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    GpuSkyAtmosphereParams atmo = env_data.atmosphere;

    // UV -> cos_sun_zenith and altitude
    float2 uv = (float2(dispatch_thread_id.xy) + 0.5)
              / float2(pass_constants.output_extent);

    float cos_sun_zenith = uv.x * 2.0 - 1.0;
    float altitude_m = uv.y * atmo.atmosphere_height_m;

    float3 sun_dir = float3(sqrt(max(0.0, 1.0 - cos_sun_zenith * cos_sun_zenith)),
                            0.0,
                            cos_sun_zenith);

    // Compute origin at the given altitude on +Z axis.
    float r = atmo.planet_radius_m + altitude_m;
    float3 origin = float3(0.0, 0.0, r);

    // Physical sun illuminance (linear RGB).
    float3 sun_illuminance = GetSunLuminanceRGB();

    // Bind LUT inputs.
    Texture2D<float4> multi_scat_lut = ResourceDescriptorHeap[pass_constants.multi_scat_srv_index];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];

    float3 irradiance = float3(0.0, 0.0, 0.0);

    // Differential solid angle for uniform hemisphere sampling.
    const float domega = TWO_PI / float(kHemisphereSamples);

    // Integrate diffuse sky radiance over the upper hemisphere.
    // Excludes the direct sun disk by construction: only scattering source terms
    // contribute to the radiance returned by IntegrateScatteredLuminance*.
    for (uint i = 0; i < kHemisphereSamples; ++i)
    {
        float3 wi;
        float cos_theta;
        UniformHemisphereSample(i, kHemisphereSamples, wi, cos_theta);

        float atmosphere_radius = atmo.planet_radius_m + atmo.atmosphere_height_m;
        float ray_length = RaySphereIntersectNearest(origin, wi, atmosphere_radius);

        if (ray_length <= 0.0)
        {
            continue;
        }

        float3 throughput;
        float3 Li = IntegrateScatteredLuminanceUniform(
            origin,
            wi,
            ray_length,
            kRaymarchSteps,
            atmo,
            sun_dir,
            sun_illuminance,
            pass_constants.transmittance_srv_index,
            float(pass_constants.transmittance_extent.x),
            float(pass_constants.transmittance_extent.y),
            multi_scat_lut,
            linear_sampler,
            throughput);

        irradiance += Li * cos_theta * domega;
    }

    // Safety: clamp to FP16-safe range.
    irradiance = min(irradiance, float3(kFP16SafeMax, kFP16SafeMax, kFP16SafeMax));

    RWTexture2D<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
    output[dispatch_thread_id.xy] = float4(irradiance, 0.0);
}
