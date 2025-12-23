// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core;

/// <summary>
///     Utility math helpers used by the editor for common floating-point comparisons.
/// </summary>
public static class MathUtil
{
    /// <summary>
    ///     Tolerance used for floating-point comparisons. Values whose absolute difference is less than
    ///     this constant are considered equal.
    /// </summary>
    private const float Epsilon = 1e-5f;

    /// <summary>
    ///     Determines whether two nullable <see cref="float" /> values are considered the same.
    ///     Returns <see langword="true" /> when both are <see langword="null" />, or when both have values and the absolute
    ///     difference between them is less than <see cref="Epsilon" />.
    /// </summary>
    /// <param name="a">The first nullable float to compare.</param>
    /// <param name="b">The second nullable float to compare.</param>
    /// <returns><see langword="true" /> if the values are considered the same; otherwise <see langword="false" />.</returns>
    public static bool AreSame(float? a, float? b) =>
        a.HasValue && b.HasValue
            ? MathF.Abs(a.Value - b.Value) < Epsilon
            : !a.HasValue && !b.HasValue;

    /// <summary>
    ///     Extension method that compares a non-nullable <see cref="float" /> to a nullable <see cref="float" />
    ///     using <see cref="AreSame(float?, float?)" />.
    /// </summary>
    /// <param name="a">The non-nullable float value.</param>
    /// <param name="b">The nullable float value to compare against.</param>
    /// <returns><see langword="true" /> if the values are considered the same; otherwise <see langword="false" />.</returns>
    public static bool IsSameAs(this float a, float? b) => AreSame(a, b);
}
