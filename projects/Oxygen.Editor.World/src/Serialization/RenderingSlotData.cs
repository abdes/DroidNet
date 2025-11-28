// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for <see cref="Slots.RenderingSlot"/>.
/// </summary>
public record RenderingSlotData : OverrideSlotData
{
    /// <summary>
    /// Gets or initializes the visibility override.
    /// </summary>
    public OverridablePropertyData<bool>? IsVisible { get; init; }
}
