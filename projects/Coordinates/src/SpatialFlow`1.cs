// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Windows.Foundation;

namespace DroidNet.Coordinates;

/// <summary>
///     Fluent API for chaining coordinate transformations and offsets within a typed space.
/// </summary>
/// <typeparam name="TSpace">Coordinate space marker type.</typeparam>
public class SpatialFlow<TSpace>(SpatialPoint<TSpace> point, ISpatialMapper mapper)
{
    private readonly ISpatialMapper mapper = mapper;

    /// <summary>
    ///     Gets the current point in this flow.
    /// </summary>
    public SpatialPoint<TSpace> Point { get; } = point;

    /// <summary>
    ///     Converts to ScreenSpace.
    /// </summary>
    /// <returns>A flow in screen coordinates.</returns>
    public SpatialFlow<ScreenSpace> ToScreen() => new(this.mapper.ToScreen(this.Point), this.mapper);

    /// <summary>
    ///     Converts to WindowSpace.
    /// </summary>
    /// <returns>A flow in window-client coordinates.</returns>
    public SpatialFlow<WindowSpace> ToWindow() => new(this.mapper.ToWindow(this.Point), this.mapper);

    /// <summary>
    ///     Converts to ElementSpace.
    /// </summary>
    /// <returns>A flow in element-local coordinates.</returns>
    public SpatialFlow<ElementSpace> ToElement() => new(this.mapper.ToElement(this.Point), this.mapper);

    /// <summary>
    ///     Extracts the underlying untyped point, ending the flow.
    /// </summary>
    /// <returns>The raw coordinates.</returns>
    public Point ToPoint() => this.Point.Point;

    /// <summary>
    ///     Applies an offset vector in the same space.
    /// </summary>
    /// <param name="offset">The offset to add.</param>
    /// <returns>A new flow with the offset applied.</returns>
    public SpatialFlow<TSpace> OffsetBy(SpatialPoint<TSpace> offset)
    {
        var p = new Point(this.Point.Point.X + offset.Point.X, this.Point.Point.Y + offset.Point.Y);
        return new(new SpatialPoint<TSpace>(p), this.mapper);
    }

    /// <summary>
    ///     Offsets the X coordinate.
    /// </summary>
    /// <param name="dx">The X offset.</param>
    /// <returns>A new flow with X offset applied.</returns>
    public SpatialFlow<TSpace> OffsetX(double dx)
    {
        var p = new Point(this.Point.Point.X + dx, this.Point.Point.Y);
        return new(new SpatialPoint<TSpace>(p), this.mapper);
    }

    /// <summary>
    ///     Offsets the Y coordinate.
    /// </summary>
    /// <param name="dy">The Y offset.</param>
    /// <returns>A new flow with Y offset applied.</returns>
    public SpatialFlow<TSpace> OffsetY(double dy)
    {
        var p = new Point(this.Point.Point.X, this.Point.Point.Y + dy);
        return new(new SpatialPoint<TSpace>(p), this.mapper);
    }

    /// <summary>
    ///     Offsets both coordinates.
    /// </summary>
    /// <param name="dx">The X offset.</param>
    /// <param name="dy">The Y offset.</param>
    /// <returns>A new flow with both offsets applied.</returns>
    public SpatialFlow<TSpace> Offset(double dx, double dy)
    {
        var p = new Point(this.Point.Point.X + dx, this.Point.Point.Y + dy);
        return new(new SpatialPoint<TSpace>(p), this.mapper);
    }
}
