// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for <see cref="Slots.LightingSlot"/>.
/// </summary>
public record LightingSlotData : OverrideSlotData
{
    /// <summary>
    /// Gets or initializes the shadow casting override.
    /// </summary>
    public OverridablePropertyData<bool>? CastShadows { get; init; }

    /// <summary>
    /// Gets or initializes the shadow receiving override.
    /// </summary>
    public OverridablePropertyData<bool>? ReceiveShadows { get; init; }
}
