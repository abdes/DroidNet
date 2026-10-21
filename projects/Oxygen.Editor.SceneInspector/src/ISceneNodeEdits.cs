// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.SceneInspector;

/// <summary>Renames an inspected node and adds and removes its components, each as one step of its scene's history.</summary>
public interface ISceneNodeEdits
{
    /// <summary>Adds a component of a kind to a node.</summary>
    /// <param name="node">The node.</param>
    /// <param name="kind">The component's kind.</param>
    /// <returns>The completion of the edit.</returns>
    public Task AddComponentAsync(SceneNode node, string kind);

    /// <summary>Renames a node.</summary>
    /// <param name="node">The node.</param>
    /// <param name="name">The new name.</param>
    /// <returns><see langword="true"/> when the node took the name; <see langword="false"/> when the edit was refused.</returns>
    public Task<bool> RenameAsync(SceneNode node, string name);

    /// <summary>Removes a component from a node.</summary>
    /// <param name="node">The node.</param>
    /// <param name="component">The component.</param>
    /// <returns>The completion of the edit.</returns>
    public Task RemoveComponentAsync(SceneNode node, GameComponent component);
}
