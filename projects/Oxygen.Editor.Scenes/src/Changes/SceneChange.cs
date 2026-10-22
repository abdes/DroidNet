// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Changes;

/// <summary>Names what an edit, a replay, a preview or a relocation changed in a scene, by id.</summary>
/// <remarks>
/// A change carries no values: consumers read them from the scene, which already holds them when the change is raised.
/// </remarks>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1034:Nested types should not be visible", Justification = "A closed union: the private constructor admits only the nested cases, which consumers match as SceneChange.NodesAdded and so on.")]
public abstract record SceneChange
{
    private SceneChange()
    {
    }

    /// <summary>Nodes were inserted, each with its subtree.</summary>
    /// <param name="Roots">The inserted subtrees' roots.</param>
    public sealed record NodesAdded(IReadOnlyList<NodeId> Roots) : SceneChange;

    /// <summary>Nodes were removed, each with its subtree.</summary>
    /// <param name="Roots">The removed subtrees' roots.</param>
    public sealed record NodesRemoved(IReadOnlyList<NodeId> Roots) : SceneChange;

    /// <summary>Nodes moved in the scene graph: reparented, reordered or put back where they were.</summary>
    /// <param name="Nodes">The moved nodes; each holds its new local transform.</param>
    public sealed record NodesPlaced(IReadOnlyList<NodeId> Nodes) : SceneChange;

    /// <summary>A node was renamed.</summary>
    /// <param name="Node">The renamed node.</param>
    public sealed record NodeRenamed(NodeId Node) : SceneChange;

    /// <summary>A component was added to a node.</summary>
    /// <param name="Node">The node.</param>
    /// <param name="Component">The added component.</param>
    public sealed record ComponentAdded(NodeId Node, ComponentId Component) : SceneChange;

    /// <summary>A component was removed from a node.</summary>
    /// <param name="Node">The node.</param>
    /// <param name="Component">The removed component.</param>
    /// <param name="Kind">The removed component's kind, as <c>SceneComponents.Add</c> takes it, since the scene no longer holds it.</param>
    public sealed record ComponentRemoved(NodeId Node, ComponentId Component, string Kind) : SceneChange;

    /// <summary>Properties of a node, or of the scene, changed.</summary>
    /// <param name="Node">The node, or <see langword="null"/> for the scene's own properties.</param>
    /// <param name="Properties">The changed properties.</param>
    /// <param name="Phase">The step of the edit the change belongs to.</param>
    public sealed record PropertiesChanged(NodeId? Node, IReadOnlyList<PropertyId> Properties, EditPhase Phase) : SceneChange;

    /// <summary>A node's geometry asset changed.</summary>
    /// <param name="Node">The node.</param>
    public sealed record GeometryChanged(NodeId Node) : SceneChange;

    /// <summary>Overrides of a node's component, the values it stores over its sources, changed.</summary>
    /// <param name="Node">The node.</param>
    /// <param name="Properties">The properties whose overrides changed, on any part.</param>
    public sealed record OverridesChanged(NodeId Node, IReadOnlyList<PropertyId> Properties) : SceneChange;

    /// <summary>The explorer layout changed.</summary>
    public sealed record LayoutChanged : SceneChange;

    /// <summary>Nodes were hidden or shown in the editor.</summary>
    /// <param name="Nodes">The nodes.</param>
    public sealed record EditorVisibilityChanged(IReadOnlyList<NodeId> Nodes) : SceneChange;

    /// <summary>The camera node the scene starts viewing through changed.</summary>
    public sealed record StartingCameraChanged : SceneChange;

    /// <summary>The input mapping contexts that are active when the scene starts changed.</summary>
    public sealed record StartingInputContextsChanged : SceneChange;

    /// <summary>The scene's asset references changed.</summary>
    public sealed record SceneReferencesChanged : SceneChange;

    /// <summary>A node's script slots changed.</summary>
    /// <param name="Node">The node.</param>
    public sealed record ScriptSlotsChanged(NodeId Node) : SceneChange;
}
