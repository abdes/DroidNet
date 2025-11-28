// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for a targeted override (specific LOD/submesh).
/// </summary>
public record TargetedOverrideData
{
    /// <summary>
    /// Gets or initializes the LOD index (-1 for all LODs).
    /// </summary>
    public int LodIndex { get; init; } = -1;

    /// <summary>
    /// Gets or initializes the submesh index (-1 for all submeshes).
    /// </summary>
    public int SubmeshIndex { get; init; } = -1;

    /// <summary>
    /// Gets or initializes the override slots for this target.
    /// </summary>
    public IList<OverrideSlotData> OverrideSlots { get; init; } = [];
}
