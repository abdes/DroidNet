// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Projects authored exposure keys into the view's compact curve preview.</summary>
internal static class InspectorCurvePresentation
{
    /// <summary>Creates the view-owned polyline points without changing authored keys.</summary>
    /// <param name="keys">The authored exposure compensation curve.</param>
    /// <returns>The points for the 160-by-32-DIP preview.</returns>
    public static PointCollection CreatePoints(ImmutableArray<ExposureCompensationKeyData> keys)
    {
        PointCollection points = [];
        if (keys.IsDefaultOrEmpty)
        {
            points.Add(new(0, 16));
            points.Add(new(160, 16));
            return points;
        }

        var minX = keys.Min(static key => key.MeteredEv);
        var maxX = keys.Max(static key => key.MeteredEv);
        var minY = keys.Min(static key => key.CompensationEv);
        var maxY = keys.Max(static key => key.CompensationEv);
        var xRange = MathF.Max(maxX - minX, 1.0f);
        var yRange = MathF.Max(maxY - minY, 1.0f);
        foreach (var key in keys)
        {
            points.Add(new(
                (key.MeteredEv - minX) / xRange * 160,
                32 - ((key.CompensationEv - minY) / yRange * 28)));
        }

        return points;
    }
}
