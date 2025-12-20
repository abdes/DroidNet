// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Messages;

/// <summary>
/// Message indicating that one or more scene nodes were added to the explorer.
/// </summary>
/// <param name="nodes">The nodes that were added.</param>
internal sealed class SceneNodeAddedMessage(IList<SceneNode> nodes)
{
    /// <summary>
    /// Gets the nodes that were added.
    /// </summary>
    public IList<SceneNode> Nodes { get; } = nodes;
}
