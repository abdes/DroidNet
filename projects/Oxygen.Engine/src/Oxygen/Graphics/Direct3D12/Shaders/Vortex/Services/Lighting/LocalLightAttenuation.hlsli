//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_LOCAL_LIGHT_ATTENUATION_HLSLI
#define OXYGEN_VORTEX_LOCAL_LIGHT_ATTENUATION_HLSLI

static float ComputeSpotLightAngularAttenuation(float3 direction_to_source,
    float3 emitted_axis, float outer_cosine, float inverse_cosine_width)
{
    const float cosine = -dot(direction_to_source, emitted_axis);
    if (inverse_cosine_width == 0.0) return cosine >= outer_cosine ? 1.0 : 0.0;
    const float ramp = saturate((cosine - outer_cosine) * inverse_cosine_width);
    return ramp * ramp;
}

static float ComputeLocalLightDistanceAttenuation(float3 light_vector, float range_m)
{
    if (range_m <= 0.0) return 0.0;
    const float distance_squared = dot(light_vector, light_vector);
    if (distance_squared <= 0.0 || distance_squared >= range_m * range_m) return 0.0;
    const float normalized_distance_squared = distance_squared / (range_m * range_m);
    float window = saturate(1.0 - normalized_distance_squared * normalized_distance_squared);
    return window * window / max(distance_squared, 0.001 * 0.001);
}

#endif
