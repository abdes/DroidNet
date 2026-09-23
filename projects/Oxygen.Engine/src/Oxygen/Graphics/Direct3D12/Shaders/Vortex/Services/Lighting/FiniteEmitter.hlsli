//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_FINITE_EMITTER_HLSLI
#define OXYGEN_VORTEX_FINITE_EMITTER_HLSLI

#include "Vortex/Contracts/Lighting/ForwardLocalLightRecord.hlsli"
#include "Vortex/Services/Lighting/LocalLightAttenuation.hlsli"
#include "Vortex/Shared/BRDFCommon.hlsli"

// Analytic spherical-source approximation: de Carpentier, Decima (2017).
// Find the specular representative direction on the apparent spherical cap.
// A single Newton step in the tangent-half-angle coordinate refines the
// boundary solution; there is no source sampling or convergence loop.
static float3 EmitterSpecularDirection(float3 N, float3 V, float3 center_direction,
    float sin_alpha)
{
    const float3 reflected = reflect(-V, N);
    const float cosine = sqrt(saturate(1.0 - sin_alpha * sin_alpha));
    const float alignment = dot(reflected, center_direction);
    if (alignment >= cosine) return reflected;
    float3 projected = reflected - center_direction * alignment;
    if (dot(projected, projected) < 1.0e-12)
        projected = N - center_direction * dot(N, center_direction);
    const float3 tangent = normalize(projected);
    const float3 bitangent = cross(center_direction, tangent);
    const float3 cap_center = center_direction * cosine;
    const float3 cap_tangent = tangent * sin_alpha;
    const float3 cap_bitangent = bitangent * sin_alpha;
    const float a = dot(N, V + cap_center);
    const float b = dot(N, cap_tangent);
    const float c = dot(N, cap_bitangent);
    const float d = 1.0 + dot(V, cap_center);
    const float e = dot(V, cap_tangent);
    const float f = dot(V, cap_bitangent);
    const float numerator = a + b;
    const float denominator = d + e;
    float parameter = 0.0;
    if (numerator > 1.0e-6 && denominator > 1.0e-6) {
        const float n = 2.0 * c / numerator;
        const float q = 2.0 * f / denominator;
        const float gradient = 2.0 * n - q;
        const float curvature = 4.0 * (a - b) / numerator - 2.0 * n * n
            - 4.0 * d / denominator + q * q;
        if (curvature < -1.0e-6) parameter = clamp(-gradient / curvature, -1.0, 1.0);
    }
    const float inverse = rcp(1.0 + parameter * parameter);
    const float cos_phi = (1.0 - parameter * parameter) * inverse;
    const float sin_phi = 2.0 * parameter * inverse;
    return cap_center + cap_tangent * cos_phi + cap_bitangent * sin_phi;
}

static float EmitterDiffuseCosine(float cosine, float sin_alpha)
{
    if (cosine >= sin_alpha) return cosine;
    if (cosine <= -sin_alpha) return 0.0;
    const float wrapped = cosine + sin_alpha;
    return wrapped * wrapped / (4.0 * sin_alpha);
}

struct LocalEmitterInput
{
    float3 direction_to_center;
    float inverse_distance;
    float attenuation;
};

// Prepare attenuation once before material/shadow work, shared by every path.
static bool PrepareLocalEmitterInput(ForwardLocalLightRecord light,
    float3 receiver, out LocalEmitterInput source)
{
    source = (LocalEmitterInput)0;
    const float3 ray = light.position_ws - receiver;
    const float distance_squared = dot(ray, ray);
    if (light.range_m <= 0.0 || distance_squared <= 0.0
        || distance_squared >= light.range_m * light.range_m
        || all(light.intensity_rgb_cd == 0.0)) return false;
    source.inverse_distance = rsqrt(distance_squared);
    source.direction_to_center = ray * source.inverse_distance;
    source.attenuation = ComputeLocalLightDistanceAttenuation(ray, light.range_m);
    if (light.kind == FORWARD_LOCAL_LIGHT_SPOT) {
        source.attenuation *= ComputeSpotLightAngularAttenuation(source.direction_to_center,
            light.emitted_direction_ws, light.outer_cone_cosine, light.inverse_cone_cosine_width);
    }
    return source.attenuation > 0.0;
}

static bool LocalEmitterFacesSurface(ForwardLocalLightRecord light,
    LocalEmitterInput source, float3 N)
{
    return dot(N, source.direction_to_center) + light.source_radius_m * source.inverse_distance > 0.0;
}

static GgxDirectLobes EvaluatePreparedLocalEmitterLobes(ForwardLocalLightRecord light,
    LocalEmitterInput source, float3 N, float3 V, GgxDirectContext brdf)
{
    GgxDirectLobes result = (GgxDirectLobes)0;
    if (brdf.nv <= 0.0 || !LocalEmitterFacesSurface(light, source, N)) return result;
    const float3 L = source.direction_to_center;
    if (light.source_radius_m == 0.0) {
        result = EvaluatePreparedGgxDirectLobes(N, V, L, brdf);
    } else {
        // Radius describes apparent source size. Range and cone attenuation
        // remain center based, allowing tight light volumes and projected spots.
        const float sin_alpha = saturate(light.source_radius_m * source.inverse_distance);
        const float diffuse_cosine = EmitterDiffuseCosine(dot(N, L), sin_alpha);
        if (diffuse_cosine <= 0.0) return result;
        const float specular_sine = sin_alpha * (1.0 - brdf.roughness * brdf.roughness);
        const float3 representative = specular_sine > 0.0
            ? EmitterSpecularDirection(N, V, L, specular_sine) : L;
        float normalization = 1.0;
        if (specular_sine > 0.0) {
            const float3 half_vector = V + representative;
            const float half_squared = dot(half_vector, half_vector);
            const float vh = half_squared > 0.0
                ? saturate(dot(V, half_vector) * rsqrt(half_squared)) : 0.0;
            const float broadened = brdf.alpha_squared + 0.25 * specular_sine
                * (3.0 * brdf.roughness * brdf.roughness + specular_sine) / (vh + 0.001);
            normalization = brdf.alpha_squared / broadened;
        }
        // Visibility uses the analytic irradiance cosine; only the specular
        // half-vector is bent toward the finite source's strongest response.
        result.single_scattering = EvaluatePreparedGgxSpecular(N, V,
            representative, diffuse_cosine, brdf) * normalization;
        result.multiple_scattering = result.single_scattering * (brdf.specular_scale - 1.0);
        result.diffuse = brdf.diffuse_scale * diffuse_cosine;
    }
    const float3 irradiance = light.intensity_rgb_cd * source.attenuation;
    result.single_scattering *= irradiance;
    result.multiple_scattering *= irradiance;
    result.diffuse *= irradiance;
    return result;
}

static GgxDirectLobes EvaluateLocalEmitterLobes(ForwardLocalLightRecord light,
    float3 receiver, float3 N, float3 V, float3 F0, float3 rho, float roughness,
    LightingFrameBindings lighting)
{
    LocalEmitterInput source;
    if (!PrepareLocalEmitterInput(light, receiver, source)) return (GgxDirectLobes)0;
    return EvaluatePreparedLocalEmitterLobes(light, source, N, V,
        PrepareGgxDirect(N, V, F0, rho, roughness, lighting));
}

#endif
