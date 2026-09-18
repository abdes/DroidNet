//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_TONE_MAPPING_HLSLI
#define VORTEX_TONE_MAPPING_HLSLI

#include "Vortex/Shared/ColorSpace.hlsli"

static const float3x3 kAcesInputMatrix = {
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};
static const float3x3 kAcesOutputMatrix = {
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

static const float3 kAcesNumerator = float3(1.0, 0.0245786, -0.000090537);
static const float3 kAcesDenominator = float3(0.983729, 0.4329510, 0.238081);

static float3 AcesResponse(float3 color)
{
    const float3 inverse_scale = rcp(max(abs(color), 1.0));
    const float3 scaled = color * inverse_scale;
    const float3 a = scaled * (scaled + kAcesNumerator.y * inverse_scale)
        + kAcesNumerator.z * inverse_scale * inverse_scale;
    const float3 b = scaled * (kAcesDenominator.x * scaled + kAcesDenominator.y * inverse_scale)
        + kAcesDenominator.z * inverse_scale * inverse_scale;
    return a / b;
}

static float3 ACESFitted(float3 color)
{
    color = AcesResponse(mul(kAcesInputMatrix, color));
    color = mul(kAcesOutputMatrix, color);
    return saturate(color);
}

static float3 Reinhard(float3 color)
{
    return color / (color + 1.0);
}

static const float kFilmicA = 0.15;
static const float kFilmicB = 0.50;
static const float kFilmicC = 0.10;
static const float kFilmicD = 0.20;
static const float kFilmicE = 0.02;
static const float kFilmicF = 0.30;
static float3 Uncharted2Tonemap(float3 x)
{
    const float3 inverse_scale = rcp(max(abs(x), 1.0));
    const float3 scaled = x * inverse_scale;
    return ((scaled * (kFilmicA * scaled + kFilmicC * kFilmicB * inverse_scale) + kFilmicD * kFilmicE * inverse_scale * inverse_scale)
        / (scaled * (kFilmicA * scaled + kFilmicB * inverse_scale) + kFilmicD * kFilmicF * inverse_scale * inverse_scale)) - kFilmicE / kFilmicF;
}

static float3 Filmic(float3 color)
{
    static const float white_point = 11.2;
    static const float exposure_bias = 2.0;
    const float3 curr = Uncharted2Tonemap(exposure_bias * color);
    const float3 white_scale = 1.0 / Uncharted2Tonemap(white_point);
    return curr * white_scale;
}

static float DitherBayer4x4(uint2 pixel_pos)
{
    static const float kBayer4x4[16] = {
        0.0,  8.0,  2.0, 10.0,
        12.0, 4.0, 14.0,  6.0,
        3.0, 11.0,  1.0,  9.0,
        15.0, 7.0, 13.0,  5.0
    };

    const uint index = (pixel_pos.x & 3u) | ((pixel_pos.y & 3u) << 2u);
    return (kBayer4x4[index] / 16.0) - 0.5;
}

static float3 MapForeground(float3 color, uint tone_mapper, float gamma)
{
    switch (tone_mapper) {
        case 1u: color = ACESFitted(color); break;
        case 2u: color = Filmic(color); break;
        case 3u: color = Reinhard(color); break;
        default: color = saturate(color); break;
    }
    return pow(max(color, 0.0f), 1.0f / max(gamma, 1.0e-4f));
}

#endif
