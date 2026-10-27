// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>What keeps a node from being edited, if anything.</summary>
/// <remarks>
/// A node is protected by the user's lock on it or on an ancestor. The ancestors of a node an instance brings in are
/// known from its instance's contents (scene instances 8.2); until those are read, nothing can tell whether one of them
/// is locked, so the node is protected by its unread instance.
/// </remarks>
public sealed record NodeLock
{
    private NodeLock(NodeReference? owner, NodeId? unreadInstance)
    {
        this.Owner = owner;
        this.UnreadInstance = unreadInstance;
    }

    /// <summary>Gets the lock of a node nothing protects.</summary>
    public static NodeLock None { get; } = new(owner: null, unreadInstance: null);

    /// <summary>Gets the locked node that protects the node: the node itself or its closest locked ancestor.</summary>
    public NodeReference? Owner { get; }

    /// <summary>Gets the instance root whose contents are not read yet, so the node's ancestors are unknown.</summary>
    public NodeId? UnreadInstance { get; }

    /// <summary>Gets a value indicating whether the node is protected from editing.</summary>
    public bool Protects => this.Owner is not null || this.UnreadInstance is not null;

    /// <summary>Creates the lock of a node a locked node protects.</summary>
    /// <param name="owner">The locked node: the node itself or its closest locked ancestor.</param>
    /// <returns>The lock.</returns>
    public static NodeLock By(NodeReference owner)
    {
        ArgumentNullException.ThrowIfNull(owner);
        return new(owner, unreadInstance: null);
    }

    /// <summary>Creates the lock of a node an instance brings in whose contents are not read yet.</summary>
    /// <param name="instanceRoot">The instance root.</param>
    /// <returns>The lock.</returns>
    public static NodeLock Unread(NodeId instanceRoot) => new(owner: null, instanceRoot);
}
