// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Command-shaped entry point for scene document mutations.
/// </summary>
public interface ISceneDocumentCommandService
{
    /// <summary>Occurs when a command added nodes to a scene.</summary>
    public event EventHandler<SceneNodesChangedEventArgs>? NodesAdded;

    /// <summary>Occurs when a command removed nodes from a scene.</summary>
    public event EventHandler<SceneNodesChangedEventArgs>? NodesRemoved;

    /// <summary>Occurs when a command added a component to a node.</summary>
    public event EventHandler<SceneComponentChangedEventArgs>? ComponentAdded;

    /// <summary>Occurs when a command removed a component from a node.</summary>
    public event EventHandler<SceneComponentChangedEventArgs>? ComponentRemoved;

    /// <summary>Occurs when a command that added or removed nodes left a selection in its document.</summary>
    public event EventHandler<SceneCommandSelectionEventArgs>? SelectionChanged;

    /// <summary>Captures selected nodes and folders without authoring or reducing mixed selections.</summary>
    /// <param name="context">The source document.</param>
    /// <param name="nodeIds">Selected node identities in order.</param>
    /// <param name="folderIds">Selected folder identities in order.</param>
    /// <returns>The owned payload, or a published validation failure.</returns>
    public SceneValueCommandResult<SceneExplorerClipboard> CaptureExplorerClipboard(
        SceneDocumentCommandContext context, IReadOnlyList<NodeId> nodeIds, IReadOnlyList<FolderId> folderIds);

    /// <summary>Validates the entire clipboard transaction without mutation.</summary>
    /// <param name="context">The destination document.</param>
    /// <param name="payload">The captured payload.</param>
    /// <param name="cut">Whether original identities are moved rather than duplicated.</param>
    /// <param name="parentNodeId">Destination scene scope.</param>
    /// <param name="parentFolderId">Destination grouping.</param>
    /// <param name="preserveWorld">Explicit preserve-world intent; false is the Explorer default.</param>
    /// <returns>A disabled reason, or null when the entire batch is valid.</returns>
    public string? ValidateExplorerPaste(
        SceneDocumentCommandContext context,
        SceneExplorerClipboard payload,
        bool cut,
        NodeId? parentNodeId,
        FolderId? parentFolderId,
        bool preserveWorld);

    /// <summary>Commits a validated mixed clipboard batch atomically, recording one undo step.</summary>
    /// <param name="context">The destination document.</param>
    /// <param name="payload">The captured payload.</param>
    /// <param name="cut">Whether original identities are moved rather than duplicated.</param>
    /// <param name="parentNodeId">Destination scene scope.</param>
    /// <param name="parentFolderId">Destination grouping.</param>
    /// <param name="preserveWorld">Explicit preserve-world intent; false is the Explorer default.</param>
    /// <param name="insertAfterNodeId">Optional sibling anchor.</param>
    /// <returns>The whole transaction's result.</returns>
    public Task<SceneCommandResult> PasteExplorerItemsAsync(
        SceneDocumentCommandContext context,
        SceneExplorerClipboard payload,
        bool cut,
        NodeId? parentNodeId,
        FolderId? parentFolderId,
        bool preserveWorld,
        NodeId? insertAfterNodeId = null);

    /// <summary>
    /// Creates a primitive scene node.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="kind">The primitive kind.</param>
    /// <param name="placement">Where the root node is created; the scene origin when <see langword="null"/>.</param>
    /// <returns>The command result with the created node.</returns>
    public Task<SceneValueCommandResult<SceneNode>> CreatePrimitiveAsync(SceneDocumentCommandContext context, string kind, NodePlacement? placement = null);

    /// <summary>
    /// Creates a light scene node.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="kind">The light kind.</param>
    /// <param name="placement">
    /// Where the root node is created; the light kind's default position when <see langword="null"/>.
    /// A placement without a rotation keeps the kind's default orientation.
    /// </param>
    /// <returns>The command result with the created node.</returns>
    public Task<SceneValueCommandResult<SceneNode>> CreateLightAsync(SceneDocumentCommandContext context, string kind, NodePlacement? placement = null);

    /// <summary>
    /// Creates a root scene node carrying a local fog volume.
    /// </summary>
    /// <param name="context">The captured scene document context.</param>
    /// <param name="placement">Where the root node is created; the scene origin when <see langword="null"/>.</param>
    /// <returns>The command result with the created node.</returns>
    public Task<SceneValueCommandResult<SceneNode>> CreateLocalFogVolumeAsync(SceneDocumentCommandContext context, NodePlacement? placement = null);

    /// <summary>
    /// Creates a camera scene node.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="kind">The camera kind: <c>Perspective</c> or <c>Orthographic</c>.</param>
    /// <param name="placement">Where the root node is created; the scene origin when <see langword="null"/>.</param>
    /// <returns>The command result with the created node.</returns>
    public Task<SceneValueCommandResult<SceneNode>> CreateCameraAsync(SceneDocumentCommandContext context, string kind, NodePlacement? placement = null);

    /// <summary>
    /// Creates an empty root scene node.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="placement">Where the root node is created; the scene origin when <see langword="null"/>.</param>
    /// <returns>The command result with the created node.</returns>
    public Task<SceneValueCommandResult<SceneNode>> CreateEmptyNodeAsync(SceneDocumentCommandContext context, NodePlacement? placement = null);

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
        IReadOnlyList<NodeId> nodeIds,
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
        IReadOnlyList<NodeId> nodeIds,
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
        IReadOnlyList<NodeId> nodeIds,
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
        IReadOnlyList<NodeId> nodeIds,
        MaterialSlotTarget target,
        Uri? newMaterialUri,
        EditSessionToken session,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Clears material overrides that no longer resolve against the geometry each node uses.
    /// </summary>
    /// <remarks>
    /// An override is unresolved when it targets another geometry, for example after the node's
    /// geometry was replaced, or a slot the current geometry inventory no longer reports. Resolved
    /// overrides are kept. The removal is one undoable edit.
    /// </remarks>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The node ids whose unresolved overrides are cleared.</param>
    /// <param name="cancellationToken">Cancels metadata acquisition before any mutation.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> ClearUnresolvedMaterialSlotsAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<NodeId> nodeIds,
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
        IReadOnlyList<NodeId> nodeIds,
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
        IReadOnlyList<NodeId> nodeIds,
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
        NodeId nodeId,
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
        NodeId nodeId,
        ComponentId componentId);

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

    /// <summary>Replaces validated scene-level asset references as one undoable edit.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="references">The complete replacement reference set.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> EditSceneReferencesAsync(
        SceneDocumentCommandContext context,
        SceneReferencesData references);

    /// <summary>Replaces how the scene starts when played as one undoable edit.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="play">The complete replacement play settings.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> EditScenePlayAsync(
        SceneDocumentCommandContext context,
        ScenePlayData play);

    /// <summary>Replaces a node's script slots as one undoable edit.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeId">The node that owns the scripting component.</param>
    /// <param name="slots">The complete replacement slots, in order.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> EditScriptSlotsAsync(
        SceneDocumentCommandContext context,
        NodeId nodeId,
        ImmutableArray<ScriptSlot> slots);

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
        IReadOnlyDictionary<NodeId, PropertyEdit> edits,
        string label,
        EditSessionToken session);

    /// <summary>Completes a scene's active property gestures before saving, reloading or closing.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="commit">Whether to commit the accepted previews or restore their original values.</param>
    /// <returns>Completion of all terminal publications.</returns>
    public Task CompleteEditSessionsAsync(Scene scene, bool commit);

    /// <summary>Determines whether a gesture in progress shows the scene changed, before it is recorded.</summary>
    /// <param name="scene">The scene.</param>
    /// <returns>Whether an open gesture's preview differs from where it started.</returns>
    public bool HasEditInProgress(Scene scene);

    /// <summary>Creates an empty scene node under a node, a folder, or the scene root.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="parentNodeId">The destination parent node, or <see langword="null"/> for root/folder scope.</param>
    /// <param name="parentFolderId">The destination folder for grouping, or <see langword="null"/> when not grouping.</param>
    /// <param name="name">The node name.</param>
    /// <returns>The command result with the created node.</returns>
    public Task<SceneValueCommandResult<SceneNode>> CreateNodeAsync(
        SceneDocumentCommandContext context,
        NodeId? parentNodeId,
        FolderId? parentFolderId,
        string name);

    /// <summary>Creates an explorer folder under a node, a folder, or the scene root.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="parentFolderId">The destination parent folder, or <see langword="null"/> for root/node scope.</param>
    /// <param name="parentNodeId">The destination parent node, or <see langword="null"/> for root/folder scope.</param>
    /// <param name="name">The folder name.</param>
    /// <returns>The command result with the created folder id.</returns>
    public Task<SceneValueCommandResult<FolderId>> CreateFolderAsync(
        SceneDocumentCommandContext context,
        FolderId? parentFolderId,
        NodeId? parentNodeId,
        string name);

    /// <summary>Shows relocated nodes and the environment again in the runtime, once the assets they reference are mounted.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The re-pointed nodes to attach again; nodes no longer in the scene are skipped.</param>
    /// <param name="environment">Whether to update the environment.</param>
    /// <returns>Completion after the runtime accepted the refresh, or skipped it for a scene it no longer shows.</returns>
    public Task RefreshRelocatedRuntimeAsync(SceneDocumentCommandContext context, IReadOnlyCollection<NodeId> nodeIds, bool environment);

    /// <summary>Renames a scene node by identity.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeId">The node to rename.</param>
    /// <param name="newName">The new name.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> RenameNodeAsync(
        SceneDocumentCommandContext context,
        NodeId nodeId,
        string newName);

    /// <summary>Renames an explorer folder by identity.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="folderId">The folder to rename.</param>
    /// <param name="newName">The new name.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> RenameFolderAsync(
        SceneDocumentCommandContext context,
        FolderId folderId,
        string newName);

    /// <summary>Deletes node hierarchies (subtree deletion) in one atomic transaction.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The hierarchy roots to delete.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> DeleteNodesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<NodeId> nodeIds);

    /// <summary>Deletes an explorer folder, promoting its contained entries in place (grouping-only).</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="folderId">The folder to remove.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> DeleteFolderAsync(
        SceneDocumentCommandContext context,
        FolderId folderId);

    /// <summary>Deletes node hierarchies and folders together in one atomic transaction.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The hierarchy roots to delete.</param>
    /// <param name="folderIds">The folders to remove (grouping-only promotion).</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> DeleteItemsAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<NodeId> nodeIds,
        IReadOnlyList<FolderId> folderIds);

    /// <summary>Reparents node hierarchies, optionally preserving world pose, in one atomic transaction.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The hierarchy roots to reparent.</param>
    /// <param name="newParentNodeId">The destination parent, or <see langword="null"/> for the scene root.</param>
    /// <param name="preserveWorldTransform">When <see langword="true"/>, preserves world pose rather than local TRS.</param>
    /// <param name="insertAfterNodeId">Optional sibling to insert after, instead of appending to the parent.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> ReparentNodesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<NodeId> nodeIds,
        NodeId? newParentNodeId,
        bool preserveWorldTransform,
        NodeId? insertAfterNodeId = null);

    /// <summary>Groups node entries into a folder, reparenting with preserve-local pose first when the
    /// folder's scene scope differs from a node's current scene parent.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The nodes to group.</param>
    /// <param name="folderId">The destination folder.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> MoveNodesToFolderAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<NodeId> nodeIds,
        FolderId folderId);

    /// <summary>Removes node entries from a folder, promoting them to the enclosing visual container.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The nodes to remove from the folder.</param>
    /// <param name="folderId">The source folder.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> RemoveNodesFromFolderAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<NodeId> nodeIds,
        FolderId folderId);

    /// <summary>Moves a folder atomically, reparenting its node roots with preserve-local pose when destination lineage changes.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="folderId">The folder to move.</param>
    /// <param name="newParentFolderId">The destination parent folder, or <see langword="null"/> for the scene root.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> MoveFolderToParentAsync(
        SceneDocumentCommandContext context,
        FolderId folderId,
        FolderId? newParentFolderId);

    /// <summary>Moves a node's layout entry to a sibling index within a container without changing scene parenting.</summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeId">The node to reorder.</param>
    /// <param name="parentFolderId">The destination folder, or <see langword="null"/> for root/node scope.</param>
    /// <param name="parentNodeId">The destination node, or <see langword="null"/> for root/folder scope.</param>
    /// <param name="index">The sibling insertion index within the destination.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> ReorderNodesAsync(
        SceneDocumentCommandContext context,
        NodeId nodeId,
        FolderId? parentFolderId,
        NodeId? parentNodeId,
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
        IReadOnlyList<NodeId> nodeIds,
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
        IReadOnlyList<NodeId> nodeIds,
        NodeId? newParentNodeId,
        FolderId? newParentFolderId,
        NodeId? insertAfterNodeId = null);

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
        NodeId? newParentNodeId,
        FolderId? newParentFolderId,
        NodeId? insertAfterNodeId = null);

    /// <summary>
    /// Deep-copies node hierarchies beside their sources, in the same scene parent and Explorer
    /// folder, as one undo step.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="nodeIds">The nodes to copy; a node whose ancestor is listed is copied with it.</param>
    /// <param name="transforms">Local transforms the copies take instead of their sources', by source id.</param>
    /// <returns>The command result with the created node roots, in source order.</returns>
    public Task<SceneValueCommandResult<IReadOnlyList<SceneNode>>> DuplicateNodesInPlaceAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<NodeId> nodeIds,
        IReadOnlyDictionary<NodeId, TransformData>? transforms = null);
}
