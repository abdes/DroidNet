// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Command-shaped entry point for scene document mutations.
/// </summary>
public interface ISceneDocumentCommandService
{
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
    /// <param name="slotIndex">The material slot index.</param>
    /// <param name="newMaterialUri">The new material URI, or <see langword="null"/> to clear the override.</param>
    /// <param name="session">The edit session token.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> EditMaterialSlotAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        int slotIndex,
        Uri? newMaterialUri,
        EditSessionToken session);

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

    /// <summary>
    /// Saves the current scene document.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> SaveSceneAsync(SceneDocumentCommandContext context);

    /// <summary>
    /// Renames a tree item in the scene document.
    /// </summary>
    /// <param name="context">The document command context.</param>
    /// <param name="item">The item to rename.</param>
    /// <param name="newName">The new display name.</param>
    /// <returns>The command result.</returns>
    public Task<SceneCommandResult> RenameItemAsync(
        SceneDocumentCommandContext context,
        ITreeItem item,
        string newName);
}
