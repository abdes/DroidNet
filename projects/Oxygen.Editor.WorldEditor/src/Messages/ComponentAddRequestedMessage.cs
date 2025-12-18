// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.WorldEditor.Messages;

/// <summary>
/// Message sent to request that a <see cref="GameComponent"/> be added to a
/// <see cref="SceneNode"/>. Handlers of this message should perform the actual add
/// operation and respond (for example by publishing a corresponding confirmation
/// message) as required by the host editor.
/// </summary>
/// <param name="node">The target <see cref="SceneNode"/> for the add request. This parameter is not expected to be <see langword="null"/>.</param>
/// <param name="component">The <see cref="GameComponent"/> instance to add. This parameter is not expected to be <see langword="null"/>.</param>
internal sealed class ComponentAddRequestedMessage(SceneNode node, GameComponent component)
{
    /// <summary>
    /// Gets the scene node that is the target of the add request.
    /// </summary>
    public SceneNode Node { get; } = node;

    /// <summary>
    /// Gets the component instance that is requested to be added to the node.
    /// </summary>
    public GameComponent Component { get; } = component;
}
