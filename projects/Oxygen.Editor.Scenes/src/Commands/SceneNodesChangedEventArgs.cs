// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Names the scene nodes a command added to or removed from a scene.</summary>
/// <param name="nodes">The nodes.</param>
public sealed class SceneNodesChangedEventArgs(IReadOnlyList<SceneNode> nodes) : EventArgs
{
    /// <summary>Gets the nodes.</summary>
    public IReadOnlyList<SceneNode> Nodes { get; } = nodes;
}
