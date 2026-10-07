//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SHARED_PACKUNPACK_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SHARED_PACKUNPACK_HLSLI

static inline float2 OctahedronEncode(float3 normal)
{
    const float length_sum
        = max(abs(normal.x) + abs(normal.y) + abs(normal.z), 1e-6f);
    float3 projected = normal / length_sum;

    if (projected.z < 0.0f) {
        const float2 sign_xy = float2(
            projected.x >= 0.0f ? 1.0f : -1.0f,
            projected.y >= 0.0f ? 1.0f : -1.0f);
        projected.xy = (1.0f - abs(projected.yx)) * sign_xy;
    }

    return projected.xy;
}

static inline float3 OctahedronDecode(float2 encoded)
{
    float3 normal = float3(
        encoded.x, encoded.y, 1.0f - abs(encoded.x) - abs(encoded.y));

    if (normal.z < 0.0f) {
        const float2 sign_xy = float2(
            normal.x >= 0.0f ? 1.0f : -1.0f,
            normal.y >= 0.0f ? 1.0f : -1.0f);
        normal.xy = (1.0f - abs(normal.yx)) * sign_xy;
    }

    return normalize(normal);
}

// Signed [-1, 1] values stored in UNORM lanes of `max_code` steps use codes
// [0, max_code - 1] centred on (max_code - 1) / 2, so 0 and +/-1 round-trip
// exactly and quantization is symmetric. The `* 0.5 + 0.5` mapping has no
// exact centre and tilts axis-aligned octahedral normals by half a step.
//
// The code is chosen here and emitted a quarter step above its value: render
// target exports may first narrow to FP16 toward zero (losing up to half a
// 10-bit step in [0.5, 1)) before the UNORM conversion rounds, so an exact
// code/max value can land one code low. The offset survives both paths.
static inline float2 EncodeSignedUnormExactZero(float2 value, float2 max_code)
{
    const float2 half_range = 0.5f * (max_code - 1.0f);
    const float2 code = round(clamp(value, -1.0f, 1.0f) * half_range + half_range);
    return (code + 0.25f) / max_code;
}

static inline float2 DecodeSignedUnormExactZero(float2 stored, float2 max_code)
{
    const float2 half_range = 0.5f * (max_code - 1.0f);
    return clamp((round(stored * max_code) - half_range) / half_range, -1.0f, 1.0f);
}

static inline float Pack2x8Unorm(float first, float second)
{
    const uint first_unorm = uint(saturate(first) * 255.0f + 0.5f);
    const uint second_unorm = uint(saturate(second) * 255.0f + 0.5f);
    return float((first_unorm << 8u) | second_unorm) / 65535.0f;
}

static inline float2 Unpack2x8Unorm(float packed)
{
    const uint value = uint(saturate(packed) * 65535.0f + 0.5f);
    return float2(
        float((value >> 8u) & 0xFFu) / 255.0f,
        float(value & 0xFFu) / 255.0f);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SHARED_PACKUNPACK_HLSLI
