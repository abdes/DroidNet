// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Command-shaped entry point for scene document mutations.
/// </summary>
public interface ISceneDocumentCommandService
{
    public Task<SceneCommandResult<SceneNode>> CreatePrimitiveAsync(SceneDocumentCommandContext context, string kind);

    public Task<SceneCommandResult<SceneNode>> CreateLightAsync(SceneDocumentCommandContext context, string kind);

    public Task<SceneCommandResult> EditTransformAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        TransformEdit edit,
        EditSessionToken session);

    public Task<SceneCommandResult> EditGeometryAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        GeometryEdit edit,
        EditSessionToken session);

    public Task<SceneCommandResult> EditMaterialSlotAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        int slotIndex,
        Uri? newMaterialUri,
        EditSessionToken session);

    public Task<SceneCommandResult> EditPerspectiveCameraAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        PerspectiveCameraEdit edit,
        EditSessionToken session);

    public Task<SceneCommandResult> EditDirectionalLightAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        DirectionalLightEdit edit,
        EditSessionToken session);

    public Task<SceneCommandResult<GameComponent>> AddComponentAsync(
        SceneDocumentCommandContext context,
        Guid nodeId,
        Type componentType);

    public Task<SceneCommandResult> RemoveComponentAsync(
        SceneDocumentCommandContext context,
        Guid nodeId,
        Guid componentId);

    public Task<SceneCommandResult> EditSceneEnvironmentAsync(
        SceneDocumentCommandContext context,
        SceneEnvironmentEdit edit,
        EditSessionToken session);

    public Task<SceneCommandResult> SaveSceneAsync(SceneDocumentCommandContext context);

    public Task<SceneCommandResult> RenameItemAsync(
        SceneDocumentCommandContext context,
        ITreeItem item,
        string newName);
}
