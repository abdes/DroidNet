// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
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
    /// Creates a folder containing the specified scene node ids, returning a new layout.
    /// </summary>
    /// <param name="selectedNodeIds">The node identifiers to group.</param>
    /// <param name="scene">The owning scene.</param>
    /// <param name="sceneAdapter">The scene adapter for contextual info (e.g., folder adapters).</param>
    /// <returns>A layout change record with the new layout and folder info.</returns>
    public LayoutChangeRecord CreateFolderFromSelection(HashSet<Guid> selectedNodeIds, Scene scene, SceneAdapter sceneAdapter);

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
}

/// <summary>
/// Provides VM-owned callbacks the organizer needs when reconciling layout changes.
/// </summary>
public interface ILayoutContext
{
    /// <summary>
    /// Gets the zero-based index of a visible item, if present.
    /// </summary>
    /// <param name="item">Item to locate.</param>
    /// <remarks>
    /// The index refers to the VM's current shown-item projection. When the root is shown,
    /// it occupies index 0; when the root is hidden, it is absent and this method must
    /// return <see langword="null" /> for it. Implementations should not force realization
    /// of items; return <see langword="null" /> if the item is not visible.
    /// </remarks>
    /// <returns>Index in the shown list, or <see langword="null" /> if not visible.</returns>
    public int? GetShownIndex(ITreeItem item);

    /// <summary>
    /// Removes an item from the shown list if present.
    /// </summary>
    /// <param name="item">Item to remove.</param>
    /// <remarks>
    /// Used when the organizer moves an adapter between parents. Implementations must be
    /// no-ops for items that are not currently visible.
    /// </remarks>
    /// <returns><see langword="true" /> when removed.</returns>
    public bool TryRemoveShownItem(ITreeItem item);

    /// <summary>
    /// Inserts an item into the shown list at the specified position.
    /// </summary>
    /// <param name="index">Insert position.</param>
    /// <param name="item">Item to insert.</param>
    /// <remarks>
    /// The organizer uses this when a folder is expanded and children must appear inline in
    /// the UI representation. Implementations must keep ordering consistent with the tree's
    /// flattened projection (e.g., parent immediately followed by its visible descendants).
    /// </remarks>
    public void InsertShownItem(int index, ITreeItem item);

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

    /// <summary>
    /// Attempts to locate the contiguous visible span for a tree item and its visible descendants.
    /// </summary>
    /// <param name="root">Root item of the span.</param>
    /// <param name="startIndex">Start index in the shown list.</param>
    /// <param name="count">Number of items in the span.</param>
    /// <returns><see langword="true" /> if the item is visible and span computed.</returns>
    public bool TryGetVisibleSpan(ITreeItem root, out int startIndex, out int count);

    /// <summary>
    /// Applies a batched removal/insert delta to the shown list.
    /// </summary>
    /// <param name="delta">Delta describing removal and insertion.</param>
    public void ApplyShownDelta(ShownItemsDelta delta);
}

/// <summary>
/// Describes a minimal mutation to the flattened shown-items projection.
/// </summary>
/// <param name="RemoveStart">Start index for removal.</param>
/// <param name="RemoveCount">Number of items to remove.</param>
/// <param name="InsertIndex">Index at which to insert.</param>
/// <param name="InsertItems">Items to insert, in order.</param>
public sealed record ShownItemsDelta(int RemoveStart, int RemoveCount, int InsertIndex, IReadOnlyList<ITreeItem> InsertItems);

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
