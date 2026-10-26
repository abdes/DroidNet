// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Testing;

/// <summary>
/// The editor's lock and hide state for one scene: what a test sets is what the scene's edits read, and what
/// <c>SetEditorHidden</c> writes is what the test reads back.
/// </summary>
public sealed class FakeNodeInteraction : ISceneNodeInteraction
{
    private readonly Lock gate = new();
    private readonly HashSet<NodeReference> locked = [];
    private readonly HashSet<NodeId> hidden = [];

    /// <summary>Gets the hidden nodes, sorted, so two reads compare by value.</summary>
    public IReadOnlyList<NodeId> Hidden
    {
        get
        {
            lock (this.gate)
            {
                return [.. this.hidden.OrderBy(static id => id.Value)];
            }
        }
    }

    /// <summary>Locks nodes; their descendants are locked through them.</summary>
    /// <param name="nodes">The nodes.</param>
    public void Lock(params IEnumerable<NodeId> nodes)
        => this.Lock(nodes.Select(NodeReference.To));

    /// <summary>Locks nodes, which an instance may have brought in; their descendants are locked through them.</summary>
    /// <param name="nodes">The nodes.</param>
    public void Lock(params IEnumerable<NodeReference> nodes)
    {
        lock (this.gate)
        {
            this.locked.UnionWith(nodes);
        }
    }

    /// <summary>Unlocks nodes.</summary>
    /// <param name="nodes">The nodes.</param>
    public void Unlock(params IEnumerable<NodeId> nodes)
    {
        lock (this.gate)
        {
            this.locked.ExceptWith(nodes.Select(NodeReference.To));
        }
    }

    /// <summary>Hides nodes, as if the user hid them before the test.</summary>
    /// <param name="nodes">The nodes.</param>
    public void Hide(params IEnumerable<NodeId> nodes)
    {
        lock (this.gate)
        {
            this.hidden.UnionWith(nodes);
        }
    }

    /// <inheritdoc/>
    public bool IsHidden(NodeId nodeId)
    {
        lock (this.gate)
        {
            return this.hidden.Contains(nodeId);
        }
    }

    /// <inheritdoc/>
    public bool IsLocked(NodeReference node)
    {
        lock (this.gate)
        {
            return this.locked.Contains(node);
        }
    }

    /// <inheritdoc/>
    public SceneNode? GetLockOwner(SceneNode node)
    {
        ArgumentNullException.ThrowIfNull(node);
        for (var current = node; current is not null; current = current.Hierarchy.Parent)
        {
            if (this.IsLocked(NodeReference.To(current.Id)))
            {
                return current;
            }
        }

        return null;
    }

    /// <inheritdoc/>
    public NodeReference? GetLockOwner(Scene scene, NodeReference node)
        => SceneNodeInteraction.FindLockOwner(scene, node, this.IsLocked);

    /// <inheritdoc/>
    public void SetHidden(NodeId nodeId, bool isHidden)
    {
        lock (this.gate)
        {
            _ = isHidden ? this.hidden.Add(nodeId) : this.hidden.Remove(nodeId);
        }
    }
}
