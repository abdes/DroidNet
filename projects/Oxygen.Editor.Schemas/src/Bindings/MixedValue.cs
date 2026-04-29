// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Linq;

namespace Oxygen.Editor.Schemas.Bindings;

/// <summary>
/// Computes the multi-selection mixed-value state for a property across
/// many node-bound values.
/// </summary>
/// <typeparam name="T">The value type.</typeparam>
/// <param name="HasValue">Whether the binding has a representative value
/// to display. False when there are zero contributing nodes.</param>
/// <param name="IsMixed">Whether the contributing nodes disagree on the
/// value.</param>
/// <param name="Value">The representative value when not mixed; the
/// first node's value when mixed (callers usually treat it as a
/// placeholder).</param>
public readonly record struct MixedValue<T>(bool HasValue, bool IsMixed, T Value)
{
    /// <summary>
    /// The "no contributing nodes" state.
    /// </summary>
    public static MixedValue<T> Empty { get; } = new(false, false, default!);

    /// <summary>
    /// Folds a sequence of values into a mixed-value state.
    /// </summary>
    /// <param name="values">The contributing values.</param>
    /// <param name="comparer">Optional equality comparer; defaults to
    /// <see cref="EqualityComparer{T}.Default"/>.</param>
    /// <returns>The mixed-value state.</returns>
    public static MixedValue<T> Fold(IEnumerable<T> values, IEqualityComparer<T>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(values);
        comparer ??= EqualityComparer<T>.Default;

        var first = true;
        var representative = default(T)!;
        var mixed = false;
        foreach (var v in values)
        {
            if (first)
            {
                representative = v;
                first = false;
                continue;
            }

            if (!comparer.Equals(representative, v))
            {
                mixed = true;
            }
        }

        return first ? Empty : new MixedValue<T>(true, mixed, representative);
    }
}
