// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Managed.Core;

/// <summary>
/// The valid values of a numeric scene property, as the scene property schema declares them; a vector property's range
/// bounds each of its components alike.
/// </summary>
/// <remarks>
/// A floating-point value must be finite whatever the bounds. A <see langword="null"/> bound is unlimited.
/// </remarks>
/// <typeparam name="T">The numeric value type.</typeparam>
/// <param name="Min">The lowest valid value, or <see langword="null"/> for no lower bound.</param>
/// <param name="Max">The highest valid value, or <see langword="null"/> for no upper bound.</param>
/// <param name="MinExclusive">A value indicating whether <paramref name="Min"/> itself is invalid.</param>
/// <param name="MaxExclusive">A value indicating whether <paramref name="Max"/> itself is invalid.</param>
/// <param name="NonZero">A value indicating whether zero is invalid.</param>
/// <param name="Commit">What an inspector edit does with a finite value outside the range.</param>
public sealed record PropertyRange<T>(T? Min, T? Max, bool MinExclusive, bool MaxExclusive, bool NonZero, RangeCommit Commit)
    where T : struct, INumber<T>
{
    /// <summary>Checks a value against the range.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The result of the check.</returns>
    public RangeCheck Check(T value)
    {
        if (!T.IsFinite(value))
        {
            return RangeCheck.NotFinite;
        }

        var below = this.Min is { } min && (value < min || (this.MinExclusive && value == min));
        var above = this.Max is { } max && (value > max || (this.MaxExclusive && value == max));
        return below || above || (this.NonZero && T.IsZero(value)) ? RangeCheck.OutOfRange : RangeCheck.InRange;
    }

    /// <summary>Gets the value an inspector edit commits for a value, clamping it when the range allows.</summary>
    /// <param name="value">The edited value.</param>
    /// <param name="committed">The value to commit; meaningful only when the method succeeds.</param>
    /// <returns>
    /// <see langword="true"/> when <paramref name="value"/> is in range, or when the range clamps and the clamped value
    /// is in range.
    /// </returns>
    public bool TryCommit(T value, out T committed)
    {
        committed = value;
        var check = this.Check(value);
        if (check == RangeCheck.InRange)
        {
            return true;
        }

        if (check == RangeCheck.NotFinite || this.Commit != RangeCommit.Clamp)
        {
            return false;
        }

        committed = this.Min is { } min && value < min ? min
            : this.Max is { } max && value > max ? max
            : value;
        return this.Check(committed) == RangeCheck.InRange;
    }
}
