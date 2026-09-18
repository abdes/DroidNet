//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef VORTEX_TRANSMITTANCE_MATH_HLSLI
#define VORTEX_TRANSMITTANCE_MATH_HLSLI

// Avoid losing optically thin scattering when exp(-x) rounds to one. The
// fifth-order series has relative truncation error below 1.4e-13 at |x|=.01;
// ordinary FP32 rounding dominates. Signed x also supports height integrals.
float OneMinusExpNegative(float x)
{
    if (abs(x) < 0.01)
        return x * (1.0 + x * (-0.5 + x * (1.0 / 6.0 + x * (-1.0 / 24.0 + x / 120.0))));
    return 1.0 - exp(-x);
}

float3 OneMinusExpNegative(float3 x)
{
    return float3(OneMinusExpNegative(x.x), OneMinusExpNegative(x.y), OneMinusExpNegative(x.z));
}

// Integral of exp(-extinction*t) over [0,distance], including extinction=0.
// Evaluate the ratio's series directly so a small numerator is never divided
// by an arbitrary extinction floor.
float IntegratedTransmittance(float extinction, float distance)
{
    const float x = extinction * distance;
    if (abs(x) < 0.01)
        return distance * (1.0 + x * (-0.5 + x * (1.0 / 6.0
            + x * (-1.0 / 24.0 + x / 120.0))));
    return OneMinusExpNegative(x) / extinction;
}

float3 IntegratedTransmittance(float3 extinction, float distance)
{
    return float3(IntegratedTransmittance(extinction.x, distance),
        IntegratedTransmittance(extinction.y, distance),
        IntegratedTransmittance(extinction.z, distance));
}

// Equivalent to -log(max(1-opacity,1e-6)), retaining a small nonzero opacity
// through the complement. Preserve the existing maximum optical-depth cap.
float OpticalDepthFromOpacity(float opacity)
{
    opacity = saturate(opacity);
    if (opacity < 0.01)
        return opacity * (1.0 + opacity * (0.5 + opacity * (1.0 / 3.0
            + opacity * (0.25 + opacity / 5.0))));
    return -log(max(1.0 - opacity, 1.0e-6));
}

#endif
