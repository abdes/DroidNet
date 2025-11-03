// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Coordinates;

/// <summary>
///     Provides methods for mapping spatial points between different coordinate systems.
/// </summary>
public interface ISpatialMapper
{
    /// <summary>
    ///     Converts a spatial point from the source coordinate space to the target coordinate space.
    /// </summary>
    /// <typeparam name="TSource">The type representing the source coordinate space.</typeparam>
    /// <typeparam name="TTarget">The type representing the target coordinate space.</typeparam>
    /// <param name="point">The spatial point to convert.</param>
    /// <returns>A new spatial point in the target coordinate space.</returns>
    public SpatialPoint<TTarget> Convert<TSource, TTarget>(SpatialPoint<TSource> point);

    /// <summary>
    ///     Converts a spatial point to screen coordinate space.
    /// </summary>
    /// <typeparam name="TSource">The type representing the source coordinate space.</typeparam>
    /// <param name="point">The spatial point to convert.</param>
    /// <returns>A new spatial point in screen coordinate space.</returns>
    public SpatialPoint<ScreenSpace> ToScreen<TSource>(SpatialPoint<TSource> point);

    /// <summary>
    ///     Converts a spatial point to window coordinate space.
    /// </summary>
    /// <typeparam name="TSource">The type representing the source coordinate space.</typeparam>
    /// <param name="point">The spatial point to convert.</param>
    /// <returns>A new spatial point in window coordinate space.</returns>
    public SpatialPoint<WindowSpace> ToWindow<TSource>(SpatialPoint<TSource> point);

    /// <summary>
    ///     Converts a spatial point to element coordinate space.
    /// </summary>
    /// <typeparam name="TSource">The type representing the source coordinate space.</typeparam>
    /// <param name="point">The spatial point to convert.</param>
    /// <returns>A new spatial point in element coordinate space.</returns>
    public SpatialPoint<ElementSpace> ToElement<TSource>(SpatialPoint<TSource> point);
}
