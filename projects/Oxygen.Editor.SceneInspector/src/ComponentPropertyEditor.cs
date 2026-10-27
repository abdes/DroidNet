// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneInspector;

/// <summary>A section that edits one component kind, or one node property group, on every selected node.</summary>
/// <remarks>
/// A section names each node it edits by its <see cref="NodeReference"/>: a node an instance brings in shows as a
/// transient node with the ID it has in its source, which two instances of one source share, and its edits land in its
/// instance root's changes (scene instances 8.5).
/// </remarks>
public abstract class ComponentPropertyEditor : InspectorSection
{
    /// <summary>Gets how the panel names a selected node; a node of the scene by its ID when unset.</summary>
    internal Func<SceneNode, NodeReference>? References { get; init; }

    /// <summary>Shows the section's values for the selected nodes and targets its edits at them.</summary>
    /// <param name="items">The selected nodes, all of which have the section's component.</param>
    public abstract void UpdateValues(ICollection<SceneNode> items);

    /// <summary>Gets the reference a selected node's edits name it by.</summary>
    /// <param name="node">The selected node.</param>
    /// <returns>Its instance root and path for a node an instance brings in, the node itself otherwise.</returns>
    private protected NodeReference ReferenceOf(SceneNode node) => this.References?.Invoke(node) ?? NodeReference.To(node.Id);
}
