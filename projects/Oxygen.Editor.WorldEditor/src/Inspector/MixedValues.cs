// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Oxygen.Editor.Core;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Represents the details of multiple selected items in the properties editor.
/// </summary>
[SuppressMessage("ReSharper", "MemberCanBePrivate.Global", Justification = "method may be used to implement mixed values in property editors")]
public static class MixedValues
{
    /// <summary>
    /// Gets the mixed value of a string property from a collection of items.
    /// </summary>
    /// <param name="items">The collection of items.</param>
    /// <param name="getProperty">A function to get the property value from an item.</param>
    /// <typeparam name="T">The type of the items in the collection.</typeparam>
    /// <returns>
    /// The property value if it is the same for all items; otherwise, null.
    /// </returns>
    internal static string? GetMixedValue<T>(ICollection<T> items, Func<T, string> getProperty)
    {
        if (items.Count == 0)
        {
            return null;
        }

        var value = getProperty(items.First());
        return items.Skip(1).Any(entity => !string.Equals(getProperty(entity), value, StringComparison.Ordinal))
            ? null
            : value;
    }

    /// <summary>
    /// Gets the mixed value of a boolean property from a collection of items.
    /// </summary>
    /// <param name="items">The collection of items.</param>
    /// <param name="getProperty">A function to get the property value from an item.</param>
    /// <typeparam name="T">The type of the items in the collection.</typeparam>
    /// <returns>
    /// The property value if it is the same for all items; otherwise, null.
    /// </returns>
    internal static bool? GetMixedValue<T>(ICollection<T> items, Func<T, bool> getProperty)
    {
        if (items.Count == 0)
        {
            return null;
        }

        var value = getProperty(items.First());
        return items.Skip(1).Any(entity => getProperty(entity) != value) ? null : value;
    }

    /// <summary>
    /// Gets the mixed value of a float property from a collection of items.
    /// </summary>
    /// <param name="items">The collection of items.</param>
    /// <param name="getProperty">A function to get the property value from an item.</param>
    /// <typeparam name="T">The type of the items in the collection.</typeparam>
    /// <returns>
    /// The property value if it is the same for all items; otherwise, null.
    /// </returns>
    internal static float? GetMixedValue<T>(ICollection<T> items, Func<T, float> getProperty)
    {
        if (items.Count == 0)
        {
            return null;
        }

        var value = getProperty(items.First());
        return items.Skip(1).Any(entity => !getProperty(entity).IsSameAs(value)) ? null : value;
    }
}
