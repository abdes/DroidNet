// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Windows.Foundation;

namespace DroidNet.Coordinates;

/// <summary>
///     Conversions between <see cref="SpatialPoint{TSpace}"/> and untyped <see cref="Point"/>.
/// </summary>
public static class SpatialPointExtensions
{
    /// <summary>
    ///     Extracts the underlying untyped point.
    /// </summary>
    /// <typeparam name="TSpace">Coordinate space marker type.</typeparam>
    /// <param name="spatial">The spatial point.</param>
    /// <returns>The raw coordinates.</returns>
    public static Point ToPoint<TSpace>(this SpatialPoint<TSpace> spatial) => spatial.Point;

    /// <summary>Wraps a point as ElementSpace.</summary>
    /// <param name="p">The point.</param>
    /// <returns>A spatial point in element coordinates.</returns>
    public static SpatialPoint<ElementSpace> AsElement(this Point p) => new(p);

    /// <summary>Wraps a point as WindowSpace.</summary>
    /// <param name="p">The point.</param>
    /// <returns>A spatial point in window-client coordinates.</returns>
    public static SpatialPoint<WindowSpace> AsWindow(this Point p) => new(p);

    /// <summary>Wraps a point as ScreenSpace.</summary>
    /// <param name="p">The point.</param>
    /// <returns>A spatial point in desktop-global logical coordinates.</returns>
    public static SpatialPoint<ScreenSpace> AsScreen(this Point p) => new(p);

    /// <summary>
    ///     Wraps a point as PhysicalScreenSpace. The underlying Point stores physical pixels;
    ///     round to integers at Win32 boundaries when constructing native structures.
    /// </summary>
    /// <param name="p">The point.</param>
    /// <returns>A spatial point in physical screen pixels.</returns>
    public static SpatialPoint<PhysicalScreenSpace> AsPhysicalScreen(this Point p) => new(p);
}
