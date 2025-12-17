// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Operations;

/// <summary>
/// Executes layout operations for the Scene Explorer (folders and ordering), including adapter reconciliation,
/// without touching the runtime scene graph. Implementations must never mutate <see cref="Scene.RootNodes" />
/// or scene-node parentage.
/// </summary>
public interface ISceneOrganizer
{
    /// <summary>
    /// Moves a scene node into the specified folder.
    /// </summary>
    /// <param name="nodeId">The node identifier to move.</param>
    /// <param name="folderId">The target folder identifier.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A layout change record with the updated layout.</returns>
    public LayoutChangeRecord MoveNodeToFolder(Guid nodeId, Guid folderId, Scene scene);

    /// <summary>
    /// Removes a scene node from the specified folder (effectively moving it to root in the layout).
    /// </summary>
    /// <param name="nodeId">The node identifier to remove.</param>
    /// <param name="folderId">The folder identifier to remove from.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A layout change record with the updated layout.</returns>
    public LayoutChangeRecord RemoveNodeFromFolder(Guid nodeId, Guid folderId, Scene scene);

    /// <summary>
    /// Moves a folder to a new parent folder or root.
    /// </summary>
    /// <param name="folderId">The folder to move.</param>
    /// <param name="newParentFolderId">The new parent folder identifier, or <see langword="null" /> for root.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A layout change record with the updated layout.</returns>
    public LayoutChangeRecord MoveFolderToParent(Guid folderId, Guid? newParentFolderId, Scene scene);

    /// <summary>
    /// Removes a folder and optionally promotes its children to the parent.
    /// </summary>
    /// <param name="folderId">The folder to remove.</param>
    /// <param name="promoteChildrenToParent">Whether to promote removed folder's children to its parent.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A layout change record with the updated layout.</returns>
    public LayoutChangeRecord RemoveFolder(Guid folderId, bool promoteChildrenToParent, Scene scene);

    /// <summary>
    /// Removes a scene node from the layout (effectively moving it to root or its natural scene parent).
    /// </summary>
    /// <param name="nodeId">The node identifier to remove.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A layout change record with the updated layout.</returns>
    public LayoutChangeRecord RemoveNodeFromLayout(Guid nodeId, Scene scene);

    /// <summary>
    /// Creates a new empty folder.
    /// </summary>
    /// <param name="parentFolderId">The parent folder identifier, or null for root.</param>
    /// <param name="name">The name of the new folder.</param>
    /// <param name="scene">The owning scene.</param>
    /// <param name="folderId">Optional folder identifier. If null, a new one is generated.</param>
    /// <param name="parentNodeId">Optional parent node identifier. If provided, the folder is created under this node.</param>
    /// <returns>A layout change record with the updated layout.</returns>
    public LayoutChangeRecord CreateFolder(Guid? parentFolderId, string name, Scene scene, Guid? folderId = null, Guid? parentNodeId = null);

    /// <summary>
    /// Reconciles the adapter tree to the provided layout snapshot deterministically, reusing
    /// existing adapters when possible and performing a single refresh via the layout context.
    /// </summary>
    /// <param name="sceneAdapter">The scene adapter backing the explorer.</param>
    /// <param name="scene">The owning scene.</param>
    /// <param name="layout">The layout snapshot to apply (uses scene root nodes when null).</param>
    /// <param name="layoutContext">Adapter context used to refresh shown items.</param>
    /// <param name="preserveNodeExpansion">When true, keeps existing node expansion instead of layout flags.</param>
    public Task ReconcileLayoutAsync(
        SceneAdapter sceneAdapter,
        Scene scene,
        IList<ExplorerEntryData>? layout,
        ILayoutContext layoutContext,
        bool preserveNodeExpansion = false);

    /// <summary>
    /// Deeply clones a layout.
    /// </summary>
    /// <param name="layout">The layout to clone.</param>
    /// <returns>A deep copy of the layout.</returns>
    public IList<ExplorerEntryData>? CloneLayout(IList<ExplorerEntryData>? layout);

    /// <summary>
    /// Extracts the IDs of all expanded folders in the layout.
    /// </summary>
    /// <param name="layout">The layout to inspect.</param>
    /// <returns>A set of expanded folder IDs.</returns>
    public HashSet<Guid> GetExpandedFolderIds(IList<ExplorerEntryData>? layout);

    /// <summary>
    /// Ensures that the scene's layout contains entries for the specified nodes.
    /// If the layout does not exist, it is created from the root nodes.
    /// </summary>
    /// <param name="scene">The scene to update.</param>
    /// <param name="nodeIds">The node IDs to ensure exist in the layout.</param>
    public void EnsureLayoutContainsNodes(Scene scene, IEnumerable<Guid> nodeIds);

    /// <summary>
    /// Builds a layout representing the state where a folder is created but empty,
    /// used for granular undo/redo operations.
    /// </summary>
    /// <param name="layoutChange">The layout change record from the folder creation.</param>
    /// <returns>A layout with the new folder inserted but empty.</returns>
    public IList<ExplorerEntryData> BuildFolderOnlyLayout(LayoutChangeRecord layoutChange);

    /// <summary>
    /// Filters the selected node IDs to return only the top-level nodes (i.e., excludes nodes whose ancestors are also selected).
    /// </summary>
    /// <param name="selectedIds">The set of selected node IDs.</param>
    /// <param name="scene">The scene to check hierarchy against.</param>
    /// <returns>A set of top-level selected node IDs.</returns>
    public HashSet<Guid> FilterTopLevelSelectedNodeIds(HashSet<Guid> selectedIds, Scene scene);

    /// <summary>
    /// Renames a folder in the layout.
    /// </summary>
    /// <param name="folderId">The folder identifier.</param>
    /// <param name="newName">The new name.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A layout change record with the updated layout.</returns>
    public LayoutChangeRecord RenameFolder(Guid folderId, string newName, Scene scene);
}

/// <summary>
/// Provides VM-owned callbacks the organizer needs when reconciling layout changes.
/// </summary>
public interface ILayoutContext
{
    /// <summary>
    /// Refreshes the tree presentation after a layout update.
    /// </summary>
    /// <param name="sceneAdapter">Scene adapter owning the layout.</param>
    /// <remarks>
    /// Invoked after the organizer updates <see cref="World.Scene.ExplorerLayout" />.
    /// Implementations should rebuild the shown items (and selection/expansion according to
    /// layout flags) so that VM-owned UI state matches the new layout. This is the primary
    /// hook for honoring the organizer's layout changes in the view layer.
    /// </remarks>
    /// <returns>Task that completes when refresh is done.</returns>
    public Task RefreshTreeAsync(SceneAdapter sceneAdapter);
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
