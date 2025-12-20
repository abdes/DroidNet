// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.World.Messages;

/// <summary>
/// Snapshot of geometry selection for messaging and undo/redo purposes.
/// </summary>
/// <param name="UriString">The string representation of the geometry URI. May be <see langword="null"/> when no geometry is set.</param>
[StructLayout(LayoutKind.Auto)]
[System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.DocumentationRules", "SA1649:File name should match first type name", Justification = "Auxiliary type in message file")]
public readonly record struct GeometrySnapshot(string? UriString);

/// <summary>
/// Message sent when one or more <see cref="SceneNode"/> geometry values have been applied
/// by a UI editor. The message carries the affected nodes together with snapshots of the
/// old and new geometry values to support undo/redo operations and downstream synchronization.
/// </summary>
/// <param name="nodes">The list of affected <see cref="SceneNode"/> instances. This collection is expected to contain the same number of entries as <paramref name="oldValues"/> and <paramref name="newValues"/>.</param>
/// <param name="oldValues">The per-node snapshots representing the geometry state prior to the change.</param>
/// <param name="newValues">The per-node snapshots representing the geometry state after the change.</param>
/// <param name="property">The name of the property that was edited (for example, "Asset").</param>
internal sealed class SceneNodeGeometryAppliedMessage(IList<SceneNode> nodes, IList<GeometrySnapshot> oldValues, IList<GeometrySnapshot> newValues, string property)
{
    /// <summary>
    /// Gets the list of nodes that were affected by the edit.
    /// </summary>
    public IList<SceneNode> Nodes { get; } = nodes;

    /// <summary>
    /// Gets the snapshots describing the geometry values before the edit.
    /// The sequence aligns with <see cref="Nodes"/> by index.
    /// </summary>
    public IList<GeometrySnapshot> OldValues { get; } = oldValues;

    /// <summary>
    /// Gets the snapshots describing the geometry values after the edit.
    /// The sequence aligns with <see cref="Nodes"/> by index.
    /// </summary>
    public IList<GeometrySnapshot> NewValues { get; } = newValues;

    /// <summary>
    /// Gets the property name that was edited. This can be used by handlers to determine
    /// the nature of the change (for example, distinguishing between "Asset" and other
    /// hypothetical geometry-related properties).
    /// </summary>
    public string Property { get; } = property;
}
