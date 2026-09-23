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

// Eight-point Gauss-Legendre rule on [0,1]. Composite panels and explicit
// geometry/BRDF-peak partitions provide deterministic refinement.
static const float kEmitterNodes[8] = {
    0.0198550717512319, 0.101666761293187, 0.237233795041836, 0.408282678752175,
    0.591717321247825, 0.762766204958164, 0.898333238706813, 0.980144928248768
};
static const float kEmitterWeights[8] = {
    0.0506142681451881, 0.111190517226687, 0.156853322938944, 0.181341891689181,
    0.181341891689181, 0.156853322938944, 0.111190517226687, 0.0506142681451881
};

static void EmitterBasis(float3 axis, out float3 first, out float3 second)
{
    first = normalize(cross(abs(axis.z) < 0.9 ? float3(0,0,1) : float3(0,1,0), axis));
    second = cross(axis, first);
}

static float EmitterPhase(float3 direction, float3 first, float3 second)
{
    float phase = atan2(dot(direction, second), dot(direction, first));
    return phase < 0.0 ? phase + 2.0 * PI : phase;
}

struct EmitterArcs {
    float boundaries[10];
    uint count;
};

static void AddEmitterBoundary(inout EmitterArcs arcs, float boundary)
{
    if (boundary <= 0.0 || boundary >= 2.0 * PI) return;
    uint index = arcs.count++;
    [loop] while (index > 0u && arcs.boundaries[index - 1u] > boundary) {
        arcs.boundaries[index] = arcs.boundaries[index - 1u];
        --index;
    }
    arcs.boundaries[index] = boundary;
}

static void AddEmitterRoots(inout EmitterArcs arcs, float3 equation)
{
    const float amplitude = length(equation.yz);
    if (amplitude == 0.0 || abs(equation.x) >= amplitude) return;
    const float phase = atan2(equation.z, equation.y);
    const float delta = acos(clamp(-equation.x / amplitude, -1.0, 1.0));
    float a = phase - delta, b = phase + delta;
    if (a < 0.0) a += 2.0 * PI;
    if (b < 0.0) b += 2.0 * PI;
    AddEmitterBoundary(arcs, a);
    AddEmitterBoundary(arcs, b);
}

static void AddEmitterLobes(inout GgxDirectLobes sum, GgxDirectLobes value, float3 weight)
{
    sum.single_scattering += value.single_scattering * weight;
    sum.multiple_scattering += value.multiple_scattering * weight;
    sum.diffuse += value.diffuse * weight;
}

static void AccumulateEmitterTerm(inout float3 sum, inout float3 correction, float3 value)
{
    precise float3 adjusted = value - correction;
    precise float3 next = sum + adjusted;
    correction = (next - sum) - adjusted;
    sum = next;
}

static void AccumulateEmitterLobes(inout GgxDirectLobes sum,
    inout GgxDirectLobes correction, GgxDirectLobes value, float3 weight)
{
    AccumulateEmitterTerm(sum.single_scattering, correction.single_scattering, value.single_scattering * weight);
    AccumulateEmitterTerm(sum.multiple_scattering, correction.multiple_scattering, value.multiple_scattering * weight);
    AccumulateEmitterTerm(sum.diffuse, correction.diffuse, value.diffuse * weight);
}

static bool EmitterLobesConverged(GgxDirectLobes a, GgxDirectLobes b)
{
    return all(4.0 * abs(a.single_scattering - b.single_scattering)
            <= 1.25e-6 + 0.0025 * abs(a.single_scattering))
        && all(4.0 * abs(a.multiple_scattering - b.multiple_scattering)
            <= 1.25e-6 + 0.0025 * abs(a.multiple_scattering))
        && all(4.0 * abs(a.diffuse - b.diffuse)
            <= 1.25e-6 + 0.0025 * abs(a.diffuse));
}

struct EmitterGeometry {
    float3 center;
    float3 axis;
    float3 first;
    float3 second;
    float distance;
    float radius;
    float range;
    float minimum;
    float maximum;
    float tangent_start;
    float tangent_span;
    float support_radius;
    float projected_distance;
    float support_phase;
    float peak_radial;
    float peak_phase;
    float3 peak_plane;
    bool sphere;
};

static bool PrepareEmitterGeometry(ForwardLocalLightRecord light, float3 receiver,
    float3 N, float3 V, out EmitterGeometry g)
{
    g = (EmitterGeometry)0;
    g.center = light.position_ws - receiver;
    g.distance = length(g.center);
    g.radius = light.source_radius_m;
    g.range = light.range_m;
    g.sphere = light.kind == FORWARD_LOCAL_LIGHT_POINT;
    if (g.range <= 0.0 || g.distance - g.radius >= g.range) return false;
    const float3 peak = reflect(-V, N);
    g.peak_plane = cross(N, peak);
    g.minimum = 0.0;
    g.maximum = 1.0;
    if (g.sphere) {
        if (g.distance <= g.radius || dot(N, g.center) + g.radius <= 0.0) return false;
        g.axis = g.center / g.distance;
        EmitterBasis(g.axis, g.first, g.second);
        const float ratio = g.radius / g.distance;
        const float nc = dot(N, g.center);
        if (nc < 0.0) g.minimum = (nc / g.radius) * (nc / g.radius);
        const float tangent_distance = sqrt(g.distance - g.radius) * sqrt(g.distance + g.radius);
        if (g.range < tangent_distance) {
            const float q = g.range / g.distance;
            g.maximum = saturate((q - (1.0 - ratio)) * ((1.0 + ratio) - q)
                * ((1.0 + ratio) + q) * ((1.0 - ratio) + q)
                / (4.0 * ratio * ratio * q * q));
        }
        if (g.minimum >= g.maximum) return false;
        g.tangent_start = sqrt(1.0 - g.maximum);
        g.tangent_span = (g.maximum - g.minimum)
            / (g.tangent_start + sqrt(1.0 - g.minimum));
        const float peak_sine = length(cross(g.axis, peak));
        const float peak_u = dot(g.axis, peak) > 0.0 && peak_sine < ratio
            ? (peak_sine / ratio) * (peak_sine / ratio) : 1.0;
        g.peak_radial = peak_u > g.minimum && peak_u < g.maximum
            ? (g.maximum - peak_u)
                / (g.tangent_span * (sqrt(1.0 - peak_u) + g.tangent_start)) : 0.0;
        g.peak_phase = EmitterPhase(peak, g.first, g.second);
    } else {
        g.axis = normalize(light.emitted_direction_ws);
        const float height = -dot(g.center, g.axis);
        if (height <= 0.0 || g.range <= height) return false;
        EmitterBasis(g.axis, g.first, g.second);
        const float nc = dot(N, g.center);
        const float normal_extent = length(float2(dot(N, g.first), dot(N, g.second)));
        if (nc + g.radius * normal_extent <= 0.0) return false;
        const float range_radius = g.range * sqrt((g.range - height) / g.range * (1.0 + height / g.range));
        precise float outer_s = light.outer_cone_sin_half_squared
            * (1.0 + light.outer_cone_relative_correction);
        precise float outer_cos = (1.0 - 2.0 * light.outer_cone_sin_half_squared)
            - 2.0 * light.outer_cone_sin_half_squared * light.outer_cone_relative_correction;
        g.support_radius = outer_cos <= 0.0 ? range_radius
            : min(range_radius, height * (2.0 * sqrt(outer_s * (1.0 - outer_s))) / outer_cos);
        g.projected_distance = length(g.center + g.axis * height);
        if (g.projected_distance > g.radius
            && g.projected_distance - g.radius >= g.support_radius) return false;
        g.support_phase = EmitterPhase(-g.center, g.first, g.second);
        g.minimum = max(0.0, (g.projected_distance - g.support_radius) / g.radius);
        g.maximum = min(1.0, (g.projected_distance + g.support_radius) / g.radius);
        if (nc < 0.0) g.minimum = max(g.minimum, -nc / (g.radius * normal_extent));
        if (g.minimum >= g.maximum) return false;
        const float facing = -dot(g.axis, peak);
        const float3 offset = facing > 0.0 ? peak * (height / facing) - g.center : -g.center;
        g.peak_radial = clamp(length(float2(dot(offset, g.first), dot(offset, g.second))) / g.radius,
            g.minimum, g.maximum);
        g.peak_phase = EmitterPhase(offset, g.first, g.second);
    }
    return true;
}

static GgxDirectLobes IntegrateEmitterRule(EmitterGeometry g,
    ForwardLocalLightRecord light, float3 N, float3 V, float3 F0, float3 rho,
    float roughness, LightingFrameBindings lighting, uint panels)
{
    GgxDirectLobes sum = (GgxDirectLobes)0;
    GgxDirectLobes correction = (GgxDirectLobes)0;
    const float radial_start = g.sphere ? 0.0 : g.minimum;
    const float radial_end = g.sphere ? 1.0 : g.maximum;
    const float radial_peak = clamp(g.peak_radial, radial_start, radial_end);
    [loop] for (uint segment = 0u; segment < 2u; ++segment) {
        float start = segment == 0u ? radial_start : radial_peak;
        float end = segment == 0u ? radial_peak : radial_end;
        if (start >= end) continue;
        [loop] for (uint radial_index = 0u; radial_index < panels * 8u; ++radial_index) {
            const uint node = radial_index % 8u;
            // Cluster nodes quadratically at partition boundaries. The known
            // specular peak is a boundary, so even smooth lobes are resolved
            // without a uniformly enormous source grid.
            const float radial_t = (float(radial_index / 8u) + kEmitterNodes[node]) / float(panels);
            const float radial_sine = sin(0.5 * PI * radial_t);
            const float radial_jacobian = 0.5 * PI * sin(PI * radial_t);
            const float parameter = start + (end - start) * radial_sine * radial_sine;
            float3 center, first, second;
            float scale;
            float ring = 0.0;
            if (g.sphere) {
                const float offset = g.tangent_span * parameter;
                const float tangent = g.tangent_start + offset;
                const float u = clamp(g.maximum - offset * (2.0 * g.tangent_start + offset), g.minimum, g.maximum);
                const float ratio = g.radius / g.distance;
                const float sine = ratio * sqrt(u);
                const float cosine = sqrt(1.0 - sine * sine);
                const float distance = (g.distance - g.radius) * (1.0 + ratio)
                    / (cosine + ratio * tangent);
                center = g.axis * cosine;
                first = g.first * sine;
                second = g.second * sine;
                const float window_coordinate = distance / g.range;
                const float window = saturate(1.0 - pow(window_coordinate, 4.0));
                const float guard = min(1.0, (distance / 0.001) * (distance / 0.001));
                scale = window * window * guard / g.distance / g.distance
                    * g.tangent_span * tangent / (PI * cosine);
            } else {
                ring = g.radius * parameter;
                center = g.center;
                first = g.first * ring;
                second = g.second * ring;
                scale = parameter / PI;
            }
            EmitterArcs arcs = (EmitterArcs)0;
            arcs.boundaries[0] = 0.0;
            arcs.boundaries[1] = 2.0 * PI;
            arcs.count = 2u;
            const float3 horizon = float3(dot(N, center), dot(N, first), dot(N, second));
            AddEmitterRoots(arcs, horizon);
            AddEmitterBoundary(arcs, g.peak_phase);
            AddEmitterRoots(arcs, float3(dot(g.peak_plane, center), dot(g.peak_plane, first), dot(g.peak_plane, second)));
            float support_half_width = PI;
            if (!g.sphere && g.support_radius < g.projected_distance + ring) {
                const float difference = abs(g.projected_distance - ring);
                if (g.support_radius <= difference || g.projected_distance == 0.0 || ring == 0.0) continue;
                const float half_sine_squared = (g.support_radius - difference) * (g.support_radius + difference)
                    / (4.0 * g.projected_distance * ring);
                support_half_width = 2.0 * asin(sqrt(saturate(half_sine_squared)));
                float a = g.support_phase - support_half_width;
                float b = g.support_phase + support_half_width;
                if (a < 0.0) a += 2.0 * PI;
                if (b >= 2.0 * PI) b -= 2.0 * PI;
                AddEmitterBoundary(arcs, a);
                AddEmitterBoundary(arcs, b);
            }
            [loop] for (uint arc = 0u; arc + 1u < arcs.count; ++arc) {
                const float a = arcs.boundaries[arc], b = arcs.boundaries[arc + 1u];
                const float midpoint = 0.5 * (a + b);
                if (horizon.x + horizon.y * cos(midpoint) + horizon.z * sin(midpoint) <= 0.0) continue;
                const float support_delta = abs(midpoint - g.support_phase);
                if (!g.sphere && min(support_delta, 2.0 * PI - support_delta) > support_half_width) continue;
                [loop] for (uint angular_index = 0u; angular_index < panels * 8u; ++angular_index) {
                    const uint angular_node = angular_index % 8u;
                    const float angular_t = (float(angular_index / 8u) + kEmitterNodes[angular_node]) / float(panels);
                    const float angular_sine = sin(0.5 * PI * angular_t);
                    const float angular_jacobian = 0.5 * PI * sin(PI * angular_t);
                    const float phi = a + (b - a) * angular_sine * angular_sine;
                    const float3 ray = center + first * cos(phi) + second * sin(phi);
                    const float distance = length(ray);
                    if (distance <= 0.0) continue;
                    const float3 L = ray / distance;
                    float illumination = scale;
                    if (!g.sphere) {
                        illumination *= ComputeLocalLightDistanceAttenuation(ray, g.range)
                            * ComputeSpotLightAngularAttenuation(L, g.axis,
                                float2(light.inner_cone_sin_half_squared, light.inner_cone_relative_correction),
                                float2(light.outer_cone_sin_half_squared, light.outer_cone_relative_correction));
                    }
                    const float weight = (end - start) * (b - a) * kEmitterWeights[node]
                        * kEmitterWeights[angular_node] * radial_jacobian * angular_jacobian / float(panels * panels);
                    AccumulateEmitterLobes(sum, correction, EvaluateGgxDirectLobes(N, V, L, F0, rho, roughness, lighting),
                        light.intensity_rgb_cd * (illumination * weight));
                }
            }
        }
    }
    return sum;
}

static GgxDirectLobes EvaluateLocalEmitterLobes(ForwardLocalLightRecord light,
    float3 receiver, float3 N, float3 V, float3 F0, float3 rho, float roughness,
    LightingFrameBindings lighting)
{
    GgxDirectLobes result = (GgxDirectLobes)0;
    if (dot(N, V) <= 0.0 || light.range_m <= 0.0 || all(light.intensity_rgb_cd == 0.0)) return result;
    if (light.source_radius_m == 0.0) {
        const float3 ray = light.position_ws - receiver;
        const float distance = length(ray);
        if (distance <= 0.0) return result;
        const float3 L = ray / distance;
        float attenuation = ComputeLocalLightDistanceAttenuation(ray, light.range_m);
        if (light.kind == FORWARD_LOCAL_LIGHT_SPOT) {
            attenuation *= ComputeSpotLightAngularAttenuation(L, light.emitted_direction_ws,
                float2(light.inner_cone_sin_half_squared, light.inner_cone_relative_correction),
                float2(light.outer_cone_sin_half_squared, light.outer_cone_relative_correction));
        }
        AddEmitterLobes(result, EvaluateGgxDirectLobes(N, V, L, F0, rho, roughness, lighting),
            light.intensity_rgb_cd * attenuation);
        return result;
    }
    EmitterGeometry geometry;
    if (!PrepareEmitterGeometry(light, receiver, N, V, geometry)) return result;
    uint consecutive = 0u;
    [loop] for (uint panels = 1u; panels <= 64u; panels *= 2u) {
        const GgxDirectLobes current = IntegrateEmitterRule(geometry, light, N, V, F0, rho, roughness, lighting, panels);
        consecutive = panels > 1u && EmitterLobesConverged(current, result) ? consecutive + 1u : 0u;
        result = current;
        if (consecutive >= 2u) return result;
    }
    // Exhaustion is a failed physical evaluation, never successful black light
    // or silently accepted unconverged output. Existing HDR admission owns it.
    result.single_scattering = asfloat(0x7fc00000u).xxx;
    return result;
}

static float3 EvaluateLocalEmitterResponse(ForwardLocalLightRecord light,
    float3 receiver, float3 N, float3 V, float3 F0, float3 rho, float roughness,
    LightingFrameBindings lighting)
{
    const GgxDirectLobes lobes = EvaluateLocalEmitterLobes(light, receiver, N, V, F0, rho, roughness, lighting);
    return lobes.single_scattering + lobes.multiple_scattering + lobes.diffuse;
}

#endif
