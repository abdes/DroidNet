// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Windows.Foundation;

namespace DroidNet.Coordinates;

/// <summary>
///     A point in a typed coordinate space. Arithmetic operators are space-preserving;
///     cross-space conversions require an <see cref="ISpatialMapper"/>.
/// </summary>
/// <typeparam name="TSpace">Coordinate space marker type (ElementSpace, WindowSpace, etc.).</typeparam>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2225:Operator overloads have named alternates", Justification = "provided in non-generic class")]
public readonly record struct SpatialPoint<TSpace>(Point Point)
{
    /// <summary>Adds two points in the same space.</summary>
    /// <param name="a">First point.</param>
    /// <param name="b">Second point.</param>
    /// <returns>The sum.</returns>
    public static SpatialPoint<TSpace> operator +(SpatialPoint<TSpace> a, SpatialPoint<TSpace> b) =>
        new(new Point(a.Point.X + b.Point.X, a.Point.Y + b.Point.Y));

    /// <summary>Subtracts two points in the same space.</summary>
    /// <param name="a">First point.</param>
    /// <param name="b">Second point.</param>
    /// <returns>The difference.</returns>
    public static SpatialPoint<TSpace> operator -(SpatialPoint<TSpace> a, SpatialPoint<TSpace> b) =>
        new(new Point(a.Point.X - b.Point.X, a.Point.Y - b.Point.Y));

    /// <summary>
    ///     Creates a fluent transformation pipeline from this point.
    /// </summary>
    /// <param name="mapper">The mapper to use for conversions.</param>
    /// <returns>A flow instance for chaining operations.</returns>
    public SpatialFlow<TSpace> Flow(ISpatialMapper mapper) =>
        new(this, mapper);

    /// <inheritdoc/>
    public override string ToString() => $"{this.Point} [{typeof(TSpace).Name}]";
}

/// <summary>
///     Non-generic operations for <see cref="SpatialPoint{TSpace}"/>.
/// </summary>
/// <example>
/// <code><![CDATA[
///     var sum = SpatialPoint.Add(
///         new SpatialPoint<ElementSpace>(new Point(10, 20)),
///         new SpatialPoint<ElementSpace>(new Point(5, 15)));
/// ]]></code>
/// </example>
public static class SpatialPoint
{
    /// <summary>Adds two points in the same space.</summary>
    /// <typeparam name="TSpace">Coordinate space marker type.</typeparam>
    /// <param name="left">First point.</param>
    /// <param name="right">Second point.</param>
    /// <returns>The sum.</returns>
    public static SpatialPoint<TSpace> Add<TSpace>(SpatialPoint<TSpace> left, SpatialPoint<TSpace> right) =>
        new(new Point(left.Point.X + right.Point.X, left.Point.Y + right.Point.Y));

    /// <summary>Subtracts two points in the same space.</summary>
    /// <typeparam name="TSpace">Coordinate space marker type.</typeparam>
    /// <param name="left">First point.</param>
    /// <param name="right">Second point.</param>
    /// <returns>The difference.</returns>
    public static SpatialPoint<TSpace> Subtract<TSpace>(SpatialPoint<TSpace> left, SpatialPoint<TSpace> right) =>
        new(new Point(left.Point.X - right.Point.X, left.Point.Y - right.Point.Y));
}
