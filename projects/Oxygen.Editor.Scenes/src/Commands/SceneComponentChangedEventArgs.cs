// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Names the component a command added to or removed from a scene node.</summary>
/// <param name="node">The node.</param>
/// <param name="component">The component.</param>
public sealed class SceneComponentChangedEventArgs(SceneNode node, GameComponent component) : EventArgs
{
    /// <summary>Gets the node.</summary>
    public SceneNode Node { get; } = node;

    /// <summary>Gets the component.</summary>
    public GameComponent Component { get; } = component;
}
