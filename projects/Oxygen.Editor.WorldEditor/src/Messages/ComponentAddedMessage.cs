// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Messages;

/// <summary>
/// Message published when a component add operation has completed for a <see cref="SceneNode"/>.
/// </summary>
/// <param name="node">The <see cref="SceneNode"/> the component was added to.</param>
/// <param name="component">The <see cref="GameComponent"/> that was added.</param>
/// <param name="added">A value indicating whether the component was actually added (<see langword="true"/>)
/// or the operation failed/was rejected (<c>false</c>).</param>
internal sealed class ComponentAddedMessage(SceneNode node, GameComponent component, bool added)
{
    /// <summary>
    /// Gets the scene node that was the target of the add operation.
    /// </summary>
    public SceneNode Node { get; } = node;

    /// <summary>
    /// Gets the component that was requested to be added.
    /// </summary>
    public GameComponent Component { get; } = component;

    /// <summary>
    /// Gets a value indicating whether the component was successfully added to the node.
    /// </summary>
    public bool Added { get; } = added;
}
