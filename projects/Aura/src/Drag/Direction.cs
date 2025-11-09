// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Drag;

/// <summary>
///     Represents the direction of movement or displacement.
/// </summary>
/// <seealso cref="DirectionExtensions"/>
internal enum Direction
{
    /// <summary>Indicates movement to the left.</summary>
    Left,

    /// <summary>Indicates movement to the right.</summary>
    Right,
}

/// <summary>
///     Provides extension methods for the <see cref="Direction"/> enum.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.DocumentationRules", "SA1649:File name should match first type name", Justification = "keep together with the enum definition")]
internal static class DirectionExtensions
{
    /// <summary>
    ///     Gets the sign value corresponding to the direction.
    /// </summary>
    /// <param name="direction">The direction to get the sign for.</param>
    /// <returns><c>+1</c> for <see cref="Direction.Right"/>, <c>-1</c> for <see cref="Direction.Left"/>.</returns>
    public static int Sign(this Direction direction)
        => direction == Direction.Right ? 1 : -1;
}
