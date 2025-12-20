// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Messages;

/// <summary>
/// Message published after an attempt to remove a <see cref="GameComponent"/> from a
/// <see cref="SceneNode"/>.
/// </summary>
/// <param name="node">The target <see cref="SceneNode"/> of the remove operation. This parameter is not expected to be <see langword="null"/>.</param>
/// <param name="component">The <see cref="GameComponent"/> that was requested to be removed. This parameter is not expected to be <see langword="null"/>.</param>
/// <param name="removed">A value indicating whether the component was actually removed (<c>true</c>) or the operation failed/was rejected (<c>false</c>).</param>
internal sealed class ComponentRemovedMessage(SceneNode node, GameComponent component, bool removed)
{
    /// <summary>
    /// Gets the scene node that was the target of the remove operation.
    /// </summary>
    public SceneNode Node { get; } = node;

    /// <summary>
    /// Gets the component that was requested to be removed from the node.
    /// </summary>
    public GameComponent Component { get; } = component;

    /// <summary>
    /// Gets a value indicating whether the component was successfully removed from the node.
    /// </summary>
    public bool Removed { get; } = removed;
}
