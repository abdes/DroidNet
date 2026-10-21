// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>
/// Presents a linear radiance multiplier in exposure stops, as a physical
/// renderer's brightness controls are: EV 0 is the authored radiance, each
/// stop doubles it.
/// </summary>
internal static class RadianceExposure
{
    /// <summary>The lowest presented stop; a zero multiplier displays as this.</summary>
    internal const float MinimumEv = -16f;

    /// <summary>Converts a multiplier to exposure stops.</summary>
    /// <param name="scale">The linear multiplier.</param>
    /// <returns>The stops, at least <see cref="MinimumEv"/>.</returns>
    internal static float ToEv(float scale) => scale > 0f ? MathF.Max(MathF.Log2(scale), MinimumEv) : MinimumEv;

    /// <summary>Converts exposure stops to a multiplier.</summary>
    /// <param name="ev">The stops.</param>
    /// <returns>The linear multiplier.</returns>
    internal static float ToScale(float ev) => float.IsFinite(ev) ? MathF.Pow(2f, ev) : float.NaN;
}
