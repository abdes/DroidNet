// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Oxygen.Assets.Model;
using Oxygen.Core;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <inheritdoc />
public sealed partial class SceneDocumentCommandService : ISceneDocumentCommandService
{
    private static readonly Uri EmptyMaterialUri = new($"{AssetUris.Scheme}:///__uninitialized__");

    private readonly ISceneExplorerService sceneExplorerService;
    private readonly ISceneSelectionService selectionService;
    private readonly ISceneEngineSync sceneEngineSync;
    private readonly IProjectManagerService projectManager;
    private readonly IDocumentService documentService;
    private readonly WindowId windowId;
    private readonly IMessenger messenger;
    private readonly IOperationResultPublisher operationResults;
    private readonly IStatusReducer statusReducer;
    private readonly Dictionary<Guid, TransformEditSessionState> transformEditSessions = [];

    public SceneDocumentCommandService(
        ISceneExplorerService sceneExplorerService,
        ISceneSelectionService selectionService,
        ISceneEngineSync sceneEngineSync,
        IProjectManagerService projectManager,
        IDocumentService documentService,
        WindowId windowId,
        IMessenger messenger,
        IOperationResultPublisher operationResults,
        IStatusReducer statusReducer)
    {
        this.sceneExplorerService = sceneExplorerService;
        this.selectionService = selectionService;
        this.sceneEngineSync = sceneEngineSync;
        this.projectManager = projectManager;
        this.documentService = documentService;
        this.windowId = windowId;
        this.messenger = messenger;
        this.operationResults = operationResults;
        this.statusReducer = statusReducer;
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult<SceneNode>> CreatePrimitiveAsync(SceneDocumentCommandContext context, string kind)
    {
        try
        {
            var normalized = NormalizePrimitiveKind(kind);
            var node = new SceneNode(context.Scene) { Name = normalized };
            _ = node.AddComponent(new GeometryComponent
            {
                Name = "Geometry",
                Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri($"BasicShapes/{normalized}")),
            });

            await this.AddRootNodeAsync(context, node, SceneOperationKinds.NodeCreatePrimitive, $"Create {normalized}").ConfigureAwait(true);
            return SceneCommandResult<SceneNode>.Success(node);
        }
        catch (Exception ex)
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.NodeCreatePrimitive,
                DiagnosticCodes.ScenePrefix + "CREATE_PRIMITIVE_FAILED",
                "Primitive was not created",
                $"The {kind} primitive could not be created.",
                context,
                ex);
            return SceneCommandResult<SceneNode>.Failure(operationResultId);
        }
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult<SceneNode>> CreateLightAsync(SceneDocumentCommandContext context, string kind)
    {
        try
        {
            var normalized = NormalizeLightKind(kind);
            var node = new SceneNode(context.Scene) { Name = $"{normalized} Light" };
            ApplyLightTransform(node, normalized);
            var light = CreateLightComponent(normalized);
            if (light is DirectionalLightComponent directional && CaptureDirectionalSunStates(context.Scene).Any(static state => state.IsSunLight))
            {
                directional.IsSunLight = false;
            }

            _ = node.AddComponent(light);

            await this.AddRootNodeAsync(context, node, SceneOperationKinds.NodeCreateLight, $"Create {normalized} Light").ConfigureAwait(true);
            return SceneCommandResult<SceneNode>.Success(node);
        }
        catch (Exception ex)
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.NodeCreateLight,
                DiagnosticCodes.ScenePrefix + "CREATE_LIGHT_FAILED",
                "Light was not created",
                $"The {kind} light could not be created.",
                context,
                ex);
            return SceneCommandResult<SceneNode>.Failure(operationResultId);
        }
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditTransformAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        TransformEdit edit,
        EditSessionToken session)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(nodeIds);
        ArgumentNullException.ThrowIfNull(edit);
        ArgumentNullException.ThrowIfNull(session);

        if (!HasAnyTransformField(edit))
        {
            return session.State == EditSessionState.Cancelled
                ? await this.CancelTransformEditSessionAsync(context, session).ConfigureAwait(true)
                : SceneCommandResult.Success;
        }

        var validation = ValidateTransformEdit(edit);
        if (validation is not null)
        {
            return this.ValidationFailure(SceneOperationKinds.EditTransform, validation.Value.Code, validation.Value.Title, validation.Value.Message, context);
        }

        var targets = ResolveNodes(context.Scene, nodeIds)
            .Select(static node => new { Node = node, Transform = node.Components.OfType<TransformComponent>().FirstOrDefault() })
            .Where(static target => target.Transform is not null)
            .ToList();
        if (targets.Count == 0)
        {
            return this.ValidationFailure(
                SceneOperationKinds.EditTransform,
                SceneDiagnosticCodes.ComponentRemoveDenied,
                "Transform was not edited",
                "No selected node has a transform component.",
                context);
        }

        if (!session.IsOneShot)
        {
            return await this.EditTransformSessionAsync(
                context,
                session,
                targets.Select(static target => target.Node).ToList(),
                targets.Select(static target => target.Transform!).ToList(),
                edit).ConfigureAwait(true);
        }

        // One-shot path is now schema-driven via the property pipeline.
        // The legacy capture/apply/sync machinery remains for the
        // session path until it is migrated.
        var propertyEdit = BuildPropertyEditFromTransformEdit(edit);
        if (propertyEdit.Count == 0)
        {
            return SceneCommandResult.Success;
        }

        return await this.EditPropertiesAsync(
            context,
            targets.Select(static target => target.Node.Id).ToList(),
            propertyEdit,
            "Edit Transform").ConfigureAwait(true);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditGeometryAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        GeometryEdit edit,
        EditSessionToken session)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(nodeIds);
        ArgumentNullException.ThrowIfNull(edit);
        ArgumentNullException.ThrowIfNull(session);

        if (SkipUncommittedSession(session) is { } sessionResult)
        {
            return sessionResult;
        }

        if (!edit.GeometryUri.HasValue)
        {
            return SceneCommandResult.Success;
        }

        if (edit.GeometryUri.Value is null)
        {
            return this.ValidationFailure(
                SceneOperationKinds.EditGeometry,
                SceneDiagnosticCodes.GeometryReferenceRequired,
                "Geometry was not edited",
                "A geometry component must reference a geometry asset. Remove the component to detach geometry.",
                context);
        }

        var targets = ResolveNodes(context.Scene, nodeIds)
            .Select(static node => new { Node = node, Geometry = node.Components.OfType<GeometryComponent>().FirstOrDefault() })
            .Where(static target => target.Geometry is not null)
            .ToList();
        if (targets.Count == 0)
        {
            return this.ValidationFailure(
                SceneOperationKinds.EditGeometry,
                SceneDiagnosticCodes.ComponentRemoveDenied,
                "Geometry was not edited",
                "No selected node has a geometry component.",
                context);
        }

        var before = targets.Select(static target => GeometryState.Capture(target.Node, target.Geometry!)).ToList();
        foreach (var target in targets)
        {
            target.Geometry!.Geometry = new AssetReference<GeometryAsset>(edit.GeometryUri.Value);
        }

        var after = targets.Select(static target => GeometryState.Capture(target.Node, target.Geometry!)).ToList();
        if (GeometryStatesEqual(before, after))
        {
            return SceneCommandResult.Success;
        }

        this.RecordGeometryHistory(context, before, after);
        var operationResultId = await this.SyncGeometryStatesAsync(context, after, SceneOperationKinds.EditGeometry).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        return new SceneCommandResult(true, operationResultId);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditMaterialSlotAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        int slotIndex,
        Uri? newMaterialUri,
        EditSessionToken session)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(nodeIds);
        ArgumentNullException.ThrowIfNull(session);

        if (SkipUncommittedSession(session) is { } sessionResult)
        {
            return sessionResult;
        }

        if (slotIndex != 0)
        {
            return this.ValidationFailure(
                SceneOperationKinds.EditMaterialSlot,
                SceneDiagnosticCodes.ComponentAddDenied,
                "Material slot was not edited",
                "ED-M04 supports only geometry material slot 0.",
                context);
        }

        var targets = ResolveNodes(context.Scene, nodeIds)
            .Select(static node => new { Node = node, Geometry = node.Components.OfType<GeometryComponent>().FirstOrDefault() })
            .Where(static target => target.Geometry is not null)
            .ToList();
        if (targets.Count == 0)
        {
            return this.ValidationFailure(
                SceneOperationKinds.EditMaterialSlot,
                SceneDiagnosticCodes.ComponentRemoveDenied,
                "Material slot was not edited",
                "No selected node has a geometry component.",
                context);
        }

        var before = targets.Select(static target => MaterialSlotState.Capture(target.Node, target.Geometry!)).ToList();
        foreach (var target in targets)
        {
            ApplyMaterialSlotEdit(target.Geometry!, newMaterialUri);
        }

        var after = targets.Select(static target => MaterialSlotState.Capture(target.Node, target.Geometry!)).ToList();
        if (MaterialSlotStatesEqual(before, after))
        {
            return SceneCommandResult.Success;
        }

        this.RecordMaterialSlotHistory(context, before, after);
        var operationResultId = await this.SyncEditedNodesAsync(
            context,
            targets.Select(static target => target.Node).ToList(),
            SceneOperationKinds.EditMaterialSlot,
            node => this.sceneEngineSync.UpdateMaterialSlotAsync(context.Scene, node, slotIndex, newMaterialUri)).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        return new SceneCommandResult(true, operationResultId);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditPerspectiveCameraAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        PerspectiveCameraEdit edit,
        EditSessionToken session)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(nodeIds);
        ArgumentNullException.ThrowIfNull(edit);
        ArgumentNullException.ThrowIfNull(session);

        if (SkipUncommittedSession(session) is { } sessionResult)
        {
            return sessionResult;
        }

        if (!HasAnyPerspectiveCameraField(edit))
        {
            return SceneCommandResult.Success;
        }

        var validation = ValidatePerspectiveCameraEdit(context.Scene, nodeIds, edit);
        if (validation is not null)
        {
            return this.ValidationFailure(SceneOperationKinds.EditPerspectiveCamera, validation.Value.Code, validation.Value.Title, validation.Value.Message, context);
        }

        var targets = ResolveNodes(context.Scene, nodeIds)
            .Select(static node => new { Node = node, Camera = node.Components.OfType<PerspectiveCamera>().FirstOrDefault() })
            .Where(static target => target.Camera is not null)
            .ToList();
        if (targets.Count == 0)
        {
            return this.ValidationFailure(
                SceneOperationKinds.EditPerspectiveCamera,
                SceneDiagnosticCodes.ComponentRemoveDenied,
                "Camera was not edited",
                "No selected node has a perspective camera component.",
                context);
        }

        var before = targets.Select(static target => CameraState.Capture(target.Node, target.Camera!)).ToList();
        foreach (var target in targets)
        {
            ApplyPerspectiveCameraEdit(target.Camera!, edit);
        }

        var after = targets.Select(static target => CameraState.Capture(target.Node, target.Camera!)).ToList();
        this.RecordCameraHistory(context, before, after);
        var operationResultId = await this.SyncEditedNodesAsync(
            context,
            targets.Select(static target => target.Node).ToList(),
            SceneOperationKinds.EditPerspectiveCamera,
            node => this.sceneEngineSync.AttachCameraAsync(context.Scene, node)).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        return new SceneCommandResult(true, operationResultId);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditDirectionalLightAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        DirectionalLightEdit edit,
        EditSessionToken session)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(nodeIds);
        ArgumentNullException.ThrowIfNull(edit);
        ArgumentNullException.ThrowIfNull(session);

        if (SkipUncommittedSession(session) is { } sessionResult)
        {
            return sessionResult;
        }

        if (!HasAnyDirectionalLightField(edit))
        {
            return SceneCommandResult.Success;
        }

        var validation = ValidateDirectionalLightEdit(edit);
        if (validation is not null)
        {
            return this.ValidationFailure(SceneOperationKinds.EditDirectionalLight, validation.Value.Code, validation.Value.Title, validation.Value.Message, context);
        }

        var targets = ResolveNodes(context.Scene, nodeIds)
            .Select(static node => new { Node = node, Light = node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() })
            .Where(static target => target.Light is not null)
            .ToList();
        if (targets.Count == 0)
        {
            return this.ValidationFailure(
                SceneOperationKinds.EditDirectionalLight,
                SceneDiagnosticCodes.ComponentRemoveDenied,
                "Light was not edited",
                "No selected node has a directional light component.",
                context);
        }

        var allSunStates = CaptureDirectionalSunStates(context.Scene);
        var before = targets.Select(static target => DirectionalLightState.Capture(target.Node, target.Light!)).ToList();
        foreach (var target in targets)
        {
            ApplyDirectionalLightEdit(target.Light!, edit);
        }

        if (edit.IsSunLight.HasValue && edit.IsSunLight.Value == true)
        {
            ApplyExclusiveSun(context.Scene, targets[0].Node.Id);
        }

        var after = targets.Select(static target => DirectionalLightState.Capture(target.Node, target.Light!)).ToList();
        var afterSunStates = CaptureDirectionalSunStates(context.Scene);
        this.RecordDirectionalLightHistory(context, before, after, allSunStates, afterSunStates);
        var syncNodes = IncludeDirectionalSunChangedNodes(
            targets.Select(static target => target.Node),
            allSunStates,
            afterSunStates);
        var operationResultId = await this.SyncEditedNodesAsync(
            context,
            syncNodes,
            SceneOperationKinds.EditDirectionalLight,
            node => this.sceneEngineSync.AttachLightAsync(context.Scene, node)).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        return new SceneCommandResult(true, operationResultId);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult<GameComponent>> AddComponentAsync(
        SceneDocumentCommandContext context,
        Guid nodeId,
        Type componentType)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(componentType);

        var node = FindNode(context.Scene, nodeId);
        if (node is null)
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.AddComponent,
                SceneDiagnosticCodes.ComponentAddDenied,
                "Component was not added",
                "The target scene node no longer exists.",
                context);
            return SceneCommandResult<GameComponent>.Failure(operationResultId);
        }

        if (!CanAddComponent(node, componentType, out var reason))
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.AddComponent,
                SceneDiagnosticCodes.ComponentAddDenied,
                "Component was not added",
                reason,
                context);
            return SceneCommandResult<GameComponent>.Failure(operationResultId);
        }

        var component = CreateComponent(componentType);
        _ = node.AddComponent(component);
        var defaultTransformBefore = CaptureDirectionalLightDefaultTransformBefore(node, component);
        TransformState? defaultTransformAfter = null;
        if (defaultTransformBefore is not null)
        {
            node.Components.OfType<TransformComponent>().First().LocalRotation = DirectionalLightComponent.DefaultLocalRotation;
            defaultTransformAfter = TransformState.Capture(node, node.Components.OfType<TransformComponent>().First());
        }

        context.History.AddChange(
            $"Remove Component ({component.Name})",
            async () => await this.RemoveComponentForUndoAsync(context, node, component, defaultTransformBefore, defaultTransformAfter).ConfigureAwait(true));
        var operationResultIdFromSync = await this.SyncComponentAddAsync(context, node, component).ConfigureAwait(true);
        if (defaultTransformAfter is not null)
        {
            _ = await this.SyncEditedNodesAsync(context, [node], SceneOperationKinds.EditTransform, syncNode => this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, syncNode)).ConfigureAwait(true);
        }

        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        _ = this.messenger.Send(new ComponentAddedMessage(node, component, added: true));
        return new SceneCommandResult<GameComponent>(true, component, operationResultIdFromSync);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> RemoveComponentAsync(
        SceneDocumentCommandContext context,
        Guid nodeId,
        Guid componentId)
    {
        ArgumentNullException.ThrowIfNull(context);

        var node = FindNode(context.Scene, nodeId);
        var component = node?.Components.FirstOrDefault(component => component.Id == componentId);
        if (node is null || component is null)
        {
            return this.ValidationFailure(
                SceneOperationKinds.RemoveComponent,
                SceneDiagnosticCodes.ComponentRemoveDenied,
                "Component was not removed",
                "The target component no longer exists.",
                context);
        }

        if (component.IsLocked || component is TransformComponent)
        {
            return this.ValidationFailure(
                SceneOperationKinds.RemoveComponent,
                SceneDiagnosticCodes.ComponentRemoveDenied,
                "Component was not removed",
                "The transform component is locked and cannot be removed.",
                context);
        }

        _ = node.RemoveComponent(component);
        context.History.AddChange(
            $"Restore Component ({component.Name})",
            async () => await this.AddComponentForRedoAsync(context, node, component, defaultTransformBefore: null, defaultTransformAfter: null).ConfigureAwait(true));
        var operationResultId = await this.SyncComponentRemoveAsync(context, node, component).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        _ = this.messenger.Send(new ComponentRemovedMessage(node, component, removed: true));
        return new SceneCommandResult(true, operationResultId);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditSceneEnvironmentAsync(
        SceneDocumentCommandContext context,
        SceneEnvironmentEdit edit,
        EditSessionToken session)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(edit);
        ArgumentNullException.ThrowIfNull(session);

        if (SkipUncommittedSession(session) is { } sessionResult)
        {
            return sessionResult;
        }

        if (!HasAnyEnvironmentField(edit))
        {
            return SceneCommandResult.Success;
        }

        var validation = ValidateEnvironmentEdit(context.Scene, edit);
        if (validation is not null && validation.Value.IsFailure)
        {
            return this.ValidationFailure(SceneOperationKinds.EditEnvironment, validation.Value.Code, validation.Value.Title, validation.Value.Message, context);
        }

        var before = context.Scene.Environment;
        var beforeSunStates = CaptureDirectionalSunStates(context.Scene);
        var after = ApplyEnvironmentEdit(before, edit);
        context.Scene.SetEnvironment(after);
        ApplyEnvironmentSunBinding(context.Scene, after.SunNodeId);
        var afterSunStates = CaptureDirectionalSunStates(context.Scene);
        this.RecordEnvironmentHistory(context, before, after, beforeSunStates, afterSunStates);
        var operationResultId = await this.PublishEnvironmentSyncAsync(context, after).ConfigureAwait(true);
        if (validation is not null)
        {
            operationResultId ??= this.PublishSceneWarning(
                SceneOperationKinds.EditEnvironment,
                validation.Value.Code,
                validation.Value.Title,
                validation.Value.Message,
                context);
        }

        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        return new SceneCommandResult(true, operationResultId);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> SaveSceneAsync(SceneDocumentCommandContext context)
    {
        try
        {
            var success = await this.projectManager.SaveSceneAsync(context.Scene).ConfigureAwait(true);
            if (!success)
            {
                var operationResultId = this.PublishSceneFailure(
                    SceneOperationKinds.Save,
                    DiagnosticCodes.DocumentPrefix + "SAVE_FAILED",
                    "Scene was not saved",
                    "The scene data could not be saved.",
                    context,
                    domain: FailureDomain.Document);
                return new SceneCommandResult(false, operationResultId);
            }

            context.Metadata.IsDirty = false;
            _ = await this.documentService.UpdateMetadataAsync(this.windowId, context.DocumentId, context.Metadata).ConfigureAwait(true);
            _ = this.messenger.Send(new AssetsChangedMessage());
            return SceneCommandResult.Success;
        }
        catch (Exception ex)
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.Save,
                DiagnosticCodes.DocumentPrefix + "SAVE_EXCEPTION",
                "Scene was not saved",
                "The scene save operation failed.",
                context,
                ex,
                FailureDomain.Document);
            return new SceneCommandResult(false, operationResultId);
        }
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> RenameItemAsync(
        SceneDocumentCommandContext context,
        ITreeItem item,
        string newName)
    {
        var oldName = item.Label;
        if (string.Equals(oldName, newName, StringComparison.Ordinal))
        {
            return SceneCommandResult.Success;
        }

        if (string.IsNullOrWhiteSpace(newName))
        {
            var operationResultId = this.PublishSceneFailure(
                item is FolderAdapter ? SceneOperationKinds.ExplorerFolderRename : SceneOperationKinds.NodeRename,
                DiagnosticCodes.ScenePrefix + "INVALID_NAME",
                "Item was not renamed",
                "Scene item names cannot be empty.",
                context);
            return new SceneCommandResult(false, operationResultId);
        }

        try
        {
            await this.sceneExplorerService.RenameItemAsync(item, newName).ConfigureAwait(false);
            context.History.AddChange(
                $"Rename({oldName} -> {newName})",
                async () => await this.RenameItemAsync(context, item, oldName).ConfigureAwait(false));
            await this.MarkDirtyAsync(context).ConfigureAwait(true);
            return SceneCommandResult.Success;
        }
        catch (Exception ex)
        {
            var operationResultId = this.PublishSceneFailure(
                item is FolderAdapter ? SceneOperationKinds.ExplorerFolderRename : SceneOperationKinds.NodeRename,
                DiagnosticCodes.ScenePrefix + "RENAME_FAILED",
                "Item was not renamed",
                $"The item '{oldName}' could not be renamed.",
                context,
                ex);
            return new SceneCommandResult(false, operationResultId);
        }
    }

    private static string NormalizePrimitiveKind(string kind)
        => kind.Trim() switch
        {
            "Sphere" => "Sphere",
            "Cube" => "Cube",
            "Cylinder" => "Cylinder",
            "Cone" => "Cone",
            "Plane" => "Plane",
            _ => throw new NotSupportedException($"Primitive kind '{kind}' is not supported."),
        };

    private static string NormalizeLightKind(string kind)
        => kind.Trim() switch
        {
            "Directional" => "Directional",
            "Point" => "Point",
            "Spot" => "Spot",
            _ => throw new NotSupportedException($"Light kind '{kind}' is not supported."),
        };

    private static void ApplyLightTransform(SceneNode node, string kind)
    {
        if (node.Components.OfType<TransformComponent>().FirstOrDefault() is not { } transform)
        {
            return;
        }

        transform.LocalPosition = kind switch
        {
            "Point" => new System.Numerics.Vector3(0f, 2f, 0f),
            "Spot" => new System.Numerics.Vector3(0f, 3f, 3f),
            _ => System.Numerics.Vector3.Zero,
        };
        transform.LocalRotation = kind switch
        {
            "Directional" => DirectionalLightComponent.DefaultLocalRotation,
            "Spot" => System.Numerics.Quaternion.CreateFromYawPitchRoll(0f, -0.7853982f, 0f),
            _ => System.Numerics.Quaternion.Identity,
        };
    }

    private static LightComponent CreateLightComponent(string kind)
        => kind switch
        {
            "Directional" => new DirectionalLightComponent { Name = "Directional Light" },
            "Point" => new PointLightComponent
            {
                Name = "Point Light",
                LuminousFluxLumens = 1_600f,
                Range = 10f,
            },
            "Spot" => new SpotLightComponent
            {
                Name = "Spot Light",
                LuminousFluxLumens = 1_600f,
                Range = 15f,
            },
            _ => throw new NotSupportedException($"Light kind '{kind}' is not supported."),
        };

    private async Task AddRootNodeAsync(
        SceneDocumentCommandContext context,
        SceneNode node,
        string operationKind,
        string undoLabel)
    {
        context.Scene.RootNodes.Add(node);
        context.History.AddChange($"Remove {undoLabel}", async () => await this.RemoveRootNodeForUndoAsync(context, node, operationKind).ConfigureAwait(true));
        await this.TrySyncCreateAsync(context, node, operationKind).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        this.PublishNodeAdded(context, node);
    }

    private async Task RemoveRootNodeForUndoAsync(SceneDocumentCommandContext context, SceneNode node, string operationKind)
    {
        _ = context.Scene.RootNodes.Remove(node);
        context.History.AddChange($"Restore {node.Name}", async () => await this.RestoreRootNodeForRedoAsync(context, node, operationKind).ConfigureAwait(true));
        await this.TrySyncRemoveAsync(context, node, operationKind).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        this.PublishNodeRemoved(context, node);
    }

    private async Task RestoreRootNodeForRedoAsync(SceneDocumentCommandContext context, SceneNode node, string operationKind)
    {
        if (!context.Scene.RootNodes.Contains(node))
        {
            context.Scene.RootNodes.Add(node);
        }

        context.History.AddChange($"Remove {node.Name}", async () => await this.RemoveRootNodeForUndoAsync(context, node, operationKind).ConfigureAwait(true));
        await this.TrySyncCreateAsync(context, node, operationKind).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        this.PublishNodeAdded(context, node);
    }

    private async Task TrySyncCreateAsync(SceneDocumentCommandContext context, SceneNode node, string operationKind)
    {
        try
        {
            await this.sceneEngineSync.CreateNodeAsync(node, parentGuid: null).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            this.PublishLiveSyncWarning(operationKind, DiagnosticCodes.LiveSyncPrefix + "CREATE_NODE_FAILED", "Scene was updated but live preview was not", context, node, ex);
        }
    }

    private async Task TrySyncRemoveAsync(SceneDocumentCommandContext context, SceneNode node, string operationKind)
    {
        try
        {
            await this.sceneEngineSync.RemoveNodeHierarchyAsync(node.Id).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            this.PublishLiveSyncWarning(operationKind, DiagnosticCodes.LiveSyncPrefix + "REMOVE_NODE_FAILED", "Scene was updated but live preview was not", context, node, ex);
        }
    }

    private async Task MarkDirtyAsync(SceneDocumentCommandContext context)
    {
        if (context.Metadata.IsDirty)
        {
            return;
        }

        context.Metadata.IsDirty = true;
        _ = await this.documentService.UpdateMetadataAsync(this.windowId, context.DocumentId, context.Metadata).ConfigureAwait(true);
    }

    private static IReadOnlyList<SceneNode> ResolveNodes(Scene scene, IReadOnlyList<Guid> nodeIds)
        => nodeIds.Select(id => FindNode(scene, id)).OfType<SceneNode>().ToList();

    private static SceneNode? FindNode(Scene scene, Guid nodeId)
    {
        foreach (var root in scene.RootNodes)
        {
            var found = SceneTraversal.FindNodeById(root, nodeId);
            if (found is not null)
            {
                return found;
            }
        }

        return null;
    }

    private static SceneCommandResult? SkipUncommittedSession(EditSessionToken session)
        => !session.IsOneShot && session.State != EditSessionState.Committed
            ? SceneCommandResult.Success
            : null;

    private static bool HasAnyTransformField(TransformEdit edit)
        => edit.Position.HasValue ||
           edit.RotationEulerDegrees.HasValue ||
           edit.Scale.HasValue ||
           edit.PositionX.HasValue ||
           edit.PositionY.HasValue ||
           edit.PositionZ.HasValue ||
           edit.RotationXDegrees.HasValue ||
           edit.RotationYDegrees.HasValue ||
           edit.RotationZDegrees.HasValue ||
           edit.ScaleX.HasValue ||
           edit.ScaleY.HasValue ||
           edit.ScaleZ.HasValue;

    private static bool HasAnyPerspectiveCameraField(PerspectiveCameraEdit edit)
        => edit.FieldOfViewDegrees.HasValue || edit.AspectRatio.HasValue || edit.NearPlane.HasValue || edit.FarPlane.HasValue;

    private static bool HasAnyDirectionalLightField(DirectionalLightEdit edit)
        => edit.Color.HasValue ||
           edit.IntensityLux.HasValue ||
           edit.IsSunLight.HasValue ||
           edit.EnvironmentContribution.HasValue ||
           edit.CastsShadows.HasValue ||
           edit.AffectsWorld.HasValue ||
           edit.AngularSizeRadians.HasValue ||
           edit.ExposureCompensation.HasValue;

    private static bool HasAnyEnvironmentField(SceneEnvironmentEdit edit)
        => edit.AtmosphereEnabled.HasValue ||
           edit.SunNodeId.HasValue ||
           edit.ExposureMode.HasValue ||
           edit.ManualExposureEv.HasValue ||
           edit.ExposureCompensation.HasValue ||
           edit.ToneMapping.HasValue ||
           edit.BackgroundColor.HasValue ||
           edit.SkyAtmosphere.HasValue ||
           edit.PostProcess.HasValue;

    private static T Get<T>(Optional<T> optional) => optional.Value!;

    private static ValidationIssue? ValidateTransformEdit(TransformEdit edit)
    {
        if ((edit.Position.HasValue && !IsFinite(Get(edit.Position))) ||
            (edit.RotationEulerDegrees.HasValue && !IsFinite(Get(edit.RotationEulerDegrees))) ||
            (edit.Scale.HasValue && !IsFinite(Get(edit.Scale))) ||
            (edit.PositionX.HasValue && !float.IsFinite(Get(edit.PositionX))) ||
            (edit.PositionY.HasValue && !float.IsFinite(Get(edit.PositionY))) ||
            (edit.PositionZ.HasValue && !float.IsFinite(Get(edit.PositionZ))) ||
            (edit.RotationXDegrees.HasValue && !float.IsFinite(Get(edit.RotationXDegrees))) ||
            (edit.RotationYDegrees.HasValue && !float.IsFinite(Get(edit.RotationYDegrees))) ||
            (edit.RotationZDegrees.HasValue && !float.IsFinite(Get(edit.RotationZDegrees))) ||
            (edit.ScaleX.HasValue && !float.IsFinite(Get(edit.ScaleX))) ||
            (edit.ScaleY.HasValue && !float.IsFinite(Get(edit.ScaleY))) ||
            (edit.ScaleZ.HasValue && !float.IsFinite(Get(edit.ScaleZ))))
        {
            return new(
                SceneDiagnosticCodes.TransformFieldNotFinite,
                "Transform was not edited",
                "Transform values must be finite numbers.",
                IsFailure: true);
        }

        if (edit.Scale.HasValue && Get(edit.Scale) is { } scale && (scale.X == 0f || scale.Y == 0f || scale.Z == 0f))
        {
            return new(
                SceneDiagnosticCodes.TransformScaleZeroAxis,
                "Transform was not edited",
                "Scale cannot contain a zero axis.",
                IsFailure: true);
        }

        if ((edit.ScaleX.HasValue && Get(edit.ScaleX) == 0f) ||
            (edit.ScaleY.HasValue && Get(edit.ScaleY) == 0f) ||
            (edit.ScaleZ.HasValue && Get(edit.ScaleZ) == 0f))
        {
            return new(
                SceneDiagnosticCodes.TransformScaleZeroAxis,
                "Transform was not edited",
                "Scale cannot contain a zero axis.",
                IsFailure: true);
        }

        return null;
    }

    private static ValidationIssue? ValidatePerspectiveCameraEdit(Scene scene, IReadOnlyList<Guid> nodeIds, PerspectiveCameraEdit edit)
    {
        if ((edit.FieldOfViewDegrees.HasValue && !float.IsFinite(Get(edit.FieldOfViewDegrees))) ||
            (edit.AspectRatio.HasValue && !float.IsFinite(Get(edit.AspectRatio))) ||
            (edit.NearPlane.HasValue && !float.IsFinite(Get(edit.NearPlane))) ||
            (edit.FarPlane.HasValue && !float.IsFinite(Get(edit.FarPlane))))
        {
            return new(SceneDiagnosticCodes.TransformFieldNotFinite, "Camera was not edited", "Camera values must be finite numbers.", IsFailure: true);
        }

        if (edit.AspectRatio.HasValue && Get(edit.AspectRatio) <= 0f)
        {
            return new(SceneDiagnosticCodes.PerspectiveCameraAspectRatioNonPositive, "Camera was not edited", "Aspect ratio must be greater than zero.", IsFailure: true);
        }

        if (edit.NearPlane.HasValue && Get(edit.NearPlane) <= 0f)
        {
            return new(SceneDiagnosticCodes.PerspectiveCameraNearPlaneNonPositive, "Camera was not edited", "Near plane must be greater than zero.", IsFailure: true);
        }

        foreach (var camera in ResolveNodes(scene, nodeIds).Select(static node => node.Components.OfType<PerspectiveCamera>().FirstOrDefault()).OfType<PerspectiveCamera>())
        {
            var near = edit.NearPlane.HasValue ? Get(edit.NearPlane) : camera.NearPlane;
            var far = edit.FarPlane.HasValue ? Get(edit.FarPlane) : camera.FarPlane;
            if (near >= far)
            {
                return new(SceneDiagnosticCodes.PerspectiveCameraNearFarInvalid, "Camera was not edited", "Near plane must be smaller than far plane.", IsFailure: true);
            }
        }

        return null;
    }

    private static ValidationIssue? ValidateDirectionalLightEdit(DirectionalLightEdit edit)
    {
        if ((edit.Color.HasValue && !IsFinite(Get(edit.Color))) ||
            (edit.IntensityLux.HasValue && !float.IsFinite(Get(edit.IntensityLux))) ||
            (edit.AngularSizeRadians.HasValue && !float.IsFinite(Get(edit.AngularSizeRadians))) ||
            (edit.ExposureCompensation.HasValue && !float.IsFinite(Get(edit.ExposureCompensation))))
        {
            return new(
                SceneDiagnosticCodes.DirectionalLightFieldNotFinite,
                "Light was not edited",
                "Directional light values must be finite numbers.",
                IsFailure: true);
        }

        return null;
    }

    private static ValidationIssue? ValidateEnvironmentEdit(Scene scene, SceneEnvironmentEdit edit)
    {
        if (edit.ExposureMode.HasValue && !Enum.IsDefined(Get(edit.ExposureMode)))
        {
            return new(SceneDiagnosticCodes.EnvironmentExposureModeInvalid, "Environment was not edited", "Exposure mode is not valid.", IsFailure: true);
        }

        if (edit.ToneMapping.HasValue && !Enum.IsDefined(Get(edit.ToneMapping)))
        {
            return new(SceneDiagnosticCodes.EnvironmentToneMappingInvalid, "Environment was not edited", "Tone mapping mode is not valid.", IsFailure: true);
        }

        if (edit.ManualExposureEv.HasValue && !float.IsFinite(Get(edit.ManualExposureEv)))
        {
            return new(SceneDiagnosticCodes.EnvironmentManualExposureInvalid, "Environment was not edited", "Manual exposure must be finite.", IsFailure: true);
        }

        if (edit.ExposureCompensation.HasValue && !float.IsFinite(Get(edit.ExposureCompensation)))
        {
            return new(SceneDiagnosticCodes.EnvironmentExposureCompensationInvalid, "Environment was not edited", "Exposure compensation must be finite.", IsFailure: true);
        }

        if (edit.BackgroundColor.HasValue && !IsFinite(Get(edit.BackgroundColor)))
        {
            return new(SceneDiagnosticCodes.EnvironmentBackgroundColorInvalid, "Environment was not edited", "Background color values must be finite.", IsFailure: true);
        }

        if (edit.SkyAtmosphere.HasValue && !IsFinite(Get(edit.SkyAtmosphere)))
        {
            return new(SceneDiagnosticCodes.EnvironmentSkyAtmosphereInvalid, "Environment was not edited", "Sky atmosphere values must be finite.", IsFailure: true);
        }

        if (edit.PostProcess.HasValue && !IsFinite(Get(edit.PostProcess)))
        {
            return new(SceneDiagnosticCodes.EnvironmentManualExposureInvalid, "Environment was not edited", "Post-process values must be finite.", IsFailure: true);
        }

        if (edit.SunNodeId.HasValue && Get(edit.SunNodeId) is { } sunNodeId)
        {
            var sunNode = FindNode(scene, sunNodeId);
            if (sunNode?.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is null)
            {
                return new(SceneDiagnosticCodes.EnvironmentSunRefStale, "Sun reference is stale", "The selected sun node does not exist or is not a directional light.", IsFailure: false);
            }
        }

        return null;
    }

    private static bool IsFinite(Vector3 value)
        => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    private static bool IsFinite(SkyAtmosphereEnvironmentData value)
        => float.IsFinite(value.PlanetRadiusMeters) &&
           float.IsFinite(value.AtmosphereHeightMeters) &&
           IsFinite(value.GroundAlbedoRgb) &&
           float.IsFinite(value.RayleighScaleHeightMeters) &&
           float.IsFinite(value.MieScaleHeightMeters) &&
           float.IsFinite(value.MieAnisotropy) &&
           IsFinite(value.SkyLuminanceFactorRgb) &&
           float.IsFinite(value.AerialPerspectiveDistanceScale) &&
           float.IsFinite(value.AerialScatteringStrength) &&
           float.IsFinite(value.AerialPerspectiveStartDepthMeters) &&
           float.IsFinite(value.HeightFogContribution);

    private static bool IsFinite(PostProcessEnvironmentData value)
        => float.IsFinite(value.ExposureCompensationEv) &&
           float.IsFinite(value.ExposureKey) &&
           float.IsFinite(value.ManualExposureEv) &&
           float.IsFinite(value.AutoExposureMinEv) &&
           float.IsFinite(value.AutoExposureMaxEv) &&
           float.IsFinite(value.AutoExposureSpeedUp) &&
           float.IsFinite(value.AutoExposureSpeedDown) &&
           float.IsFinite(value.AutoExposureLowPercentile) &&
           float.IsFinite(value.AutoExposureHighPercentile) &&
           float.IsFinite(value.AutoExposureMinLogLuminance) &&
           float.IsFinite(value.AutoExposureLogLuminanceRange) &&
           float.IsFinite(value.AutoExposureTargetLuminance) &&
           float.IsFinite(value.AutoExposureSpotMeterRadius) &&
           float.IsFinite(value.BloomIntensity) &&
           float.IsFinite(value.BloomThreshold) &&
           float.IsFinite(value.Saturation) &&
           float.IsFinite(value.Contrast) &&
           float.IsFinite(value.VignetteIntensity) &&
           float.IsFinite(value.DisplayGamma);

    private static void ApplyTransformEdit(TransformComponent transform, TransformEdit edit)
    {
        if (edit.Position.HasValue)
        {
            transform.LocalPosition = Get(edit.Position);
        }

        if (edit.PositionX.HasValue || edit.PositionY.HasValue || edit.PositionZ.HasValue)
        {
            var position = transform.LocalPosition;
            if (edit.PositionX.HasValue)
            {
                position.X = Get(edit.PositionX);
            }

            if (edit.PositionY.HasValue)
            {
                position.Y = Get(edit.PositionY);
            }

            if (edit.PositionZ.HasValue)
            {
                position.Z = Get(edit.PositionZ);
            }

            transform.LocalPosition = position;
        }

        if (edit.RotationEulerDegrees.HasValue)
        {
            transform.LocalRotation = TransformConverter.EulerDegreesToQuaternion(Get(edit.RotationEulerDegrees));
        }

        if (edit.RotationXDegrees.HasValue || edit.RotationYDegrees.HasValue || edit.RotationZDegrees.HasValue)
        {
            var rotation = TransformConverter.QuaternionToEulerDegrees(transform.LocalRotation);
            if (edit.RotationXDegrees.HasValue)
            {
                rotation.X = Get(edit.RotationXDegrees);
            }

            if (edit.RotationYDegrees.HasValue)
            {
                rotation.Y = Get(edit.RotationYDegrees);
            }

            if (edit.RotationZDegrees.HasValue)
            {
                rotation.Z = Get(edit.RotationZDegrees);
            }

            transform.LocalRotation = TransformConverter.EulerDegreesToQuaternion(rotation);
        }

        if (edit.Scale.HasValue)
        {
            transform.LocalScale = Get(edit.Scale);
        }

        if (edit.ScaleX.HasValue || edit.ScaleY.HasValue || edit.ScaleZ.HasValue)
        {
            var scale = transform.LocalScale;
            if (edit.ScaleX.HasValue)
            {
                scale.X = Get(edit.ScaleX);
            }

            if (edit.ScaleY.HasValue)
            {
                scale.Y = Get(edit.ScaleY);
            }

            if (edit.ScaleZ.HasValue)
            {
                scale.Z = Get(edit.ScaleZ);
            }

            transform.LocalScale = scale;
        }
    }

    private static void ApplyPerspectiveCameraEdit(PerspectiveCamera camera, PerspectiveCameraEdit edit)
    {
        if (edit.FieldOfViewDegrees.HasValue)
        {
            camera.FieldOfView = Math.Clamp(Get(edit.FieldOfViewDegrees), 1f, 179f);
        }

        if (edit.AspectRatio.HasValue)
        {
            camera.AspectRatio = Get(edit.AspectRatio);
        }

        if (edit.NearPlane.HasValue)
        {
            camera.NearPlane = Get(edit.NearPlane);
        }

        if (edit.FarPlane.HasValue)
        {
            camera.FarPlane = Get(edit.FarPlane);
        }
    }

    private static void ApplyDirectionalLightEdit(DirectionalLightComponent light, DirectionalLightEdit edit)
    {
        if (edit.Color.HasValue)
        {
            light.Color = Vector3.Clamp(Get(edit.Color), Vector3.Zero, Vector3.One);
        }

        if (edit.IntensityLux.HasValue)
        {
            light.IntensityLux = Math.Max(0f, Get(edit.IntensityLux));
        }

        if (edit.IsSunLight.HasValue)
        {
            light.IsSunLight = Get(edit.IsSunLight);
        }

        if (edit.EnvironmentContribution.HasValue)
        {
            light.EnvironmentContribution = Get(edit.EnvironmentContribution);
        }

        if (edit.CastsShadows.HasValue)
        {
            light.CastsShadows = Get(edit.CastsShadows);
        }

        if (edit.AffectsWorld.HasValue)
        {
            light.AffectsWorld = Get(edit.AffectsWorld);
        }

        if (edit.AngularSizeRadians.HasValue)
        {
            light.AngularSizeRadians = Math.Max(0f, Get(edit.AngularSizeRadians));
        }

        if (edit.ExposureCompensation.HasValue)
        {
            light.ExposureCompensation = Math.Clamp(Get(edit.ExposureCompensation), -10f, 10f);
        }
    }

    private static SceneEnvironmentData ApplyEnvironmentEdit(SceneEnvironmentData current, SceneEnvironmentEdit edit)
    {
        var postProcess = edit.PostProcess.HasValue
            ? SanitizePostProcess(Get(edit.PostProcess))
            : current.PostProcess ?? new();

        if (edit.ExposureMode.HasValue)
        {
            postProcess = postProcess with { ExposureMode = Get(edit.ExposureMode) };
        }

        if (edit.ManualExposureEv.HasValue)
        {
            postProcess = postProcess with { ManualExposureEv = Math.Clamp(Get(edit.ManualExposureEv), -24f, 24f) };
        }

        if (edit.ExposureCompensation.HasValue)
        {
            postProcess = postProcess with { ExposureCompensationEv = Get(edit.ExposureCompensation) };
        }

        if (edit.ToneMapping.HasValue)
        {
            postProcess = postProcess with { ToneMapper = Get(edit.ToneMapping) };
        }

        return current with
        {
            AtmosphereEnabled = edit.AtmosphereEnabled.HasValue ? Get(edit.AtmosphereEnabled) : current.AtmosphereEnabled,
            SunNodeId = edit.SunNodeId.HasValue ? Get(edit.SunNodeId) : current.SunNodeId,
            ExposureMode = postProcess.ExposureMode,
            ManualExposureEv = postProcess.ManualExposureEv,
            ExposureCompensation = postProcess.ExposureCompensationEv,
            ToneMapping = postProcess.ToneMapper,
            PostProcess = postProcess,
            BackgroundColor = edit.BackgroundColor.HasValue ? Get(edit.BackgroundColor) : current.BackgroundColor,
            SkyAtmosphere = edit.SkyAtmosphere.HasValue ? SanitizeSkyAtmosphere(Get(edit.SkyAtmosphere)) : current.SkyAtmosphere ?? new(),
        };
    }

    private static SkyAtmosphereEnvironmentData SanitizeSkyAtmosphere(SkyAtmosphereEnvironmentData value)
        => value with
        {
            PlanetRadiusMeters = Math.Max(1.0f, value.PlanetRadiusMeters),
            AtmosphereHeightMeters = Math.Max(1.0f, value.AtmosphereHeightMeters),
            GroundAlbedoRgb = Vector3.Clamp(value.GroundAlbedoRgb, Vector3.Zero, Vector3.One),
            RayleighScaleHeightMeters = Math.Max(1.0f, value.RayleighScaleHeightMeters),
            MieScaleHeightMeters = Math.Max(1.0f, value.MieScaleHeightMeters),
            MieAnisotropy = Math.Clamp(value.MieAnisotropy, -0.999f, 0.999f),
            SkyLuminanceFactorRgb = Vector3.Max(value.SkyLuminanceFactorRgb, Vector3.Zero),
            AerialPerspectiveDistanceScale = Math.Max(0.0f, value.AerialPerspectiveDistanceScale),
            AerialScatteringStrength = Math.Max(0.0f, value.AerialScatteringStrength),
            AerialPerspectiveStartDepthMeters = Math.Max(0.0f, value.AerialPerspectiveStartDepthMeters),
            HeightFogContribution = Math.Max(0.0f, value.HeightFogContribution),
        };

    private static PostProcessEnvironmentData SanitizePostProcess(PostProcessEnvironmentData value)
        => value with
        {
            ExposureKey = Math.Max(0.001f, value.ExposureKey),
            ManualExposureEv = Math.Clamp(value.ManualExposureEv, -24f, 24f),
            AutoExposureMinEv = Math.Min(value.AutoExposureMinEv, value.AutoExposureMaxEv),
            AutoExposureMaxEv = Math.Max(value.AutoExposureMaxEv, value.AutoExposureMinEv),
            AutoExposureSpeedUp = Math.Max(0.0f, value.AutoExposureSpeedUp),
            AutoExposureSpeedDown = Math.Max(0.0f, value.AutoExposureSpeedDown),
            AutoExposureLowPercentile = Math.Clamp(value.AutoExposureLowPercentile, 0.0f, 1.0f),
            AutoExposureHighPercentile = Math.Clamp(value.AutoExposureHighPercentile, 0.0f, 1.0f),
            AutoExposureLogLuminanceRange = Math.Max(0.001f, value.AutoExposureLogLuminanceRange),
            AutoExposureTargetLuminance = Math.Max(0.0f, value.AutoExposureTargetLuminance),
            AutoExposureSpotMeterRadius = Math.Max(0.0f, value.AutoExposureSpotMeterRadius),
            BloomIntensity = Math.Max(0.0f, value.BloomIntensity),
            BloomThreshold = Math.Max(0.0f, value.BloomThreshold),
            Saturation = Math.Max(0.0f, value.Saturation),
            Contrast = Math.Max(0.0f, value.Contrast),
            VignetteIntensity = Math.Clamp(value.VignetteIntensity, 0.0f, 1.0f),
            DisplayGamma = Math.Max(0.001f, value.DisplayGamma),
        };

    private static void ApplyMaterialSlotEdit(GeometryComponent geometry, Uri? materialUri)
    {
        var slot = geometry.OverrideSlots.OfType<MaterialsSlot>().FirstOrDefault();
        if (slot is null)
        {
            slot = new MaterialsSlot();
            geometry.OverrideSlots.Add(slot);
        }

        slot.Material = new AssetReference<MaterialAsset>(materialUri ?? EmptyMaterialUri);
    }

    private static void ApplyExclusiveSun(Scene scene, Guid sunNodeId)
    {
        foreach (var state in CaptureDirectionalSunStates(scene))
        {
            state.Light.IsSunLight = state.Node.Id == sunNodeId;
        }
    }

    private static void ApplyEnvironmentSunBinding(Scene scene, Guid? sunNodeId)
    {
        if (sunNodeId is null)
        {
            foreach (var state in CaptureDirectionalSunStates(scene))
            {
                state.Light.IsSunLight = false;
            }

            return;
        }

        if (FindNode(scene, sunNodeId.Value)?.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is not null)
        {
            ApplyExclusiveSun(scene, sunNodeId.Value);
        }
    }

    private static IReadOnlyList<DirectionalSunState> CaptureDirectionalSunStates(Scene scene)
        => scene.RootNodes
            .SelectMany(static root => SceneTraversal.CollectNodes(root))
            .Select(static node => new { Node = node, Light = node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() })
            .Where(static item => item.Light is not null)
            .Select(static item => new DirectionalSunState(item.Node, item.Light!, item.Light!.IsSunLight))
            .ToList();

    private static void ApplySunStates(IReadOnlyList<DirectionalSunState> states)
    {
        foreach (var state in states)
        {
            state.Light.IsSunLight = state.IsSunLight;
        }
    }

    private static IReadOnlyList<SceneNode> IncludeDirectionalSunChangedNodes(
        IEnumerable<SceneNode> primaryNodes,
        IReadOnlyList<DirectionalSunState> before,
        IReadOnlyList<DirectionalSunState> after)
    {
        var nodesById = new Dictionary<Guid, SceneNode>();
        foreach (var node in primaryNodes)
        {
            nodesById[node.Id] = node;
        }

        foreach (var afterState in after)
        {
            var beforeState = before.FirstOrDefault(state => state.Node.Id == afterState.Node.Id);
            if (beforeState is not null && beforeState.IsSunLight != afterState.IsSunLight)
            {
                nodesById[afterState.Node.Id] = afterState.Node;
            }
        }

        return nodesById.Values.ToList();
    }

    private async Task<SceneCommandResult> EditTransformSessionAsync(
        SceneDocumentCommandContext context,
        EditSessionToken session,
        IReadOnlyList<SceneNode> nodes,
        IReadOnlyList<TransformComponent> transforms,
        TransformEdit edit)
    {
        if (session.State == EditSessionState.Cancelled)
        {
            return await this.CancelTransformEditSessionAsync(context, session).ConfigureAwait(true);
        }

        var before = this.transformEditSessions.TryGetValue(session.SessionId, out var existing)
            ? existing.Before
            : nodes.Zip(transforms, static (node, transform) => TransformState.Capture(node, transform)).ToList();

        foreach (var transform in transforms)
        {
            ApplyTransformEdit(transform, edit);
        }

        var after = nodes.Zip(transforms, static (node, transform) => TransformState.Capture(node, transform)).ToList();
        if (session.State == EditSessionState.Open)
        {
            this.transformEditSessions[session.SessionId] = new TransformEditSessionState(session, before, after);
            await this.PreviewTransformSessionAsync(context, nodes).ConfigureAwait(true);
            return SceneCommandResult.Success;
        }

        _ = this.transformEditSessions.Remove(session.SessionId);
        if (TransformStatesEqual(before, after))
        {
            var noOpOperationResultId = await this.SyncTerminalTransformSessionAsync(context, nodes).ConfigureAwait(true);
            return new SceneCommandResult(true, noOpOperationResultId);
        }

        this.RecordTransformHistory(context, before, after);
        var operationResultId = await this.SyncTerminalTransformSessionAsync(context, nodes).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        return new SceneCommandResult(true, operationResultId);
    }

    private async Task<SceneCommandResult> CancelTransformEditSessionAsync(
        SceneDocumentCommandContext context,
        EditSessionToken session)
    {
        if (!this.transformEditSessions.Remove(session.SessionId, out var state))
        {
            return SceneCommandResult.Success;
        }

        foreach (var before in state.Before)
        {
            before.Apply();
        }

        foreach (var before in state.Before)
        {
            _ = await this.sceneEngineSync.CancelPreviewSyncAsync(
                context.Scene.Id,
                before.Node.Id,
                _ => this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, before.Node)).ConfigureAwait(true);
        }

        return SceneCommandResult.Success;
    }

    private async Task PreviewTransformSessionAsync(SceneDocumentCommandContext context, IReadOnlyList<SceneNode> nodes)
    {
        var observedAt = DateTimeOffset.UtcNow;
        foreach (var node in nodes)
        {
            _ = await this.sceneEngineSync.TryPreviewSyncAsync(
                context.Scene.Id,
                node.Id,
                observedAt,
                _ => this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, node)).ConfigureAwait(true);
        }
    }

    private async Task<Guid?> SyncTerminalTransformSessionAsync(SceneDocumentCommandContext context, IReadOnlyList<SceneNode> nodes)
    {
        Guid? firstOperationResultId = null;
        foreach (var node in nodes)
        {
            var outcome = await this.sceneEngineSync.CompleteTerminalSyncAsync(
                context.Scene.Id,
                node.Id,
                _ => this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, node)).ConfigureAwait(true);
            firstOperationResultId ??= await this.PublishSyncOutcomeAsync(context, SceneOperationKinds.EditTransform, outcome).ConfigureAwait(true);
        }

        return firstOperationResultId;
    }

    private static bool TransformStatesEqual(IReadOnlyList<TransformState> before, IReadOnlyList<TransformState> after)
    {
        if (before.Count != after.Count)
        {
            return false;
        }

        for (var i = 0; i < before.Count; i++)
        {
            if (before[i].Node.Id != after[i].Node.Id ||
                before[i].Position != after[i].Position ||
                before[i].Rotation != after[i].Rotation ||
                before[i].Scale != after[i].Scale)
            {
                return false;
            }
        }

        return true;
    }

    private static bool GeometryStatesEqual(IReadOnlyList<GeometryState> before, IReadOnlyList<GeometryState> after)
    {
        if (before.Count != after.Count)
        {
            return false;
        }

        for (var i = 0; i < before.Count; i++)
        {
            if (before[i].Node.Id != after[i].Node.Id ||
                !UriValuesEqual(before[i].GeometryUri, after[i].GeometryUri))
            {
                return false;
            }
        }

        return true;
    }

    private static bool MaterialSlotStatesEqual(IReadOnlyList<MaterialSlotState> before, IReadOnlyList<MaterialSlotState> after)
    {
        if (before.Count != after.Count)
        {
            return false;
        }

        for (var i = 0; i < before.Count; i++)
        {
            if (before[i].Node.Id != after[i].Node.Id ||
                before[i].HasSlot != after[i].HasSlot ||
                !UriValuesEqual(before[i].MaterialUri, after[i].MaterialUri))
            {
                return false;
            }
        }

        return true;
    }

    private static bool UriValuesEqual(Uri? left, Uri? right)
        => left == right || (left is not null && right is not null && string.Equals(left.ToString(), right.ToString(), StringComparison.Ordinal));

    private static bool IsEmptyMaterialUri(Uri? uri)
        => UriValuesEqual(uri, EmptyMaterialUri);

    private static Uri? ToMaterialSyncUri(Uri? uri)
        => IsEmptyMaterialUri(uri) ? null : uri;

    private void RecordTransformHistory(SceneDocumentCommandContext context, IReadOnlyList<TransformState> before, IReadOnlyList<TransformState> after)
        => context.History.AddChange("Restore Transform", async () => await this.ApplyTransformStatesForHistoryAsync(context, before, after).ConfigureAwait(true));

    private async Task ApplyTransformStatesForHistoryAsync(SceneDocumentCommandContext context, IReadOnlyList<TransformState> states, IReadOnlyList<TransformState> inverse)
    {
        foreach (var state in states)
        {
            state.Apply();
        }

        context.History.AddChange("Reapply Transform", async () => await this.ApplyTransformStatesForHistoryAsync(context, inverse, states).ConfigureAwait(true));
        _ = await this.SyncEditedNodesAsync(context, states.Select(static state => state.Node).ToList(), SceneOperationKinds.EditTransform, node => this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, node)).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
    }

    private void RecordGeometryHistory(SceneDocumentCommandContext context, IReadOnlyList<GeometryState> before, IReadOnlyList<GeometryState> after)
        => context.History.AddChange("Restore Geometry", async () => await this.ApplyGeometryStatesForHistoryAsync(context, before, after).ConfigureAwait(true));

    private async Task ApplyGeometryStatesForHistoryAsync(SceneDocumentCommandContext context, IReadOnlyList<GeometryState> states, IReadOnlyList<GeometryState> inverse)
    {
        foreach (var state in states)
        {
            state.Apply();
        }

        context.History.AddChange("Reapply Geometry", async () => await this.ApplyGeometryStatesForHistoryAsync(context, inverse, states).ConfigureAwait(true));
        _ = await this.SyncGeometryStatesAsync(context, states, SceneOperationKinds.EditGeometry).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
    }

    private void RecordMaterialSlotHistory(SceneDocumentCommandContext context, IReadOnlyList<MaterialSlotState> before, IReadOnlyList<MaterialSlotState> after)
        => context.History.AddChange("Restore Material Slot", async () => await this.ApplyMaterialSlotStatesForHistoryAsync(context, before, after).ConfigureAwait(true));

    private async Task ApplyMaterialSlotStatesForHistoryAsync(SceneDocumentCommandContext context, IReadOnlyList<MaterialSlotState> states, IReadOnlyList<MaterialSlotState> inverse)
    {
        foreach (var state in states)
        {
            state.Apply();
        }

        context.History.AddChange("Reapply Material Slot", async () => await this.ApplyMaterialSlotStatesForHistoryAsync(context, inverse, states).ConfigureAwait(true));
        _ = await this.SyncEditedNodesAsync(context, states.Select(static state => state.Node).ToList(), SceneOperationKinds.EditMaterialSlot, node => this.sceneEngineSync.UpdateMaterialSlotAsync(context.Scene, node, 0, ToMaterialSyncUri(states.First(state => state.Node == node).MaterialUri))).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
    }

    private void RecordCameraHistory(SceneDocumentCommandContext context, IReadOnlyList<CameraState> before, IReadOnlyList<CameraState> after)
        => context.History.AddChange("Restore Camera", async () => await this.ApplyCameraStatesForHistoryAsync(context, before, after).ConfigureAwait(true));

    private async Task ApplyCameraStatesForHistoryAsync(SceneDocumentCommandContext context, IReadOnlyList<CameraState> states, IReadOnlyList<CameraState> inverse)
    {
        foreach (var state in states)
        {
            state.Apply();
        }

        context.History.AddChange("Reapply Camera", async () => await this.ApplyCameraStatesForHistoryAsync(context, inverse, states).ConfigureAwait(true));
        _ = await this.SyncEditedNodesAsync(context, states.Select(static state => state.Node).ToList(), SceneOperationKinds.EditPerspectiveCamera, node => this.sceneEngineSync.AttachCameraAsync(context.Scene, node)).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
    }

    private void RecordDirectionalLightHistory(
        SceneDocumentCommandContext context,
        IReadOnlyList<DirectionalLightState> before,
        IReadOnlyList<DirectionalLightState> after,
        IReadOnlyList<DirectionalSunState> beforeSunStates,
        IReadOnlyList<DirectionalSunState> afterSunStates)
        => context.History.AddChange("Restore Directional Light", async () => await this.ApplyDirectionalLightStatesForHistoryAsync(context, before, after, beforeSunStates, afterSunStates).ConfigureAwait(true));

    private async Task ApplyDirectionalLightStatesForHistoryAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<DirectionalLightState> states,
        IReadOnlyList<DirectionalLightState> inverse,
        IReadOnlyList<DirectionalSunState> sunStates,
        IReadOnlyList<DirectionalSunState> inverseSunStates)
    {
        foreach (var state in states)
        {
            state.Apply();
        }

        ApplySunStates(sunStates);
        context.History.AddChange("Reapply Directional Light", async () => await this.ApplyDirectionalLightStatesForHistoryAsync(context, inverse, states, inverseSunStates, sunStates).ConfigureAwait(true));
        var syncNodes = IncludeDirectionalSunChangedNodes(states.Select(static state => state.Node), inverseSunStates, sunStates);
        _ = await this.SyncEditedNodesAsync(context, syncNodes, SceneOperationKinds.EditDirectionalLight, node => this.sceneEngineSync.AttachLightAsync(context.Scene, node)).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
    }

    private void RecordEnvironmentHistory(
        SceneDocumentCommandContext context,
        SceneEnvironmentData before,
        SceneEnvironmentData after,
        IReadOnlyList<DirectionalSunState> beforeSunStates,
        IReadOnlyList<DirectionalSunState> afterSunStates)
        => context.History.AddChange("Restore Environment", async () => await this.ApplyEnvironmentForHistoryAsync(context, before, after, beforeSunStates, afterSunStates).ConfigureAwait(true));

    private async Task ApplyEnvironmentForHistoryAsync(
        SceneDocumentCommandContext context,
        SceneEnvironmentData environment,
        SceneEnvironmentData inverse,
        IReadOnlyList<DirectionalSunState> sunStates,
        IReadOnlyList<DirectionalSunState> inverseSunStates)
    {
        context.Scene.SetEnvironment(environment);
        ApplySunStates(sunStates);
        context.History.AddChange("Reapply Environment", async () => await this.ApplyEnvironmentForHistoryAsync(context, inverse, environment, inverseSunStates, sunStates).ConfigureAwait(true));
        _ = await this.PublishEnvironmentSyncAsync(context, environment).ConfigureAwait(true);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
    }

    private static bool CanAddComponent(SceneNode node, Type componentType, out string reason)
    {
        if (componentType == typeof(TransformComponent))
        {
            reason = "Transform is locked and already present on every scene node.";
            return false;
        }

        if (componentType == typeof(GeometryComponent))
        {
            reason = "This node already has a geometry component.";
            return !node.Components.OfType<GeometryComponent>().Any();
        }

        if (componentType == typeof(PerspectiveCamera) ||
            componentType == typeof(OrthographicCamera))
        {
            reason = "This node already has a camera component.";
            return !node.Components.OfType<CameraComponent>().Any();
        }

        if (componentType == typeof(DirectionalLightComponent) ||
            componentType == typeof(PointLightComponent) ||
            componentType == typeof(SpotLightComponent))
        {
            reason = "This node already has a light component.";
            return !node.Components.OfType<LightComponent>().Any();
        }

        reason = $"Component type '{componentType.Name}' is not supported by ED-M04.";
        return false;
    }

    private static GameComponent CreateComponent(Type componentType)
    {
        if (componentType == typeof(GeometryComponent))
        {
            return new GeometryComponent
            {
                Name = "Geometry",
                Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
            };
        }

        if (componentType == typeof(PerspectiveCamera))
        {
            return new PerspectiveCamera { Name = "Perspective Camera" };
        }

        if (componentType == typeof(OrthographicCamera))
        {
            return new OrthographicCamera { Name = "Orthographic Camera" };
        }

        if (componentType == typeof(DirectionalLightComponent))
        {
            return new DirectionalLightComponent { Name = "Directional Light", IsSunLight = false };
        }

        if (componentType == typeof(PointLightComponent))
        {
            return new PointLightComponent
            {
                Name = "Point Light",
                LuminousFluxLumens = 1_600f,
                Range = 10f,
            };
        }

        if (componentType == typeof(SpotLightComponent))
        {
            return new SpotLightComponent
            {
                Name = "Spot Light",
                LuminousFluxLumens = 1_600f,
                Range = 15f,
            };
        }

        throw new NotSupportedException($"Component type '{componentType.Name}' is not supported.");
    }

    private static TransformState? CaptureDirectionalLightDefaultTransformBefore(SceneNode node, GameComponent component)
    {
        if (component is not DirectionalLightComponent)
        {
            return null;
        }

        var transform = node.Components.OfType<TransformComponent>().FirstOrDefault();
        return transform is not null && transform.LocalRotation == Quaternion.Identity
            ? TransformState.Capture(node, transform)
            : null;
    }

    private async Task<Guid?> SyncComponentAddAsync(SceneDocumentCommandContext context, SceneNode node, GameComponent component)
    {
        var outcome = component switch
        {
            GeometryComponent => await this.sceneEngineSync.AttachGeometryAsync(context.Scene, node).ConfigureAwait(true),
            CameraComponent => await this.sceneEngineSync.AttachCameraAsync(context.Scene, node).ConfigureAwait(true),
            LightComponent => await this.sceneEngineSync.AttachLightAsync(context.Scene, node).ConfigureAwait(true),
            _ => null,
        };

        return outcome is null
            ? null
            : await this.PublishSyncOutcomeAsync(context, SceneOperationKinds.AddComponent, outcome).ConfigureAwait(true);
    }

    private async Task<Guid?> SyncComponentRemoveAsync(SceneDocumentCommandContext context, SceneNode node, GameComponent component)
    {
        var outcome = component switch
        {
            GeometryComponent => await this.sceneEngineSync.DetachGeometryAsync(context.Scene, node.Id).ConfigureAwait(true),
            CameraComponent => await this.sceneEngineSync.DetachCameraAsync(context.Scene, node.Id).ConfigureAwait(true),
            LightComponent => await this.sceneEngineSync.DetachLightAsync(context.Scene, node.Id).ConfigureAwait(true),
            _ => null,
        };

        return outcome is null
            ? null
            : await this.PublishSyncOutcomeAsync(context, SceneOperationKinds.RemoveComponent, outcome).ConfigureAwait(true);
    }

    private async Task RemoveComponentForUndoAsync(
        SceneDocumentCommandContext context,
        SceneNode node,
        GameComponent component,
        TransformState? defaultTransformBefore,
        TransformState? defaultTransformAfter)
    {
        _ = node.RemoveComponent(component);
        defaultTransformBefore?.Apply();
        context.History.AddChange(
            $"Restore Component ({component.Name})",
            async () => await this.AddComponentForRedoAsync(context, node, component, defaultTransformBefore, defaultTransformAfter).ConfigureAwait(true));
        _ = await this.SyncComponentRemoveAsync(context, node, component).ConfigureAwait(true);
        if (defaultTransformBefore is not null)
        {
            _ = await this.SyncEditedNodesAsync(context, [node], SceneOperationKinds.EditTransform, syncNode => this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, syncNode)).ConfigureAwait(true);
        }

        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        _ = this.messenger.Send(new ComponentRemovedMessage(node, component, removed: true));
    }

    private async Task AddComponentForRedoAsync(
        SceneDocumentCommandContext context,
        SceneNode node,
        GameComponent component,
        TransformState? defaultTransformBefore,
        TransformState? defaultTransformAfter)
    {
        if (!node.Components.Contains(component))
        {
            _ = node.AddComponent(component);
        }

        defaultTransformAfter?.Apply();
        context.History.AddChange(
            $"Remove Component ({component.Name})",
            async () => await this.RemoveComponentForUndoAsync(context, node, component, defaultTransformBefore, defaultTransformAfter).ConfigureAwait(true));
        _ = await this.SyncComponentAddAsync(context, node, component).ConfigureAwait(true);
        if (defaultTransformAfter is not null)
        {
            _ = await this.SyncEditedNodesAsync(context, [node], SceneOperationKinds.EditTransform, syncNode => this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, syncNode)).ConfigureAwait(true);
        }

        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        _ = this.messenger.Send(new ComponentAddedMessage(node, component, added: true));
    }

    private async Task<Guid?> SyncEditedNodesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<SceneNode> nodes,
        string operationKind,
        Func<SceneNode, Task<SyncOutcome>> sync)
    {
        Guid? firstOperationResultId = null;
        foreach (var node in nodes)
        {
            var outcome = await sync(node).ConfigureAwait(true);
            firstOperationResultId ??= await this.PublishSyncOutcomeAsync(context, operationKind, outcome).ConfigureAwait(true);
        }

        return firstOperationResultId;
    }

    private async Task<Guid?> SyncGeometryStatesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<GeometryState> states,
        string operationKind)
    {
        Guid? firstOperationResultId = null;
        foreach (var state in states)
        {
            var outcome = state.GeometryUri is null
                ? await this.sceneEngineSync.DetachGeometryAsync(context.Scene, state.Node.Id).ConfigureAwait(true)
                : await this.sceneEngineSync.AttachGeometryAsync(context.Scene, state.Node).ConfigureAwait(true);
            firstOperationResultId ??= await this.PublishSyncOutcomeAsync(context, operationKind, outcome).ConfigureAwait(true);
        }

        return firstOperationResultId;
    }

    private async Task<Guid?> PublishEnvironmentSyncAsync(SceneDocumentCommandContext context, SceneEnvironmentData environment)
    {
        var result = await this.sceneEngineSync.UpdateEnvironmentAsync(context.Scene, environment).ConfigureAwait(true);
        if (result.Overall == SyncStatus.Accepted)
        {
            return null;
        }

        var outcome = new SyncOutcome(
            result.Overall,
            SceneOperationKinds.EditEnvironment,
            Scope(context),
            result.Overall == SyncStatus.Unsupported ? LiveSyncDiagnosticCodes.EnvironmentAtmosphereUnsupported : LiveSyncDiagnosticCodes.EnvironmentRejected,
            "The scene environment was authored, but one or more live preview environment fields are not synchronized.");
        return await this.PublishSyncOutcomeAsync(context, SceneOperationKinds.EditEnvironment, outcome).ConfigureAwait(true);
    }

    private Task<Guid?> PublishSyncOutcomeAsync(SceneDocumentCommandContext context, string operationKind, SyncOutcome outcome)
    {
        if (outcome.Status == SyncStatus.Accepted)
        {
            return Task.FromResult<Guid?>(null);
        }

        var severity = outcome.Status switch
        {
            SyncStatus.Failed => DiagnosticSeverity.Error,
            SyncStatus.Cancelled => DiagnosticSeverity.Info,
            _ => DiagnosticSeverity.Warning,
        };
        var status = outcome.Status switch
        {
            SyncStatus.Cancelled => OperationStatus.Cancelled,
            SyncStatus.Rejected or SyncStatus.Failed => OperationStatus.PartiallySucceeded,
            _ => OperationStatus.SucceededWithWarnings,
        };
        var completedAt = DateTimeOffset.Now;
        var operationId = Guid.NewGuid();
        var scope = outcome.Scope with
        {
            DocumentId = context.DocumentId,
            DocumentName = context.Metadata.Title,
            SceneId = outcome.Scope.SceneId ?? context.Scene.Id,
            SceneName = outcome.Scope.SceneName ?? context.Scene.Name,
        };
        var diagnostic = new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = FailureDomain.LiveSync,
            Severity = severity,
            Code = outcome.Code ?? DiagnosticCodes.LiveSyncPrefix + "Unknown",
            Message = outcome.Message ?? "Live sync did not fully apply the authored edit.",
            TechnicalMessage = outcome.Exception?.Message,
            ExceptionType = outcome.Exception?.GetType().FullName,
            AffectedEntity = scope,
        };
        var diagnostics = new[] { diagnostic };
        this.operationResults.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = operationKind,
            Status = status,
            Severity = this.statusReducer.ComputeSeverity(diagnostics),
            Title = "Scene was updated but live preview was not",
            Message = diagnostic.Message,
            StartedAt = completedAt,
            CompletedAt = completedAt,
            AffectedScope = scope,
            Diagnostics = diagnostics,
            PrimaryAction = new PrimaryAction
            {
                ActionId = "open-details",
                Label = "Details",
                Kind = PrimaryActionKind.OpenDetails,
            },
        });
        return Task.FromResult<Guid?>(operationId);
    }

    private void PublishNodeAdded(SceneDocumentCommandContext context, SceneNode node)
    {
        this.selectionService.SetSelection(context.DocumentId, [node], "Command");
        _ = this.messenger.Send(new SceneNodeAddedMessage([node]));
        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage([node]));
    }

    private void PublishNodeRemoved(SceneDocumentCommandContext context, SceneNode node)
    {
        var selection = this.selectionService.Reconcile(context.DocumentId, context.Scene);
        _ = this.messenger.Send(new SceneNodeRemovedMessage([node]));
        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage([.. selection]));
    }

    private Guid PublishSceneFailure(
        string operationKind,
        string code,
        string title,
        string message,
        SceneDocumentCommandContext context,
        Exception? exception = null,
        FailureDomain domain = FailureDomain.SceneAuthoring)
        => SceneOperationResults.PublishFailure(
            this.operationResults,
            this.statusReducer,
            operationKind,
            domain,
            code,
            title,
            message,
            Scope(context),
            exception);

    private Guid PublishSceneWarning(
        string operationKind,
        string code,
        string title,
        string message,
        SceneDocumentCommandContext context,
        Exception? exception = null,
        FailureDomain domain = FailureDomain.SceneAuthoring)
        => SceneOperationResults.PublishWarning(
            this.operationResults,
            this.statusReducer,
            operationKind,
            domain,
            code,
            title,
            message,
            Scope(context),
            exception);

    private SceneCommandResult ValidationFailure(
        string operationKind,
        string code,
        string title,
        string message,
        SceneDocumentCommandContext context)
    {
        var operationResultId = this.PublishSceneFailure(
            operationKind,
            code,
            title,
            message,
            context);
        return new(false, operationResultId);
    }

    private Guid PublishLiveSyncWarning(
        string operationKind,
        string code,
        string title,
        SceneDocumentCommandContext context,
        SceneNode node,
        Exception exception)
        => SceneOperationResults.PublishWarning(
            this.operationResults,
            this.statusReducer,
            operationKind,
            FailureDomain.LiveSync,
            code,
            title,
            $"The scene node '{node.Name}' was changed in the authoring model, but the live preview did not update.",
            Scope(context, node),
            exception);

    private static AffectedScope Scope(SceneDocumentCommandContext context, SceneNode? node = null)
        => new()
        {
            DocumentId = context.DocumentId,
            DocumentName = context.Metadata.Title,
            SceneId = context.Scene.Id,
            SceneName = context.Scene.Name,
            NodeId = node?.Id,
            NodeName = node?.Name,
        };

    private readonly record struct ValidationIssue(string Code, string Title, string Message, bool IsFailure);

    private sealed record TransformState(SceneNode Node, TransformComponent Transform, Vector3 Position, Quaternion Rotation, Vector3 Scale)
    {
        public static TransformState Capture(SceneNode node, TransformComponent transform)
            => new(node, transform, transform.LocalPosition, transform.LocalRotation, transform.LocalScale);

        public void Apply()
        {
            this.Transform.LocalPosition = this.Position;
            this.Transform.LocalRotation = this.Rotation;
            this.Transform.LocalScale = this.Scale;
        }
    }

    private sealed record TransformEditSessionState(
        EditSessionToken Session,
        IReadOnlyList<TransformState> Before,
        IReadOnlyList<TransformState> Latest);

    private sealed record GeometryState(SceneNode Node, GeometryComponent Geometry, Uri? GeometryUri)
    {
        public static GeometryState Capture(SceneNode node, GeometryComponent geometry)
            => new(node, geometry, geometry.Geometry?.Uri);

        public void Apply()
            => this.Geometry.Geometry = this.GeometryUri is null ? null : new AssetReference<GeometryAsset>(this.GeometryUri);
    }

    private sealed record MaterialSlotState(SceneNode Node, GeometryComponent Geometry, bool HasSlot, Uri? MaterialUri)
    {
        public static MaterialSlotState Capture(SceneNode node, GeometryComponent geometry)
        {
            var slot = geometry.OverrideSlots.OfType<MaterialsSlot>().FirstOrDefault();
            return new(node, geometry, slot is not null, slot?.Material.Uri);
        }

        public void Apply()
        {
            if (!this.HasSlot)
            {
                var slot = this.Geometry.OverrideSlots.OfType<MaterialsSlot>().FirstOrDefault();
                if (slot is not null)
                {
                    _ = this.Geometry.OverrideSlots.Remove(slot);
                }

                return;
            }

            ApplyMaterialSlotEdit(this.Geometry, this.MaterialUri);
        }
    }

    private sealed record CameraState(SceneNode Node, PerspectiveCamera Camera, float FieldOfView, float AspectRatio, float NearPlane, float FarPlane)
    {
        public static CameraState Capture(SceneNode node, PerspectiveCamera camera)
            => new(node, camera, camera.FieldOfView, camera.AspectRatio, camera.NearPlane, camera.FarPlane);

        public void Apply()
        {
            this.Camera.FieldOfView = this.FieldOfView;
            this.Camera.AspectRatio = this.AspectRatio;
            this.Camera.NearPlane = this.NearPlane;
            this.Camera.FarPlane = this.FarPlane;
        }
    }

    private sealed record DirectionalLightState(
        SceneNode Node,
        DirectionalLightComponent Light,
        Vector3 Color,
        float IntensityLux,
        bool IsSunLight,
        bool EnvironmentContribution,
        bool CastsShadows,
        bool AffectsWorld,
        float AngularSizeRadians,
        float ExposureCompensation)
    {
        public static DirectionalLightState Capture(SceneNode node, DirectionalLightComponent light)
            => new(
                node,
                light,
                light.Color,
                light.IntensityLux,
                light.IsSunLight,
                light.EnvironmentContribution,
                light.CastsShadows,
                light.AffectsWorld,
                light.AngularSizeRadians,
                light.ExposureCompensation);

        public void Apply()
        {
            this.Light.Color = this.Color;
            this.Light.IntensityLux = this.IntensityLux;
            this.Light.IsSunLight = this.IsSunLight;
            this.Light.EnvironmentContribution = this.EnvironmentContribution;
            this.Light.CastsShadows = this.CastsShadows;
            this.Light.AffectsWorld = this.AffectsWorld;
            this.Light.AngularSizeRadians = this.AngularSizeRadians;
            this.Light.ExposureCompensation = this.ExposureCompensation;
        }
    }

    private sealed record DirectionalSunState(SceneNode Node, DirectionalLightComponent Light, bool IsSunLight);
}
