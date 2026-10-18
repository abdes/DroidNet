// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Nodes;

namespace Oxygen.Editor.World.Messages;

/// <summary>
/// Message sent to request that a component be added to a <see cref="SceneNode"/>. Handlers of this message should
/// perform the actual add operation, which creates the component.
/// </summary>
/// <param name="node">The target <see cref="SceneNode"/> for the add request. This parameter is not expected to be <see langword="null"/>.</param>
/// <param name="kind">The kind of component to add, one of the <see cref="SceneComponents"/> constants.</param>
internal sealed class ComponentAddRequestedMessage(SceneNode node, string kind)
{
    /// <summary>
    /// Gets the scene node that is the target of the add request.
    /// </summary>
    public SceneNode Node { get; } = node;

    /// <summary>
    /// Gets the kind of component to add, one of the <see cref="SceneComponents"/> constants.
    /// </summary>
    public string Kind { get; } = kind;
}
