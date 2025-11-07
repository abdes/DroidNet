// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Coordinates;
using DroidNet.Tests;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

/// <summary>
/// Common test helper utilities for drag-related tests.
/// </summary>
[ExcludeFromCodeCoverage]
internal static class DragTestHelpers
{
    /// <summary>
    /// Creates a <see cref="SpatialPoint{ScreenSpace}"/> from logical screen coordinates.
    /// </summary>
    /// <param name="x">The X coordinate.</param>
    /// <param name="y">The Y coordinate.</param>
    /// <returns>A spatial point in screen space.</returns>
    public static SpatialPoint<ScreenSpace> ScreenPoint(double x, double y)
        => new(new Point(x, y));

    /// <summary>
    /// Creates a <see cref="SpatialPoint{PhysicalScreenSpace}"/> from physical screen coordinates.
    /// </summary>
    /// <param name="x">The X coordinate.</param>
    /// <param name="y">The Y coordinate.</param>
    /// <returns>A spatial point in physical screen space.</returns>
    public static SpatialPoint<PhysicalScreenSpace> PhysicalPoint(double x, double y)
        => new(new Point(x, y));

    /// <summary>
    /// Determines whether two points are close within a specified tolerance.
    /// </summary>
    /// <param name="left">The first point.</param>
    /// <param name="right">The second point.</param>
    /// <param name="tolerance">The maximum allowed difference (default: 0.1).</param>
    /// <returns><see langword="true"/> if points are within tolerance; otherwise <see langword="false"/>.</returns>
    public static bool AreClose(Point left, Point right, double tolerance = 0.1)
        => Math.Abs(left.X - right.X) < tolerance && Math.Abs(left.Y - right.Y) < tolerance;

    /// <summary>
    /// Waits for the composition rendering to complete.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    public static async Task WaitForRenderAsync()
        => _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
}
