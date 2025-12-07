// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Messages;

/// <summary>
/// Message indicating that one or more scene nodes were removed from the explorer.
/// </summary>
/// <param name="nodes">The nodes that were removed.</param>
internal sealed class SceneNodeRemovedMessage(IList<SceneNode> nodes)
{
    /// <summary>
    /// The nodes that were removed.
    /// </summary>
    public IList<SceneNode> Nodes { get; } = nodes;
}
