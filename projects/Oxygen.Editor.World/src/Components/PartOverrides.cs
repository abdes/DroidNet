// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// The values a scene stores over the source of one part, such as a geometry surface.
/// </summary>
/// <param name="LayoutRevision">The revision of the part layout the part was last witnessed in.</param>
/// <param name="Values">The overridden values, by property; never empty.</param>
public sealed record PartOverrides(string LayoutRevision, ImmutableDictionary<PropertyId, object> Values)
{
    /// <inheritdoc/>
    public bool Equals(PartOverrides? other)
        => other is not null
            && string.Equals(this.LayoutRevision, other.LayoutRevision, StringComparison.Ordinal)
            && this.Values.Count == other.Values.Count
            && this.Values.All(entry => other.Values.TryGetValue(entry.Key, out var value) && Equals(entry.Value, value));

    /// <inheritdoc/>
    public override int GetHashCode() => HashCode.Combine(this.LayoutRevision, this.Values.Count);
}
