// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls;

/// <summary>
///     Represents a point with an explicit coordinate space, eliminating ambiguity in spatial calculations.
/// </summary>
public readonly struct SpatialPoint : IEquatable<SpatialPoint>
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="SpatialPoint"/> struct.
    /// </summary>
    /// <param name="point">The point coordinates.</param>
    /// <param name="space">The coordinate space the point is expressed in.</param>
    /// <param name="referenceElement">The reference UIElement when using CoordinateSpace.Element.</param>
    public SpatialPoint(Windows.Foundation.Point point, CoordinateSpace space, UIElement? referenceElement = null)
    {
        this.Point = point;
        this.Space = space;
        this.ReferenceElement = referenceElement;

        // Validate that Element space requires a reference element
        if (space == CoordinateSpace.Element && referenceElement is null)
        {
            throw new ArgumentNullException(nameof(referenceElement), "Element coordinate space requires a reference element");
        }
    }

    /// <summary>
    ///     Gets the point coordinates.
    /// </summary>
    public Windows.Foundation.Point Point { get; }

    /// <summary>
    ///     Gets the coordinate space this point is expressed in.
    /// </summary>
    public CoordinateSpace Space { get; }

    /// <summary>
    ///     Gets the reference UIElement when Space is CoordinateSpace.Element.
    /// </summary>
    public UIElement? ReferenceElement { get; }

    /// <summary>
    ///     Gets the X coordinate.
    /// </summary>
    public double X => this.Point.X;

    /// <summary>
    ///     Gets the Y coordinate.
    /// </summary>
    public double Y => this.Point.Y;

    /// <inheritdoc/>
    public override string ToString() => $"{this.Point} [{this.Space}]";

    /// <inheritdoc/>
    public bool Equals(SpatialPoint other) =>
        this.Point.Equals(other.Point) &&
        this.Space == other.Space &&
        ReferenceEquals(this.ReferenceElement, other.ReferenceElement);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is SpatialPoint other && this.Equals(other);

    /// <inheritdoc/>
    public override int GetHashCode() => HashCode.Combine(this.Point, this.Space, this.ReferenceElement);

    public static bool operator ==(SpatialPoint left, SpatialPoint right) => left.Equals(right);

    public static bool operator !=(SpatialPoint left, SpatialPoint right) => !left.Equals(right);
}
