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
    /// <summary>
    /// Creates a new scene node under the specified parent.
    /// </summary>
    /// <param name="parent">The parent item (Node or Folder).</param>
    /// <param name="name">The name of the new node.</param>
    /// <returns>A task representing the asynchronous operation, returning the change record.</returns>
    public Task<SceneNodeChangeRecord?> CreateNodeAsync(ITreeItem parent, string name);

    /// <summary>
    /// Adds an existing scene node under the specified parent.
    /// Used for Undo/Redo operations to restore a deleted node.
    /// </summary>
    /// <param name="parent">The parent item (Node or Folder).</param>
    /// <param name="node">The existing node to add.</param>
    /// <returns>A task representing the asynchronous operation, returning the change record.</returns>
    public Task<SceneNodeChangeRecord?> AddNodeAsync(ITreeItem parent, SceneNode node);

    /// <summary>
    /// Creates a new folder under the specified parent.
    /// </summary>
    /// <param name="parent">The parent item (Node or Folder).</param>
    /// <param name="name">The name of the new folder.</param>
    /// <param name="folderId">Optional folder identifier. If null, a new one is generated.</param>
    /// <returns>A task representing the asynchronous operation, returning the new folder's ID.</returns>
    public Task<Guid> CreateFolderAsync(ITreeItem parent, string name, Guid? folderId = null);

    /// <summary>
    /// Moves an item to a new parent at a specific index.
    /// Handles complex logic for reparenting in Scene Graph vs Layout updates.
    /// </summary>
    /// <param name="item">The item to move.</param>
    /// <param name="newParent">The new parent item.</param>
    /// <param name="index">The index at which to insert the item.</param>
    /// <returns>A task representing the asynchronous operation, returning the change record.</returns>
    public Task<SceneNodeChangeRecord?> MoveItemAsync(ITreeItem item, ITreeItem newParent, int index);

    /// <summary>
    /// Updates the backend to reflect a batch of moves that have already occurred in the UI.
    /// </summary>
    /// <param name="args">The arguments describing the moves.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task UpdateMovedItemsAsync(TreeItemsMovedEventArgs args);

    /// <summary>
    /// Deletes the specified items.
    /// </summary>
    /// <param name="items">The items to delete.</param>
    /// <returns>A task representing the asynchronous operation, returning the list of change records.</returns>
    public Task<IList<SceneNodeChangeRecord>> DeleteItemsAsync(IEnumerable<ITreeItem> items);

    /// <summary>
    /// Renames an item.
    /// </summary>
    /// <param name="item">The item to rename.</param>
    /// <param name="newName">The new name.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task RenameItemAsync(ITreeItem item, string newName);
}
