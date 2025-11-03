// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Windows.Foundation;

namespace DroidNet.Coordinates;

/// <summary>
///     Provides a fluent API for spatial point transformations and operations within a specific coordinate space.
/// </summary>
/// <typeparam name="TSpace">The type representing the spatial coordinate system.</typeparam>
public class SpatialFlow<TSpace>(SpatialPoint<TSpace> point, ISpatialMapper mapper)
{
    private readonly ISpatialMapper mapper = mapper;

    /// <summary>
    ///     Gets the spatial point associated with this flow.
    /// </summary>
    public SpatialPoint<TSpace> Point { get; } = point;

    /// <summary>
    ///     Converts the spatial point to screen coordinate space.
    /// </summary>
    /// <returns>A new spatial flow in screen coordinate space.</returns>
    public SpatialFlow<ScreenSpace> ToScreen() => new(this.mapper.ToScreen(this.Point), this.mapper);

    /// <summary>
    ///     Converts the spatial point to window coordinate space.
    /// </summary>
    /// <returns>A new spatial flow in window coordinate space.</returns>
    public SpatialFlow<WindowSpace> ToWindow() => new(this.mapper.ToWindow(this.Point), this.mapper);

    /// <summary>
    ///     Converts the spatial point to element coordinate space.
    /// </summary>
    /// <returns>A new spatial flow in element coordinate space.</returns>
    public SpatialFlow<ElementSpace> ToElement() => new(this.mapper.ToElement(this.Point), this.mapper);

    /// <summary>
    ///     Gets the underlying point coordinates.
    /// </summary>
    /// <returns>The point coordinates.</returns>
    public Point ToPoint() => this.Point.Point;

    /// <summary>
    ///     Offsets the spatial point by another spatial point in the same coordinate space.
    /// </summary>
    /// <param name="offset">The offset point to add.</param>
    /// <returns>A new spatial flow with the offset applied.</returns>
    public SpatialFlow<TSpace> OffsetBy(SpatialPoint<TSpace> offset)
    {
        var p = new Point(this.Point.Point.X + offset.Point.X, this.Point.Point.Y + offset.Point.Y);
        return new(new SpatialPoint<TSpace>(p), this.mapper);
    }

    /// <summary>
    ///     Offsets the spatial point by the specified X coordinate.
    /// </summary>
    /// <param name="dx">The X offset to add.</param>
    /// <returns>A new spatial flow with the X offset applied.</returns>
    public SpatialFlow<TSpace> OffsetX(double dx)
    {
        var p = new Point(this.Point.Point.X + dx, this.Point.Point.Y);
        return new(new SpatialPoint<TSpace>(p), this.mapper);
    }

    /// <summary>
    ///     Offsets the spatial point by the specified Y coordinate.
    /// </summary>
    /// <param name="dy">The Y offset to add.</param>
    /// <returns>A new spatial flow with the Y offset applied.</returns>
    public SpatialFlow<TSpace> OffsetY(double dy)
    {
        var p = new Point(this.Point.Point.X, this.Point.Point.Y + dy);
        return new(new SpatialPoint<TSpace>(p), this.mapper);
    }

    /// <summary>
    ///     Offsets the spatial point by the specified X and Y coordinates.
    /// </summary>
    /// <param name="dx">The X offset to add.</param>
    /// <param name="dy">The Y offset to add.</param>
    /// <returns>A new spatial flow with the offsets applied.</returns>
    public SpatialFlow<TSpace> Offset(double dx, double dy)
    {
        var p = new Point(this.Point.Point.X + dx, this.Point.Point.Y + dy);
        return new(new SpatialPoint<TSpace>(p), this.mapper);
    }
}
