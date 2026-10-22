// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// The required component placing its node in the scene hierarchy, the engine's <c>GraphData</c>: the parent and the
/// ordered children. Parent links and child membership always agree, and a node never becomes its own ancestor.
/// </summary>
public sealed class HierarchyComponent : GameComponent
{
    /// <summary>The component name a new node's hierarchy takes.</summary>
    public const string DefaultName = "Hierarchy";

    private SceneNode? parent;

    /// <summary>
    /// Gets the parent node, or <see langword="null"/> for a scene root or a detached node.
    /// </summary>
    public SceneNode? Parent
    {
        get => this.parent;
        private set => _ = this.SetProperty(ref this.parent, value);
    }

    /// <summary>
    /// Gets the ordered child nodes.
    /// </summary>
    public ObservableCollection<SceneNode> Children { get; } = [];

    /// <summary>
    /// Gets a value indicating whether the component can be removed; a required component cannot.
    /// </summary>
    public override bool IsLocked => true;

    private SceneNode Owner => this.Node ?? throw new InvalidOperationException("The hierarchy component has no node.");

    /// <summary>
    /// Adds a child node at the end of the children.
    /// </summary>
    /// <param name="child">The child node to add.</param>
    public void AddChild(SceneNode child)
    {
        ArgumentNullException.ThrowIfNull(child);
        child.Hierarchy.SetParent(this.Owner);
    }

    /// <summary>
    /// Removes a child node, detaching it.
    /// </summary>
    /// <param name="child">The child node to remove.</param>
    public void RemoveChild(SceneNode child)
    {
        ArgumentNullException.ThrowIfNull(child);
        if (child.Hierarchy.Parent == this.Owner)
        {
            child.Hierarchy.SetParent(newParent: null);
        }
    }

    /// <summary>
    /// Sets the parent of this component's node, appending the node to the new parent's children.
    /// </summary>
    /// <param name="newParent">The new parent node, or <see langword="null"/> to detach.</param>
    /// <exception cref="InvalidOperationException">The new parent is the node itself or one of its descendants.</exception>
    public void SetParent(SceneNode? newParent)
    {
        var node = this.Owner;
        if (this.Parent == newParent)
        {
            return;
        }

        if (newParent != null && (newParent == node || newParent.Ancestors().Contains(node)))
        {
            throw new InvalidOperationException("Cannot set parent: would create circular reference.");
        }

        var oldParent = this.Parent;
        this.Parent = newParent;

        // Joining the new parent before leaving the old one keeps the node in the graph throughout, so observers of the
        // graph never see it removed.
        if (newParent?.Hierarchy.Children.Contains(node) == false)
        {
            newParent.Hierarchy.Children.Add(node);
        }

        _ = oldParent?.Hierarchy.Children.Remove(node);
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new HierarchyData
        {
            Id = this.Id,
            Name = this.Name,
            Children = this.Children.Count == 0 ? null : [.. this.Children.Select(static child => child.Dehydrate())],
        };
}
