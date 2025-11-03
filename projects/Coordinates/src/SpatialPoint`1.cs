// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Windows.Foundation;

namespace DroidNet.Coordinates;

/// <summary>
///     Represents a point in a spatial coordinate system.
/// </summary>
/// <typeparam name="TSpace">The type representing the spatial coordinate system.</typeparam>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2225:Operator overloads have named alternates", Justification = "provided in non-generic class")]
public readonly record struct SpatialPoint<TSpace>(Point Point)
{
    /// <summary>Adds two <see cref="SpatialPoint{TSpace}"/> instances.</summary>
    /// <param name="a">The first point.</param>
    /// <param name="b">The second point.</param>
    /// <returns>The sum of the two points.</returns>
    public static SpatialPoint<TSpace> operator +(SpatialPoint<TSpace> a, SpatialPoint<TSpace> b) =>
        new(new Point(a.Point.X + b.Point.X, a.Point.Y + b.Point.Y));

    /// <summary>Subtracts one <see cref="SpatialPoint{TSpace}"/> from another.</summary>
    /// <param name="a">The first point.</param>
    /// <param name="b">The second point.</param>
    /// <returns>The difference of the two points.</returns>
    public static SpatialPoint<TSpace> operator -(SpatialPoint<TSpace> a, SpatialPoint<TSpace> b) =>
        new(new Point(a.Point.X - b.Point.X, a.Point.Y - b.Point.Y));

    /// <summary>
    ///     Creates a <see cref="SpatialFlow{TSpace}"/> from this point using the specified mapper.
    /// </summary>
    /// <param name="mapper">The spatial mapper.</param>
    /// <returns>A spatial flow.</returns>
    public SpatialFlow<TSpace> Flow(ISpatialMapper mapper) =>
        new(this, mapper);

    /// <inheritdoc/>
    public override string ToString() => $"{this.Point} [{typeof(TSpace).Name}]";
}

/// <summary>
///     Provides static methods for operations on <see cref="SpatialPoint{TSpace}"/> instances.
/// </summary>
/// <example>
/// <code><![CDATA[
///     var a = new SpatialPoint<ElementSpace>(new Point(10, 20));
///     var b = new SpatialPoint<ElementSpace>(new Point(5, 15));
///     var origin = SpatialPoint.Add<ElementSpace>(a, b);
/// ]]></code>
/// </example>
public static class SpatialPoint
{
    /// <summary>Adds two <see cref="SpatialPoint{TSpace}"/> instances.</summary>
    /// <typeparam name="TSpace">The type representing the spatial coordinate system.</typeparam>
    /// <param name="left">The first point.</param>
    /// <param name="right">The second point.</param>
    /// <returns>The sum of the two points.</returns>
    public static SpatialPoint<TSpace> Add<TSpace>(SpatialPoint<TSpace> left, SpatialPoint<TSpace> right) =>
        new(new Point(left.Point.X + right.Point.X, left.Point.Y + right.Point.Y));

    /// <summary>Subtracts one <see cref="SpatialPoint{TSpace}"/> from another.</summary>
    /// <typeparam name="TSpace">The type representing the spatial coordinate system.</typeparam>
    /// <param name="left">The first point.</param>
    /// <param name="right">The second point.</param>
    /// <returns>The difference of the two points.</returns>
    public static SpatialPoint<TSpace> Subtract<TSpace>(SpatialPoint<TSpace> left, SpatialPoint<TSpace> right) =>
        new(new Point(left.Point.X - right.Point.X, left.Point.Y - right.Point.Y));
}
