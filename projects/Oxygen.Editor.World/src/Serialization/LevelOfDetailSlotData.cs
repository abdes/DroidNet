// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for <see cref="Slots.LevelOfDetailSlot"/>.
/// </summary>
public record LevelOfDetailSlotData : OverrideSlotData
{
    /// <summary>
    /// Gets or initializes the LOD policy override.
    /// </summary>
    public OverridablePropertyData<LodPolicy>? LodPolicy { get; init; }
}
