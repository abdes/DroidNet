//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_RENDERER_DEBUG_HELPERS_HLSLI
#define OXYGEN_RENDERER_DEBUG_HELPERS_HLSLI

float3 HeatMapColor(float t) {
    t = saturate(t);
    float3 colors[4] = { float3(0,0,0), float3(0,1,0), float3(1,1,0), float3(1,0,0) };
    float segment = t * 3.0;
    uint idx = min((uint)floor(segment), 2);
    return lerp(colors[idx], colors[idx + 1], frac(segment));
}

float3 DepthSliceColor(uint slice, uint max_slices) {
    if (max_slices <= 1) return 0.5.xxx;
    static const float3 colors[8] = { float3(1,0,0), float3(1,0.5,0), float3(1,1,0), float3(0,1,0), float3(1,0,0.5), float3(0.5,0,0), float3(0,0.5,0), float3(1,1,0.5) };
    return colors[slice % 8];
}

float3 ClusterIndexColor(uint3 cluster_id) {
    uint checker = (cluster_id.x + cluster_id.y + cluster_id.z) % 2;
    float3 base = float3(frac(float(cluster_id.x)*0.123), frac(float(cluster_id.y)*0.567), frac(float(cluster_id.z)*0.901));
    return lerp(base * 0.5, base, float(checker));
}

#endif // OXYGEN_RENDERER_DEBUG_HELPERS_HLSLI
