// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.World.SceneExplorer.Operations;

namespace Oxygen.Editor.World.SceneExplorer.Services;

/// <summary>
/// Defines the service for managing the Scene Explorer's business logic,
/// encapsulating operations that involve both the Scene Graph (Model) and the Explorer Layout (UI).
/// </summary>
public interface ISceneExplorerService
{
    /// <summary>Raised synchronously after an authoring mutation and before engine synchronization.</summary>
    public event EventHandler<SceneAuthoringChangedEventArgs>? AuthoringChanged;

    /// <summary>
    /// Adds an existing scene node under the specified parent.
    /// Used for Undo/Redo operations to restore a deleted node.
    /// </summary>
    /// <param name="parent">The parent item (Node or Folder).</param>
    /// <param name="node">The existing node to add.</param>
    /// <returns>A task representing the asynchronous operation, returning the change record.</returns>
    public Task<SceneNodeChangeRecord?> AddNodeAsync(ITreeItem parent, SceneNode node);

    /// <summary>
    /// Deletes the specified items.
    /// </summary>
    /// <param name="items">The items to delete.</param>
    /// <returns>A task representing the asynchronous operation, returning the list of change records.</returns>
    public Task<IList<SceneNodeChangeRecord>> DeleteItemsAsync(IEnumerable<ITreeItem> items);
}
