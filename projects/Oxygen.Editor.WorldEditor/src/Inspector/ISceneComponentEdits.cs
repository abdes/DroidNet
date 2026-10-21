// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Adds and removes the components of an inspected node, each as one step of its scene's history.</summary>
public interface ISceneComponentEdits
{
    /// <summary>Adds a component of a kind to a node.</summary>
    /// <param name="node">The node.</param>
    /// <param name="kind">The component's kind.</param>
    /// <returns>The completion of the edit.</returns>
    public Task AddComponentAsync(SceneNode node, string kind);

    /// <summary>Removes a component from a node.</summary>
    /// <param name="node">The node.</param>
    /// <param name="component">The component.</param>
    /// <returns>The completion of the edit.</returns>
    public Task RemoveComponentAsync(SceneNode node, GameComponent component);
}
