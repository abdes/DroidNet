//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_PRISTINEGRID_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_PRISTINEGRID_HLSLI

static const float kPristineGridDerivativeEpsilon = 1e-6;
static const float kPristineGridAAFactor = 1.5;

static inline float2 PristineGridDerivativeLength(float2 ddx_uv, float2 ddy_uv)
{
    return max(
        float2(length(float2(ddx_uv.x, ddy_uv.x)),
               length(float2(ddx_uv.y, ddy_uv.y))),
        float2(kPristineGridDerivativeEpsilon, kPristineGridDerivativeEpsilon));
}

static inline float2 PristineGridDerivativeLength(float2 uv)
{
    return PristineGridDerivativeLength(ddx(uv), ddy(uv));
}

static inline float PristineLineDerivativeLength(float ddx_uv, float ddy_uv)
{
    return max(length(float2(ddx_uv, ddy_uv)), kPristineGridDerivativeEpsilon);
}

static inline float PristineLineDerivativeLength(float uv)
{
    return PristineLineDerivativeLength(ddx(uv), ddy(uv));
}

static inline float PristineGridGrad(
    float2 uv,
    float2 ddx_uv,
    float2 ddy_uv,
    float2 line_width)
{
    const float2 width = saturate(line_width);
    const float2 uv_deriv = PristineGridDerivativeLength(ddx_uv, ddy_uv);
    const bool2 invert_line = bool2(width.x > 0.5, width.y > 0.5);

    float2 target_width = float2(
        invert_line.x ? 1.0 - width.x : width.x,
        invert_line.y ? 1.0 - width.y : width.y);

    const float2 draw_width = clamp(target_width, uv_deriv, 0.5);
    const float2 line_aa = uv_deriv * kPristineGridAAFactor;

    float2 grid_uv = abs(frac(uv) * 2.0 - 1.0);
    grid_uv.x = invert_line.x ? grid_uv.x : 1.0 - grid_uv.x;
    grid_uv.y = invert_line.y ? grid_uv.y : 1.0 - grid_uv.y;

    float2 grid2 = smoothstep(draw_width + line_aa, draw_width - line_aa, grid_uv);
    grid2 *= saturate(target_width / draw_width);
    grid2 = lerp(grid2, target_width, saturate(uv_deriv * 2.0 - 1.0));

    grid2.x = invert_line.x ? 1.0 - grid2.x : grid2.x;
    grid2.y = invert_line.y ? 1.0 - grid2.y : grid2.y;

    return lerp(grid2.x, 1.0, grid2.y);
}

static inline float PristineGrid(float2 uv, float2 line_width)
{
    return PristineGridGrad(uv, ddx(uv), ddy(uv), line_width);
}

static inline float PristineLineGrad(
    float uv,
    float ddx_uv,
    float ddy_uv,
    float line_width)
{
    const float uv_deriv = PristineLineDerivativeLength(ddx_uv, ddy_uv);
    const float draw_width = max(line_width, uv_deriv);
    const float line_aa = uv_deriv * kPristineGridAAFactor;
    const float line_uv = abs(uv * 2.0);
    float line_mask = smoothstep(draw_width + line_aa, draw_width - line_aa, line_uv);
    line_mask *= saturate(line_width / draw_width);
    return line_mask;
}

static inline float PristineLine(float uv, float line_width)
{
    return PristineLineGrad(uv, ddx(uv), ddy(uv), line_width);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_PRISTINEGRID_HLSLI
