//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_CUBEMAP_GEOMETRY_HLSLI
#define OXYGEN_VORTEX_CUBEMAP_GEOMETRY_HLSLI

// Hardware faces +X/-X/+Y/-Y/+Z/-Z, with top-down UVs. Face indices are 0..5.
static float3 CubemapDirectionFromFaceUv(uint face, float2 uv)
{
    float2 p = uv * 2.0 - 1.0;
    float3 d;
    switch (face) {
    case 0u: d = float3(1.0, -p.y, -p.x); break;
    case 1u: d = float3(-1.0, -p.y, p.x); break;
    case 2u: d = float3(p.x, 1.0, p.y); break;
    case 3u: d = float3(p.x, -1.0, -p.y); break;
    case 4u: d = float3(p.x, -p.y, 1.0); break;
    default: d = float3(-p.x, -p.y, -1.0); break;
    }
    return normalize(d);
}

// Finite, nonzero cube directions. Dominant-axis ties choose X, then Y, then Z.
// Normalization belongs to the caller; this inverse uses component ratios.
static inline void CubemapFaceUvFromDirection(
  float3 dir, out uint face_index, out float2 uv)
{
  float3 a = abs(dir);
  float s = 0.0;
  float t = 0.0;

  if (a.x >= a.y && a.x >= a.z) {
    if (dir.x >= 0.0) {
      face_index = 0u; // +X
      s = -dir.z / a.x;
      t = dir.y / a.x;
    } else {
      face_index = 1u; // -X
      s = dir.z / a.x;
      t = dir.y / a.x;
    }
  } else if (a.y >= a.x && a.y >= a.z) {
    if (dir.y >= 0.0) {
      face_index = 2u; // +Y
      s = dir.x / a.y;
      t = -dir.z / a.y;
    } else {
      face_index = 3u; // -Y
      s = dir.x / a.y;
      t = dir.z / a.y;
    }
  } else {
    if (dir.z >= 0.0) {
      face_index = 4u; // +Z
      s = dir.x / a.z;
      t = dir.y / a.z;
    } else {
      face_index = 5u; // -Z
      s = -dir.x / a.z;
      t = dir.y / a.z;
    }
  }

  uv = float2(0.5 * (s + 1.0), 0.5 * (1.0 - t));
}

// Exact texel boundary area in steradians. Size is positive; pixel lies in the face.
static float CubemapTexelSolidAngle(uint2 pixel, uint size)
{
    float2 lo = 2.0 * float2(pixel) / float(size) - 1.0;
    float2 hi = 2.0 * float2(pixel + 1u) / float(size) - 1.0;
    return atan2(hi.x * hi.y, sqrt(dot(hi, hi) + 1.0))
        - atan2(lo.x * hi.y, sqrt(lo.x * lo.x + hi.y * hi.y + 1.0))
        - atan2(hi.x * lo.y, sqrt(hi.x * hi.x + lo.y * lo.y + 1.0))
        + atan2(lo.x * lo.y, sqrt(dot(lo, lo) + 1.0));
}

// Oxygen is +Z up, -Y forward; hardware cube coordinates are +Y up, +Z forward.
static inline float3 CubemapSamplingDirFromOxygenWS(float3 dir_ws)
{
    return float3(dir_ws.x, dir_ws.z, -dir_ws.y);
}

static inline float3 OxygenDirFromCubemapSamplingDir(float3 dir_cube)
{
    return float3(dir_cube.x, -dir_cube.z, dir_cube.y);
}

// Rotates an Oxygen world direction about +Z. Sky display and sky-light
// capture share it so an authored cubemap rotation lights what it shows.
static inline float3 RotateDirectionAroundOxygenUp(float3 direction, float radians)
{
    float s, c;
    sincos(radians, s, c);
    return float3(c * direction.x - s * direction.y,
        s * direction.x + c * direction.y, direction.z);
}

#endif // OXYGEN_VORTEX_CUBEMAP_GEOMETRY_HLSLI
