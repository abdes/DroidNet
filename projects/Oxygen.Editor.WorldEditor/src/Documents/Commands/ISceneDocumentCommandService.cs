// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Command-shaped entry point for scene document mutations.
/// </summary>
public interface ISceneDocumentCommandService
{
    /// <summary>Captures selected nodes and folders without authoring or reducing mixed selections.</summary>
    /// <param name="context">The source document.</param>
    /// <param name="nodeIds">Selected node identities in order.</param>
    /// <param name="folderIds">Selected folder identities in order.</param>
    /// <returns>The owned payload, or a published validation failure.</returns>
    public SceneValueCommandResult<SceneExplorerClipboard> CaptureExplorerClipboard(
        SceneDocumentCommandContext context, IReadOnlyList<Guid> nodeIds, IReadOnlyList<Guid> folderIds);

    /// <summary>Validates the entire clipboard transaction without mutation.</summary>
    /// <param name="context">The destination document.</param>
    /// <param name="payload">The captured payload.</param>
    /// <param name="cut">Whether original identities are moved rather than duplicated.</param>
    /// <param name="parentNodeId">Destination scene scope.</param>
    /// <param name="parentFolderId">Destination grouping.</param>
    /// <param name="preserveWorld">Explicit preserve-world intent; false is the Explorer default.</param>
    /// <returns>A disabled reason, or null when the entire batch is valid.</returns>
    public string? ValidateExplorerPaste(SceneDocumentCommandContext context, SceneExplorerClipboard payload,
        bool cut, Guid? parentNodeId, Guid? parentFolderId, bool preserveWorld);

    /// <summary>Commits a validated mixed clipboard batch atomically, recording one undo step.</summary>
    /// <param name="context">The destination document.</param>
    /// <param name="payload">The captured payload.</param>
    /// <param name="cut">Whether original identities are moved rather than duplicated.</param>
    /// <param name="parentNodeId">Destination scene scope.</param>
    /// <param name="parentFolderId">Destination grouping.</param>
    /// <param name="preserveWorld">Explicit preserve-world intent; false is the Explorer default.</param>
    /// <param name="insertAfterNodeId">Optional sibling anchor.</param>
    /// <returns>The whole transaction's result.</returns>
    public Task<SceneCommandResult> PasteExplorerItemsAsync(SceneDocumentCommandContext context,
        SceneExplorerClipboard payload, bool cut, Guid? parentNodeId, Guid? parentFolderId,
        bool preserveWorld, Guid? insertAfterNodeId = null);
    /// <summary>Acquires the scene's existing save gate and captures its saved-input state on the UI thread.</summary>
    /// <param name="context">The still-current document and scene owner.</param>
    /// <param name="cancellationToken">Cancels waiting for an in-flight save.</param>
    /// <returns>The read lease, or null if the document has closed or its scene was replaced.</returns>
    public Task<CookDocumentReadLease?> AcquireCookReadAsync(SceneDocumentCommandContext context, CancellationToken cancellationToken);

    /// <summary>
    /// Creates a primitive scene node.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="kind">The primitive kind.</param>
    /// <returns>The command result with the created node.</returns>
    public Task<SceneValueCommandResult<SceneNode>> CreatePrimitiveAsync(SceneDocumentCommandContext context, string kind);

    /// <summary>
    /// Creates a light scene node.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="kind">The light kind.</param>
    /// <returns>The command result with the created node.</returns>
    public Task<SceneValueCommandResult<SceneNode>> CreateLightAsync(SceneDocumentCommandContext context, string kind);

    /// <summary>
    /// Edits transform component values on one or more nodes.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The node ids to edit.</param>
    /// <param name="edit">The transform edit payload.</param>
    /// <param name="session">The edit session token.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> EditTransformAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        TransformEdit edit,
        EditSessionToken session);

    /// <summary>
    /// Edits descriptor-addressed component properties on one or more nodes.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The node ids to edit.</param>
    /// <param name="edit">The property edit payload.</param>
    /// <param name="label">The history label.</param>
    /// <param name="session">The edit session token.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> EditPropertiesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        PropertyEdit edit,
        string label,
        EditSessionToken session);

    /// <summary>
    /// Edits geometry component values on one or more nodes.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The node ids to edit.</param>
    /// <param name="edit">The geometry edit payload.</param>
    /// <param name="session">The edit session token.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> EditGeometryAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        GeometryEdit edit,
        EditSessionToken session);

    /// <summary>
    /// Edits a material override slot on one or more geometry components.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The node ids to edit.</param>
    /// <param name="target">The native slot and inventory observed before the edit.</param>
    /// <param name="newMaterialUri">The new material URI, or <see langword="null"/> to clear the override.</param>
    /// <param name="session">The edit session token.</param>
    /// <param name="cancellationToken">Cancels metadata acquisition before any mutation.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> EditMaterialSlotAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        MaterialSlotTarget target,
        Uri? newMaterialUri,
        EditSessionToken session,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Edits perspective camera component values on one or more nodes.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The node ids to edit.</param>
    /// <param name="edit">The camera edit payload.</param>
    /// <param name="session">The edit session token.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> EditPerspectiveCameraAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        PerspectiveCameraEdit edit,
        EditSessionToken session);

    /// <summary>
    /// Edits directional light component values on one or more nodes.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The node ids to edit.</param>
    /// <param name="edit">The light edit payload.</param>
    /// <param name="session">The edit session token.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> EditDirectionalLightAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        DirectionalLightEdit edit,
        EditSessionToken session);

    /// <summary>
    /// Adds a component to a scene node.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeId">The node id.</param>
    /// <param name="componentType">The component type to add.</param>
    /// <returns>The command result with the added component.</returns>
    public Task<SceneValueCommandResult<GameComponent>> AddComponentAsync(
        SceneDocumentCommandContext context,
        Guid nodeId,
        Type componentType);

    /// <summary>
    /// Removes a component from a scene node.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeId">The node id.</param>
    /// <param name="componentId">The component id.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> RemoveComponentAsync(
        SceneDocumentCommandContext context,
        Guid nodeId,
        Guid componentId);

    /// <summary>
    /// Edits scene environment values.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="edit">The scene environment edit payload.</param>
    /// <param name="session">The edit session token.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> EditSceneEnvironmentAsync(
        SceneDocumentCommandContext context,
        SceneEnvironmentEdit edit,
        EditSessionToken session);

    /// <summary>
    /// Edits descriptor-addressed scene environment properties.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="edit">The property edit payload.</param>
    /// <param name="label">The history label.</param>
    /// <param name="session">The edit session token.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> EditSceneEnvironmentPropertiesAsync(
        SceneDocumentCommandContext context,
        PropertyEdit edit,
        string label,
        EditSessionToken session);

    /// <summary>Applies target-specific property values as one validated transaction or gesture.</summary>
    /// <param name="context">The document and its lifetime.</param>
    /// <param name="edits">Immutable values identified by authored target.</param>
    /// <param name="label">The undo label.</param>
    /// <param name="session">The control's edit session.</param>
    /// <returns>The authoring and synchronization outcome.</returns>
    public Task<SceneCommandResult> EditPropertiesForTargetsAsync(
        SceneDocumentCommandContext context,
        IReadOnlyDictionary<Guid, PropertyEdit> edits,
        string label,
        EditSessionToken session);

    /// <summary>
    /// Saves the current scene document.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> SaveSceneAsync(SceneDocumentCommandContext context);

    /// <summary>Completes the document's active property gestures before saving, reloading or closing.</summary>
    /// <param name="context">The owning document instance.</param>
    /// <param name="commit">Whether to commit the accepted previews or restore their original values.</param>
    /// <returns>Completion of all terminal publications.</returns>
    public Task CompleteEditSessionsAsync(SceneDocumentCommandContext context, bool commit);

    /// <summary>Saves a distinct scene asset without redirecting or acknowledging the original document.</summary>
    /// <param name="context">The source document.</param>
    /// <param name="name">The new scene file name stem.</param>
    /// <returns>The newly created scene or the reported failure.</returns>
    public Task<SceneValueCommandResult<Scene>> SaveSceneCopyAsync(SceneDocumentCommandContext context, string name);

    /// <summary>Reloads source after explicit discard confirmation, retiring the previous model and history.</summary>
    /// <param name="context">The document whose current source is being replaced.</param>
    /// <param name="cancellationToken">Cancels before accepting the replacement.</param>
    /// <returns>The replacement model, or a failure retaining the original authoring state.</returns>
    public Task<SceneValueCommandResult<Scene>> ReloadSceneAsync(SceneDocumentCommandContext context, CancellationToken cancellationToken = default);

    /// <summary>Creates an empty scene node under a node, a folder, or the scene root.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="parentNodeId">The destination parent node, or <see langword="null"/> for root/folder scope.</param>
    /// <param name="parentFolderId">The destination folder for grouping, or <see langword="null"/> when not grouping.</param>
    /// <param name="name">The node name.</param>
    /// <returns>The command result with the created node.</returns>
    public Task<SceneValueCommandResult<SceneNode>> CreateNodeAsync(
        SceneDocumentCommandContext context,
        Guid? parentNodeId,
        Guid? parentFolderId,
        string name);

    /// <summary>Creates an explorer folder under a node, a folder, or the scene root.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="parentFolderId">The destination parent folder, or <see langword="null"/> for root/node scope.</param>
    /// <param name="parentNodeId">The destination parent node, or <see langword="null"/> for root/folder scope.</param>
    /// <param name="name">The folder name.</param>
    /// <returns>The command result with the created folder id.</returns>
    public Task<SceneValueCommandResult<Guid>> CreateFolderAsync(
        SceneDocumentCommandContext context,
        Guid? parentFolderId,
        Guid? parentNodeId,
        string name);

    /// <summary>Renames a scene node by identity.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeId">The node to rename.</param>
    /// <param name="newName">The new name.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> RenameNodeAsync(
        SceneDocumentCommandContext context,
        Guid nodeId,
        string newName);

    /// <summary>Renames an explorer folder by identity.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="folderId">The folder to rename.</param>
    /// <param name="newName">The new name.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> RenameFolderAsync(
        SceneDocumentCommandContext context,
        Guid folderId,
        string newName);

    /// <summary>Deletes node hierarchies (subtree deletion) in one atomic transaction.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The hierarchy roots to delete.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> DeleteNodesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds);

    /// <summary>Deletes an explorer folder, promoting its contained entries in place (grouping-only).</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="folderId">The folder to remove.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> DeleteFolderAsync(
        SceneDocumentCommandContext context,
        Guid folderId);

    /// <summary>Deletes node hierarchies and folders together in one atomic transaction.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The hierarchy roots to delete.</param>
    /// <param name="folderIds">The folders to remove (grouping-only promotion).</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> DeleteItemsAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        IReadOnlyList<Guid> folderIds);

    /// <summary>Reparents node hierarchies, optionally preserving world pose, in one atomic transaction.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The hierarchy roots to reparent.</param>
    /// <param name="newParentNodeId">The destination parent, or <see langword="null"/> for the scene root.</param>
    /// <param name="preserveWorldTransform">When <see langword="true"/>, preserves world pose rather than local TRS.</param>
    /// <param name="insertAfterNodeId">Optional sibling to insert after, instead of appending to the parent.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> ReparentNodesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        Guid? newParentNodeId,
        bool preserveWorldTransform,
        Guid? insertAfterNodeId = null);

    /// <summary>Groups node entries into a folder, reparenting with preserve-local pose first when the
    /// folder's scene scope differs from a node's current scene parent.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The nodes to group.</param>
    /// <param name="folderId">The destination folder.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> MoveNodesToFolderAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        Guid folderId);

    /// <summary>Removes node entries from a folder, promoting them to the enclosing visual container.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The nodes to remove from the folder.</param>
    /// <param name="folderId">The source folder.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> RemoveNodesFromFolderAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        Guid folderId);

    /// <summary>Moves a folder atomically, reparenting its node roots with preserve-local pose when destination lineage changes.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="folderId">The folder to move.</param>
    /// <param name="newParentFolderId">The destination parent folder, or <see langword="null"/> for the scene root.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> MoveFolderToParentAsync(
        SceneDocumentCommandContext context,
        Guid folderId,
        Guid? newParentFolderId);

    /// <summary>Moves a node's layout entry to a sibling index within a container without changing scene parenting.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeId">The node to reorder.</param>
    /// <param name="parentFolderId">The destination folder, or <see langword="null"/> for root/node scope.</param>
    /// <param name="parentNodeId">The destination node, or <see langword="null"/> for root/folder scope.</param>
    /// <param name="index">The sibling insertion index within the destination.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> ReorderNodesAsync(
        SceneDocumentCommandContext context,
        Guid nodeId,
        Guid? parentFolderId,
        Guid? parentNodeId,
        int index);

    /// <summary>
    /// Hides or shows nodes in the editing viewports ("Show in Editor"), recording one undo step for
    /// the batch without dirtying the document.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The nodes to hide or show; the batch is one undo step.</param>
    /// <param name="hidden">When <see langword="true"/>, hide the nodes; when <see langword="false"/>, show them.</param>
    /// <returns>The command result.</returns>
    /// <remarks>
    /// This is the one ratified command that records history without marking the document dirty: the
    /// user expects Ctrl+Z to reach an accidental hide, while saved content, source hashes and cook
    /// demand must stay untouched. It never writes <c>SceneNodeFlags::kVisible</c> and never routes
    /// through authored visibility; the state lives in workspace storage, and the step is applied and
    /// reverted through this owner so no second history authority exists.
    /// </remarks>
    public Task<SceneCommandResult> SetEditorHiddenAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        bool hidden);

    /// <summary>Deep-copies node hierarchies and inserts them under a node, folder, or the scene root.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The hierarchy roots to duplicate.</param>
    /// <param name="newParentNodeId">The destination parent node, or <see langword="null"/> for root/folder scope.</param>
    /// <param name="newParentFolderId">The destination folder for grouping, or <see langword="null"/> when not grouping.</param>
    /// <param name="insertAfterNodeId">Optional sibling to insert after, instead of appending to the parent.</param>
    /// <returns>The command result with the created node roots.</returns>
    public Task<SceneValueCommandResult<IReadOnlyList<SceneNode>>> DuplicateNodesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        Guid? newParentNodeId,
        Guid? newParentFolderId,
        Guid? insertAfterNodeId = null);

    /// <summary>Deep-copies previously captured node snapshots and inserts them under a node, folder, or the scene root.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="rootData">The captured root DTOs to duplicate.</param>
    /// <param name="newParentNodeId">The destination parent node, or <see langword="null"/> for root/folder scope.</param>
    /// <param name="newParentFolderId">The destination folder for grouping, or <see langword="null"/> when not grouping.</param>
    /// <param name="insertAfterNodeId">Optional sibling to insert after, instead of appending to the parent.</param>
    /// <returns>The command result with the created node roots.</returns>
    public Task<SceneValueCommandResult<IReadOnlyList<SceneNode>>> DuplicateNodesFromDataAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<SceneNodeData> rootData,
        Guid? newParentNodeId,
        Guid? newParentFolderId,
        Guid? insertAfterNodeId = null);
}
