// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Windows.Foundation;

namespace DroidNet.Coordinates;

/// <summary>
///     Provides extension methods for converting between <see cref="SpatialPoint{TSpace}"/>
///     instances and <see cref="Point"/> instances.
/// </summary>
public static class SpatialPointExtensions
{
    /// <summary>
    ///     Extracts the underlying <see cref="Point"/> from a <see cref="SpatialPoint{TSpace}"/>.
    /// </summary>
    /// <typeparam name="TSpace">The type representing the spatial coordinate system.</typeparam>
    /// <param name="spatial">The spatial point to extract from.</param>
    /// <returns>The underlying point coordinates.</returns>
    public static Point ToPoint<TSpace>(this SpatialPoint<TSpace> spatial) => spatial.Point;

    /// <summary>
    ///     Converts a <see cref="Point"/> to a spatial point in element coordinate space.
    /// </summary>
    /// <param name="p">The point to convert.</param>
    /// <returns>A spatial point in element coordinate space.</returns>
    public static SpatialPoint<ElementSpace> AsElement(this Point p) => new(p);

    /// <summary>
    ///     Converts a <see cref="Point"/> to a spatial point in window coordinate space.
    /// </summary>
    /// <param name="p">The point to convert.</param>
    /// <returns>A spatial point in window coordinate space.</returns>
    public static SpatialPoint<WindowSpace> AsWindow(this Point p) => new(p);

    /// <summary>
    ///     Converts a <see cref="Point"/> to a spatial point in screen coordinate space.
    /// </summary>
    /// <param name="p">The point to convert.</param>
    /// <returns>A spatial point in screen coordinate space.</returns>
    public static SpatialPoint<ScreenSpace> AsScreen(this Point p) => new(p);
}
