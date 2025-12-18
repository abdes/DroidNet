// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.WorldEditor.Messages;

/// <summary>
/// Message sent to request that a <see cref="GameComponent"/> be removed from a
/// <see cref="SceneNode"/>. Handlers of this message should perform the actual removal
/// and publish a confirmation message (for example <c>ComponentRemovedMessage</c>) when
/// the operation completes.
/// </summary>
/// <param name="node">The target <see cref="SceneNode"/> for the remove request. This parameter is not expected to be <see langword="null"/>.</param>
/// <param name="component">The <see cref="GameComponent"/> instance requested for removal. This parameter is not expected to be <see langword="null"/>.</param>
internal sealed class ComponentRemoveRequestedMessage(SceneNode node, GameComponent component)
{
    /// <summary>
    /// Gets the scene node that is the target of the remove request.
    /// </summary>
    public SceneNode Node { get; } = node;

    /// <summary>
    /// Gets the component instance that is requested to be removed from the node.
    /// </summary>
    public GameComponent Component { get; } = component;
}
