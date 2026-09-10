// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.World.Services;

/// <summary>Projects feature property values into the managed runtime request.</summary>
internal static class EnginePropertyWire
{
    /// <summary>Copies property values into an immutable runtime payload.</summary>
    /// <param name="entries">The feature property entries.</param>
    /// <returns>The managed runtime values.</returns>
    public static ImmutableArray<RuntimePropertyValue> ToWireEntries(IReadOnlyList<EnginePropertyValueEntry> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);
        return [.. entries.Select(value => new RuntimePropertyValue((ushort)value.Component, value.FieldId, value.Value))];
    }
}
