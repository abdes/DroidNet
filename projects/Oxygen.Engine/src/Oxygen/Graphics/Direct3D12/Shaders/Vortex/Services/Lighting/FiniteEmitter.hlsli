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

// Bounded Gauss rule for clipped source support. No per-pixel convergence
// search: the independent CPU integrator owns numerical refinement.
static const float kEmitterNodes[8] = {
    0.0198550717512319, 0.101666761293187, 0.237233795041836, 0.408282678752175,
    0.591717321247825, 0.762766204958164, 0.898333238706813, 0.980144928248768
};
static const float kEmitterWeights[8] = {
    0.0506142681451881, 0.111190517226687, 0.156853322938944, 0.181341891689181,
    0.181341891689181, 0.156853322938944, 0.111190517226687, 0.0506142681451881
};

static const float kSmoothEmitterNodes[4] = {
    0.0694318442029737, 0.330009478207572, 0.669990521792428, 0.930568155797026
};
static const float kSmoothEmitterWeights[4] = {
    0.173927422568727, 0.326072577431273, 0.326072577431273, 0.173927422568727
};

static void EmitterBasis(float3 axis, out float3 first, out float3 second)
{
    first = normalize(cross(abs(axis.z) < 0.9 ? float3(0,0,1) : float3(0,1,0), axis));
    second = cross(axis, first);
}

static float EmitterPhase(float3 direction, float3 first, float3 second)
{
    const float2 projected = float2(dot(direction, first), dot(direction, second));
    // Azimuth is arbitrary on the axis; atan2(0,0) is undefined in HLSL.
    if (all(projected == 0.0)) return 0.0;
    float phase = atan2(projected.y, projected.x);
    return phase < 0.0 ? phase + 2.0 * PI : phase;
}

// Two endpoints, two horizon roots, one peak phase, two peak-plane roots,
// two inner-cone roots and two outer-support roots require at most 11 entries.
static const uint kEmitterBoundaryCapacity = 12u;

struct EmitterArcs {
    float boundaries[kEmitterBoundaryCapacity];
    uint count;
    bool scalar_access;
};

static void AddEmitterBoundary(inout EmitterArcs arcs, float boundary)
{
    if (boundary <= 0.0 || boundary >= 2.0 * PI) return;
    // Sphere integration uses compact indexed storage to limit register pressure;
    // disk integration benefits from scalar boundary access. Deferred source-family
    // specialization eliminates the unused storage strategy at compile time.
    if (!arcs.scalar_access) {
        uint index = arcs.count++;
        [loop] while (index > 0u && arcs.boundaries[index - 1u] > boundary) {
            arcs.boundaries[index] = arcs.boundaries[index - 1u];
            --index;
        }
        arcs.boundaries[index] = boundary;
        return;
    }
    [unroll] for (uint index = 1u; index < kEmitterBoundaryCapacity; ++index) {
        if (index <= arcs.count) {
            const float previous = index == arcs.count ? 2.0 * PI : arcs.boundaries[index];
            arcs.boundaries[index] = min(previous, boundary);
            boundary = max(previous, boundary);
        }
    }
    ++arcs.count;
}

static float GetEmitterBoundary(EmitterArcs arcs, uint selected)
{
    if (!arcs.scalar_access) return arcs.boundaries[selected];
    float boundary = 0.0;
    [unroll] for (uint index = 0u; index < kEmitterBoundaryCapacity; ++index) {
        boundary = selected == index ? arcs.boundaries[index] : boundary;
    }
    return boundary;
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

// Finite-source integration has a 1% lobe budget. Away from the cone edges,
// ordinary FP32 arithmetic contributes at most 0.2% of that response. Punctual
// evaluation and poorly conditioned cones retain the compensated evaluator.
static float EmitterAngularAttenuation(ForwardLocalLightRecord light,
    float3 L, float3 axis)
{
    const float coordinate = 0.5 * (1.0 + dot(L, axis));
    const float width = light.outer_cone_sin_half_squared - light.inner_cone_sin_half_squared;
    const float edge = light.outer_cone_sin_half_squared - coordinate;
    if (width > 0.02 && edge > 0.004 && edge < width - 0.004) {
        const float ramp = edge / width;
        return ramp * ramp;
    }
    return ComputeSpotLightAngularAttenuation(L, axis,
        float2(light.inner_cone_sin_half_squared, light.inner_cone_relative_correction),
        float2(light.outer_cone_sin_half_squared, light.outer_cone_relative_correction));
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
    float inner_support_radius;
    float projected_distance;
    float support_phase;
    float peak_radial;
    float peak_phase;
    float3 peak_plane;
    bool sphere;
    bool peak_supported;
};

static bool PrepareEmitterGeometry(ForwardLocalLightRecord light, float3 receiver,
    float3 N, out EmitterGeometry g)
{
    g = (EmitterGeometry)0;
    g.center = light.position_ws - receiver;
    g.distance = length(g.center);
    g.radius = light.source_radius_m;
    g.range = light.range_m;
    g.sphere = light.kind == FORWARD_LOCAL_LIGHT_POINT;
    if (g.range <= 0.0 || g.distance - g.radius >= g.range) return false;
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
        const float inner_cos = 1.0 - 2.0 * light.inner_cone_sin_half_squared;
        const float inner_sine = 2.0 * sqrt(light.inner_cone_sin_half_squared
            * (1.0 - light.inner_cone_sin_half_squared));
        g.inner_support_radius = inner_cos > 0.0 ? height * inner_sine / inner_cos : g.range;
        g.projected_distance = length(g.center + g.axis * height);
        if (g.projected_distance > g.radius
            && g.projected_distance - g.radius >= g.support_radius) return false;
        g.minimum = max(0.0, (g.projected_distance - g.support_radius) / g.radius);
        g.maximum = min(1.0, (g.projected_distance + g.support_radius) / g.radius);
        if (nc < 0.0) g.minimum = max(g.minimum, -nc / (g.radius * normal_extent));
        if (g.minimum >= g.maximum) return false;
    }
    return true;
}

// Peak coordinates and support azimuths are only needed by clipped integration.
// The common cubature path does not construct or sort angular partitions.
static void PrepareEmitterPeak(inout EmitterGeometry g, float3 N, float3 V)
{
    const float3 peak = reflect(-V, N);
    g.peak_plane = cross(N, peak);
    if (g.sphere) {
        const float ratio = g.radius / g.distance;
        const float peak_sine = length(cross(g.axis, peak));
        const float peak_u = dot(g.axis, peak) > 0.0 && peak_sine < ratio
            ? (peak_sine / ratio) * (peak_sine / ratio) : 1.0;
        g.peak_supported = dot(g.axis, peak) > 0.0 && peak_sine < ratio
            && peak_u >= g.minimum && peak_u <= g.maximum;
        const float clipped_peak = clamp(peak_u, g.minimum, g.maximum);
        // The cap rim maps to the start of the radial interval. Handle it
        // directly: the rationalized expression is 0/0 at maximum == 1.
        g.peak_radial = clipped_peak == g.maximum ? 0.0
            : (g.maximum - clipped_peak)
                / (g.tangent_span * (sqrt(1.0 - clipped_peak) + g.tangent_start));
        g.peak_phase = EmitterPhase(peak, g.first, g.second);
    } else {
        const float height = -dot(g.center, g.axis);
        g.support_phase = EmitterPhase(-g.center, g.first, g.second);
        const float facing = -dot(g.axis, peak);
        const float3 offset = facing > 0.0 ? peak * (height / facing) - g.center : -g.center;
        const float peak_radius = length(float2(dot(offset, g.first), dot(offset, g.second))) / g.radius;
        g.peak_supported = facing > 0.0 && peak_radius >= g.minimum && peak_radius <= g.maximum;
        g.peak_radial = clamp(peak_radius, g.minimum, g.maximum);
        g.peak_phase = EmitterPhase(offset, g.first, g.second);
    }
}

static bool EmitterHasSmoothResponse(EmitterGeometry g, float3 N, float3 V,
    GgxDirectContext brdf)
{
    const float ratio = g.radius / g.distance;
    const float peak_distance = length(g.center / g.distance - reflect(-V, N));
    const float variation_scale = 2.0 * brdf.roughness * brdf.roughness
        + max(0.0, peak_distance - ratio);
    return ratio <= 0.2 && ratio <= 0.5 * variation_scale
        && g.distance <= 0.25 * g.range && g.distance - g.radius > 0.001;
}

// Map the known GGX peak to a uniform angular coordinate. Integrating the
// narrow lobe in its natural width avoids resolving it with a dense source grid.
// Broad diffuse/compensation lobes keep the ordinary source-area rule.
static float3 EmitterPeakCoordinate(float node, float width, float span,
    bool peak_at_start)
{
    const float scale = max(width / span, 1.0e-7);
    const float angle = atan(1.0 / scale);
    float sine, cosine;
    sincos(angle * node, sine, cosine);
    const float offset = scale * sine / cosine;
    return float3(peak_at_start ? offset : 1.0 - offset,
        scale * angle / (cosine * cosine), offset);
}

static float3 EmitterRadialPeakCoordinate(EmitterGeometry g, float3 N,
    GgxDirectContext brdf, float node, float span, bool peak_at_start)
{
    const float alpha = brdf.roughness * brdf.roughness;
    const float3 radial_axis = g.first * cos(g.peak_phase) + g.second * sin(g.peak_phase);
    float3 derivative;
    if (g.sphere) {
        const float ratio = g.radius / g.distance;
        const float offset = g.tangent_span * g.peak_radial;
        const float tangent = g.tangent_start + offset;
        const float u = max(0.0, g.maximum - offset * (2.0 * g.tangent_start + offset));
        if (u < 1.0e-8) {
            // At the cap axis, angular distance is proportional to sqrt(u).
            // The GGX radial CDF is rational here, not an arctangent.
            const float width = 2.0 * alpha * alpha * brdf.nv * brdf.nv
                / (ratio * ratio * g.tangent_span * tangent);
            const float scale = width / span;
            const float limit = 1.0 / (1.0 + scale);
            const float denominator = 1.0 - node * limit;
            const float distance = scale * node * limit / denominator;
            return float3(peak_at_start ? distance : 1.0 - distance,
                scale * limit / (denominator * denominator), distance);
        }
        const float cosine = sqrt(1.0 - ratio * ratio * u);
        derivative = (g.axis * (-ratio * ratio / (2.0 * cosine))
            + radial_axis * (ratio / (2.0 * sqrt(u)))) * (-2.0 * g.tangent_span * tangent);
    } else {
        const float3 ray = g.center + radial_axis * (g.radius * g.peak_radial);
        const float distance = length(ray);
        const float3 direction = ray / distance;
        derivative = (g.radius / distance) * (radial_axis - direction * dot(direction, radial_axis));
    }
    const float slope = length(derivative - N * dot(N, derivative)) / (2.0 * brdf.nv);
    return EmitterPeakCoordinate(node, alpha / max(slope, 1.0e-20), span, peak_at_start);
}

static GgxDirectLobes IntegrateEmitterRule(EmitterGeometry g,
    ForwardLocalLightRecord light, float3 N, float3 V, GgxDirectContext brdf,
    bool importance, bool broad_only)
{
    GgxDirectLobes sum = (GgxDirectLobes)0;
    const float radial_start = g.sphere ? 0.0 : g.minimum;
    const float radial_end = g.sphere ? 1.0 : g.maximum;
    const float radial_peak = clamp(g.peak_radial, radial_start, radial_end);
    const bool smooth = !importance && EmitterHasSmoothResponse(g, N, V, brdf);
    const uint order = smooth ? 4u : 8u;
    const float peak_extent = g.sphere ? length(cross(g.axis, reflect(-V, N)))
        : g.radius * g.peak_radial / g.distance;
    const bool angular_peak = peak_extent > 2.0 * brdf.roughness * brdf.roughness;
    [loop] for (uint segment = 0u; segment < 2u; ++segment) {
        float start = segment == 0u ? radial_start : radial_peak;
        float end = segment == 0u ? radial_peak : radial_end;
        if (start >= end) continue;
        [loop] for (uint radial_index = 0u; radial_index < order; ++radial_index) {
            const uint node = radial_index;
            // Cluster nodes quadratically at partition boundaries. The known
            // specular peak is a boundary, so even smooth lobes are resolved
            // without a uniformly enormous source grid.
            const float radial_t = smooth ? kSmoothEmitterNodes[node] : kEmitterNodes[node];
            const float radial_weight = smooth ? kSmoothEmitterWeights[node] : kEmitterWeights[node];
            const float radial_sine = sin(0.5 * PI * radial_t);
            float radial_jacobian = 0.5 * PI * sin(PI * radial_t);
            float radial_coordinate = smooth ? radial_t : radial_sine * radial_sine;
            if (smooth) radial_jacobian = 1.0;
            float parameter_to_end = (1.0 - start) - (end - start) * radial_coordinate;
            if (importance) {
                const float3 mapped = EmitterRadialPeakCoordinate(g, N, brdf,
                    radial_t, end - start, segment == 1u);
                radial_coordinate = mapped.x;
                radial_jacobian = mapped.y;
                // Preserve the small distance from the cap axis independently
                // of the rounded [0,1] parameter used for the surface distance.
                parameter_to_end = segment == 0u
                    ? (1.0 - end) + (end - start) * mapped.z
                    : (1.0 - start) - (end - start) * mapped.z;
            }
            const float parameter = start + (end - start) * radial_coordinate;
            float3 center, first, second;
            float scale;
            float ring = 0.0;
            if (g.sphere) {
                const float offset = g.tangent_span * parameter;
                const float tangent = g.tangent_start + offset;
                const float tail = g.tangent_span * parameter_to_end;
                const float u = clamp(g.minimum + tail * (2.0 * sqrt(1.0 - g.minimum) - tail),
                    g.minimum, g.maximum);
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
            arcs.scalar_access = !g.sphere;
            const float3 horizon = float3(dot(N, center), dot(N, first), dot(N, second));
            AddEmitterRoots(arcs, horizon);
            AddEmitterBoundary(arcs, g.peak_phase);
            AddEmitterRoots(arcs, float3(dot(g.peak_plane, center), dot(g.peak_plane, first), dot(g.peak_plane, second)));
            if (!g.sphere) {
                // Split the inner cone too: its slope changes at the plateau.
                // This permits a small fixed rule on each smooth interval.
                const float extent = 2.0 * g.projected_distance * ring;
                AddEmitterRoots(arcs, float3(g.inner_support_radius * g.inner_support_radius
                    - g.projected_distance * g.projected_distance - ring * ring,
                    extent * cos(g.support_phase), extent * sin(g.support_phase)));
            }
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
                const float a = GetEmitterBoundary(arcs, arc);
                const float b = GetEmitterBoundary(arcs, arc + 1u);
                if (b <= a) continue;
                const float midpoint = 0.5 * (a + b);
                if (horizon.x + horizon.y * cos(midpoint) + horizon.z * sin(midpoint) <= 0.0) continue;
                const float support_delta = abs(midpoint - g.support_phase);
                if (!g.sphere && min(support_delta, 2.0 * PI - support_delta) > support_half_width) continue;
                [loop] for (uint angular_index = 0u; angular_index < order; ++angular_index) {
                    const uint angular_node = angular_index;
                    const float angular_t = smooth ? kSmoothEmitterNodes[angular_node] : kEmitterNodes[angular_node];
                    const float angular_weight = smooth ? kSmoothEmitterWeights[angular_node] : kEmitterWeights[angular_node];
                    const float angular_sine = sin(0.5 * PI * angular_t);
                    float angular_jacobian = 0.5 * PI * sin(PI * angular_t);
                    float angular_coordinate = smooth || importance ? angular_t : angular_sine * angular_sine;
                    if (smooth || importance) angular_jacobian = 1.0;
                    const bool peak_at_start = abs(a - g.peak_phase) < 1.0e-6;
                    const bool peak_at_end = abs(b - g.peak_phase) < 1.0e-6
                        || abs(b - g.peak_phase - 2.0 * PI) < 1.0e-6;
                    if (importance && angular_peak && (peak_at_start || peak_at_end)) {
                        const float peak_distance = length(center
                            + first * cos(g.peak_phase) + second * sin(g.peak_phase));
                        const float width = 2.0 * brdf.roughness * brdf.roughness
                            * brdf.nv * peak_distance / max(length(first), 1.0e-20);
                        const float3 mapped = EmitterPeakCoordinate(angular_t,
                            width, b - a, peak_at_start);
                        angular_coordinate = mapped.x;
                        angular_jacobian = mapped.y;
                    }
                    const float phi = a + (b - a) * angular_coordinate;
                    const float3 ray = center + first * cos(phi) + second * sin(phi);
                    const float distance = length(ray);
                    if (distance <= 0.0) continue;
                    const float3 L = ray / distance;
                    float illumination = scale;
                    if (!g.sphere) {
                        illumination *= ComputeLocalLightDistanceAttenuation(ray, g.range)
                            * EmitterAngularAttenuation(light, L, g.axis);
                    }
                    const float weight = (end - start) * (b - a) * radial_weight
                        * angular_weight * radial_jacobian * angular_jacobian;
                    GgxDirectLobes lobes = (GgxDirectLobes)0;
                    if (importance) {
                        lobes.single_scattering = EvaluatePreparedGgxSingleScattering(N, V, L, brdf);
                    } else if (broad_only) {
                        lobes = EvaluatePreparedGgxBroadLobes(N, L, brdf);
                    } else {
                        lobes = EvaluatePreparedGgxDirectLobes(N, V, L, brdf);
                    }
                    AddEmitterLobes(sum, lobes, light.intensity_rgb_cd * (illumination * weight));
                }
            }
        }
    }
    return sum;
}

// The seven-point degree-five disk cubature integrates projected-area moments
// through quartic order. It is used only when source support and BRDF variation
// are smooth; clipped support and narrow highlights use the bounded peak rule.
static bool CanUseEmitterCubature(EmitterGeometry g, ForwardLocalLightRecord light,
    float3 N, float3 V, GgxDirectContext brdf)
{
    if (g.minimum > 0.0 || g.maximum < 1.0
        || g.distance - g.radius <= 0.001 || g.distance + g.radius >= g.range
        || dot(N, g.center) <= g.radius) return false;
    const float radius_ratio = g.radius / g.distance;
    if (!EmitterHasSmoothResponse(g, N, V, brdf)) return false;
    const float3 center_direction = g.center / g.distance;
    if (!g.sphere) {
        const float cosine = saturate(-dot(center_direction, g.axis));
        const float extent = sqrt(saturate(1.0 - cosine * cosine)) * radius_ratio;
        const float center = cosine * sqrt(1.0 - radius_ratio * radius_ratio);
        const float inner = 1.0 - 2.0 * light.inner_cone_sin_half_squared;
        const float outer = 1.0 - 2.0 * light.outer_cone_sin_half_squared;
        // Do not straddle either non-smooth cone boundary.
        if (center - extent <= outer + 2.0e-6) return false;
        if (center + extent >= inner - 2.0e-6
            && center - extent <= inner + 2.0e-6) return false;
    }
    return true;
}

static void AccumulateEmitterCubatureNode(inout GgxDirectLobes sum,
    EmitterGeometry g, ForwardLocalLightRecord light, float3 N, float3 V,
    GgxDirectContext brdf, float2 node, float weight)
{
    const float3 offset = g.radius * (g.first * node.x + g.second * node.y);
    float3 L;
    float illumination;
    if (g.sphere) {
        const float ratio = g.radius / g.distance;
        const float u = dot(node, node);
        const float cosine = sqrt(1.0 - ratio * ratio * u);
        const float surface_distance = g.distance * cosine - g.radius * sqrt(1.0 - u);
        const float q = surface_distance / g.range;
        const float window = saturate(1.0 - q * q * q * q);
        L = g.axis * cosine + offset / g.distance;
        illumination = window * window / (g.distance * g.distance * cosine);
    } else {
        const float3 ray = g.center + offset;
        L = normalize(ray);
        illumination = ComputeLocalLightDistanceAttenuation(ray, g.range)
            * EmitterAngularAttenuation(light, L, g.axis);
    }
    AddEmitterLobes(sum, EvaluatePreparedGgxDirectLobes(N, V, L, brdf),
        light.intensity_rgb_cd * (illumination * weight));
}

static GgxDirectLobes EvaluateEmitterCubature(EmitterGeometry g,
    ForwardLocalLightRecord light, float3 N, float3 V, GgxDirectContext brdf)
{
    static const float2 nodes[7] = {
        float2(0.0, 0.0), float2(0.8164965809, 0.0), float2(0.4082482905, 0.7071067812),
        float2(-0.4082482905, 0.7071067812), float2(-0.8164965809, 0.0),
        float2(-0.4082482905, -0.7071067812), float2(0.4082482905, -0.7071067812)
    };
    GgxDirectLobes sum = (GgxDirectLobes)0;
    // The sphere's longer node evaluation benefits from a compact loop. Disk
    // expansion exposes its fixed offsets without expanding the sphere path.
    if (g.sphere) {
        [loop] for (uint sample_index = 0u; sample_index < 7u; ++sample_index) {
            AccumulateEmitterCubatureNode(sum, g, light, N, V, brdf,
                nodes[sample_index], sample_index == 0u ? 0.25 : 0.125);
        }
    } else {
        [unroll] for (uint sample_index = 0u; sample_index < 7u; ++sample_index) {
            AccumulateEmitterCubatureNode(sum, g, light, N, V, brdf,
                nodes[sample_index], sample_index == 0u ? 0.25 : 0.125);
        }
    }
    return sum;
}

// Conservative rejection uses the complete source extent, never its center alone.
static bool LocalEmitterCanContribute(ForwardLocalLightRecord light,
    float3 receiver, float3 N)
{
    const float3 ray = light.position_ws - receiver;
    const float extent = light.range_m + light.source_radius_m;
    return light.range_m > 0.0 && any(light.intensity_rgb_cd != 0.0)
        && dot(ray, ray) < extent * extent
        && dot(N, ray) + light.source_radius_m > 0.0;
}

static GgxDirectLobes EvaluatePreparedLocalEmitterLobes(ForwardLocalLightRecord light,
    float3 receiver, float3 N, float3 V, GgxDirectContext brdf)
{
    GgxDirectLobes result = (GgxDirectLobes)0;
    if (brdf.nv <= 0.0 || !LocalEmitterCanContribute(light, receiver, N)) return result;
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
        if (attenuation <= 0.0) return result;
        AddEmitterLobes(result, EvaluatePreparedGgxDirectLobes(N, V, L, brdf),
            light.intensity_rgb_cd * attenuation);
        return result;
    }
    EmitterGeometry geometry;
    if (!PrepareEmitterGeometry(light, receiver, N, geometry)) return result;
    if (CanUseEmitterCubature(geometry, light, N, V, brdf))
        return EvaluateEmitterCubature(geometry, light, N, V, brdf);
    PrepareEmitterPeak(geometry, N, V);
    const bool narrow_peak = brdf.roughness < 0.5 && geometry.peak_supported
        && !EmitterHasSmoothResponse(geometry, N, V, brdf);
    result = IntegrateEmitterRule(geometry, light, N, V, brdf, false, narrow_peak);
    if (narrow_peak) {
        const GgxDirectLobes specular = IntegrateEmitterRule(geometry, light, N, V, brdf, true, false);
        result.single_scattering = specular.single_scattering;
    }
    return result;
}

static GgxDirectLobes EvaluateLocalEmitterLobes(ForwardLocalLightRecord light,
    float3 receiver, float3 N, float3 V, float3 F0, float3 rho, float roughness,
    LightingFrameBindings lighting)
{
    return EvaluatePreparedLocalEmitterLobes(light, receiver, N, V,
        PrepareGgxDirect(N, V, F0, rho, roughness, lighting));
}

static float3 EvaluateLocalEmitterResponse(ForwardLocalLightRecord light,
    float3 receiver, float3 N, float3 V, float3 F0, float3 rho, float roughness,
    LightingFrameBindings lighting)
{
    const GgxDirectLobes lobes = EvaluateLocalEmitterLobes(light, receiver, N, V, F0, rho, roughness, lighting);
    return lobes.single_scattering + lobes.multiple_scattering + lobes.diffuse;
}

#endif
