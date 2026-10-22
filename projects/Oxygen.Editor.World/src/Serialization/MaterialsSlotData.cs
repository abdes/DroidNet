// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for <see cref="Slots.MaterialsSlot"/>.
/// </summary>
public record MaterialsSlotData : OverrideSlotData
{
    /// <summary>Gets the geometry identity retained with this assignment.</summary>
    public required AssetPath GeometryUri { get; init; }

    /// <summary>Gets the native surface identity, independent of display order.</summary>
    public required Guid SurfaceId { get; init; }

    /// <summary>Gets the last resolved native inventory revision.</summary>
    public required string LayoutRevision { get; init; }

    /// <summary>
    /// Gets or initializes the material URI.
    /// </summary>
    public required AssetPath MaterialUri { get; init; }
}
