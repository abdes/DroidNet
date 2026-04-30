// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Converts managed property-sync entries to the native interop payload.
/// </summary>
internal static class EnginePropertyWire
{
    /// <summary>
    /// Converts managed property entries into the compact native wire representation.
    /// </summary>
    /// <param name="entries">The managed entries to convert.</param>
    /// <returns>The native interop entries.</returns>
    public static Oxygen.Interop.World.PropertyValueEntry[] ToWireEntries(IReadOnlyList<EnginePropertyValueEntry> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);

        var wire = new Oxygen.Interop.World.PropertyValueEntry[entries.Count];
        for (var i = 0; i < entries.Count; i++)
        {
            wire[i] = new Oxygen.Interop.World.PropertyValueEntry
            {
                ComponentId = (ushort)entries[i].Component,
                FieldId = entries[i].FieldId,
                Value = entries[i].Value,
            };
        }

        return wire;
    }
}
