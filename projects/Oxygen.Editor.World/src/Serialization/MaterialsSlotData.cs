// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for <see cref="Slots.MaterialsSlot"/>.
/// </summary>
public record MaterialsSlotData : OverrideSlotData
{
    /// <summary>Gets the geometry identity retained with this assignment.</summary>
    public required Uri GeometryUri { get; init; }

    /// <summary>Gets the native slot identity, independent of display order.</summary>
    public required Guid SlotId { get; init; }

    /// <summary>Gets the last resolved native inventory revision.</summary>
    public required string LayoutRevision { get; init; }

    /// <summary>
    /// Gets or initializes the material URI.
    /// </summary>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1056:URI-like properties should not be strings", Justification = "for a DTO with the property serialized as a string, a Uri is overkill")]
    public required string MaterialUri { get; init; }
}
