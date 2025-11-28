// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for <see cref="GeometryComponent"/>.
/// </summary>
public record GeometryComponentData : ComponentData
{
    /// <summary>
    /// Gets or initializes the geometry asset URI.
    /// </summary>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1056:URI-like properties should not be strings", Justification = "for a DTO with the property serialized as a string, a Uri is overkill")]
    public string? GeometryUri { get; init; }

    /// <summary>
    /// Gets or initializes the component-level override slots.
    /// </summary>
    public IList<OverrideSlotData> OverrideSlots { get; init; } = [];

    /// <summary>
    /// Gets or initializes the targeted overrides for specific LODs/submeshes.
    /// </summary>
    public IList<TargetedOverrideData> TargetedOverrides { get; init; } = [];
}
