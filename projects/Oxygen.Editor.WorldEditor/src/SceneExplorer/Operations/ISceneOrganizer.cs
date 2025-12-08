// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Operations;

/// <summary>
/// Executes layout-only operations for the Scene Explorer (folders and ordering) without touching the runtime scene graph.
/// Implementations must never mutate <see cref="Scene.RootNodes" /> or scene-node parentage.
/// </summary>
public interface ISceneOrganizer
{
    /// <summary>
    /// Creates a folder containing the specified scene node ids, returning a new layout.
    /// </summary>
    /// <param name="selectedNodeIds">The node identifiers to group.</param>
    /// <param name="scene">The owning scene.</param>
    /// <param name="sceneAdapter">The scene adapter for contextual info (e.g., folder adapters).</param>
    /// <returns>A layout change record with the new layout and folder info.</returns>
    LayoutChangeRecord CreateFolderFromSelection(HashSet<Guid> selectedNodeIds, Scene scene, SceneAdapter sceneAdapter);

    /// <summary>
    /// Moves a scene node into the specified folder.
    /// </summary>
    /// <param name="nodeId">The node identifier to move.</param>
    /// <param name="folderId">The target folder identifier.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A layout change record with the updated layout.</returns>
    LayoutChangeRecord MoveNodeToFolder(Guid nodeId, Guid folderId, Scene scene);

    /// <summary>
    /// Moves a folder to a new parent folder or root.
    /// </summary>
    /// <param name="folderId">The folder to move.</param>
    /// <param name="newParentFolderId">The new parent folder identifier, or <see langword="null" /> for root.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A layout change record with the updated layout.</returns>
    LayoutChangeRecord MoveFolderToParent(Guid folderId, Guid? newParentFolderId, Scene scene);

    /// <summary>
    /// Removes a folder and optionally promotes its children to the parent.
    /// </summary>
    /// <param name="folderId">The folder to remove.</param>
    /// <param name="promoteChildrenToParent">Whether to promote removed folder's children to its parent.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A layout change record with the updated layout.</returns>
    LayoutChangeRecord RemoveFolder(Guid folderId, bool promoteChildrenToParent, Scene scene);
}

/// <summary>
/// Describes a layout change for downstream adapter reconciliation, undo, and UI refresh.
/// </summary>
/// <param name="OperationName">The operation name for diagnostics.</param>
/// <param name="PreviousLayout">The prior layout snapshot (may be null).</param>
/// <param name="NewLayout">The new layout snapshot.</param>
/// <param name="NewFolder">Optional folder entry when a folder is created.</param>
/// <param name="ModifiedFolders">Optional list of folder entries touched by the operation.</param>
/// <param name="ParentLists">Optional parent lists for modified folders (for adapter reconciliation).</param>
public sealed record LayoutChangeRecord(
    string OperationName,
    IList<ExplorerEntryData>? PreviousLayout,
    IList<ExplorerEntryData> NewLayout,
    ExplorerEntryData? NewFolder = null,
    IList<ExplorerEntryData>? ModifiedFolders = null,
    IList<IList<ExplorerEntryData>>? ParentLists = null);
