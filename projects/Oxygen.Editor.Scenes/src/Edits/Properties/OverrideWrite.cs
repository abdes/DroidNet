// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Properties;

/// <summary>
/// Sets or resets one override of a node's component, or one change of a node an instance brings in, as
/// <see cref="SetOverrides"/> records it.
/// </summary>
/// <param name="Property">The property.</param>
/// <param name="Part">The part, or <see langword="null"/> for a property of the component itself.</param>
/// <param name="LayoutRevision">
/// The revision of the part layout the part was witnessed in; required with a part and a value, ignored on reset.
/// </param>
/// <param name="Value">The value, of the property's type, or <see langword="null"/> to reset to the source's value.</param>
public sealed record OverrideWrite(PropertyId Property, PartId? Part, string? LayoutRevision, object? Value)
{
    /// <summary>
    /// Gets the asset the part was chosen from, such as the geometry whose inventory lists the surface; the edit refuses a
    /// node whose component no longer uses that asset. <see langword="null"/> writes whatever the asset is, as an undo does.
    /// </summary>
    public AssetPath? PartOwner { get; init; }

    /// <summary>
    /// Gets the node below the instance root the write changes, or <see langword="null"/> for an override of the node's
    /// own component. With a path, the write sets or removes a change in the root's <see cref="SceneInstanceComponent.Changes"/>.
    /// </summary>
    public NodePath? Path { get; init; }

    /// <summary>Creates the write that puts an override back as it was.</summary>
    /// <param name="property">The property.</param>
    /// <param name="part">The part, or <see langword="null"/> for a property of the component itself.</param>
    /// <param name="previous">The override before the edit, or <see langword="null"/> when there was none.</param>
    /// <returns>The write.</returns>
    public static OverrideWrite Restoring(PropertyId property, PartId? part, SourceOverride? previous)
        => new(property, part, previous?.LayoutRevision, previous?.Value);
}
