// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <inheritdoc />
/// <param name="automaticCooking">Schedules cooking after acknowledged source saves.</param>
/// <param name="sceneExplorerService">The scene explorer service.</param>
/// <param name="selectionService">The selection service.</param>
/// <param name="sceneEngineSync">The scene engine sync.</param>
/// <param name="projectManager">The project manager.</param>
/// <param name="documentService">The document service.</param>
/// <param name="windowId">The window id.</param>
/// <param name="messenger">The messenger.</param>
/// <param name="operationResults">The operation results.</param>
/// <param name="statusReducer">The status reducer.</param>
public sealed partial class SceneDocumentCommandService(
    Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService automaticCooking,
    ISceneExplorerService sceneExplorerService,
    ISceneSelectionService selectionService,
    ISceneEngineSync sceneEngineSync,
    IProjectManagerService projectManager,
    IDocumentService documentService,
    WindowId windowId,
    IMessenger messenger,
    IOperationResultPublisher operationResults,
    IStatusReducer statusReducer) : ISceneDocumentCommandService
{
    private static readonly System.Runtime.CompilerServices.ConditionalWeakTable<Scene, SemaphoreSlim> SaveGates = [];
    private static readonly Uri EmptyMaterialUri = new($"{AssetUris.Scheme}:///__uninitialized__");

    private readonly ISceneExplorerService sceneExplorerService = sceneExplorerService;
    private readonly ISceneSelectionService selectionService = selectionService;
    private readonly ISceneEngineSync sceneEngineSync = sceneEngineSync;
    private readonly IProjectManagerService projectManager = projectManager;
    private readonly IDocumentService documentService = documentService;
    private readonly WindowId windowId = windowId;
    private readonly IMessenger messenger = messenger;
    private readonly IOperationResultPublisher operationResults = operationResults;
    private readonly IStatusReducer statusReducer = statusReducer;

    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The authoring operation boundary preserves committed state and reports failures to the editor instead of terminating the command loop.")]
    public async Task<SceneValueCommandResult<SceneNode>> CreatePrimitiveAsync(SceneDocumentCommandContext context, string kind)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return SceneCommandResults.Failure<SceneNode>();
        }

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
            return SceneCommandResults.Success(node);
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
            return SceneCommandResults.Failure<SceneNode>(operationResultId);
        }
    }

    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The authoring operation boundary preserves committed state and reports failures to the editor instead of terminating the command loop.")]
    public async Task<SceneValueCommandResult<SceneNode>> CreateLightAsync(SceneDocumentCommandContext context, string kind)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return SceneCommandResults.Failure<SceneNode>();
        }

        try
        {
            var normalized = NormalizeLightKind(kind);
            var node = new SceneNode(context.Scene) { Name = $"{normalized} Light" };
            ApplyLightTransform(node, normalized);
            var light = CreateLightComponent(normalized);

            _ = node.AddComponent(light);

            await this.AddRootNodeAsync(context, node, SceneOperationKinds.NodeCreateLight, $"Create {normalized} Light").ConfigureAwait(true);
            return SceneCommandResults.Success(node);
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
            return SceneCommandResults.Failure<SceneNode>(operationResultId);
        }
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditTransformAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        TransformEdit edit,
        EditSessionToken session)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(nodeIds);
        ArgumentNullException.ThrowIfNull(edit);
        ArgumentNullException.ThrowIfNull(session);

        if (!session.IsOneShot && session.State != EditSessionState.Open)
        {
            return await this.EditPropertiesForTargetsAsync(context, session.NodeIds.ToDictionary(id => id, _ => PropertyEdit.Empty), "Edit Transform", session).ConfigureAwait(true);
        }

        if (!HasAnyTransformField(edit))
        {
            return SceneCommandResult.Success;
        }

        var validation = ValidateTransformEdit(edit);
        if (validation is not null)
        {
            return this.ValidationFailure(SceneOperationKinds.EditTransform, validation.Value.Code, validation.Value.Title, validation.Value.Message, context);
        }

        var targets = ResolveNodes(context.Scene, session.IsOneShot ? nodeIds : session.NodeIds)
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
            var preview = BuildPropertyEditFromTransformEdit(edit);
            return await this.EditPropertiesForTargetsAsync(context, session.NodeIds.ToDictionary(id => id, _ => preview), "Edit Transform", session).ConfigureAwait(true);
        }

        // One-shot path is schema-driven via the property pipeline.
        var propertyEdit = BuildPropertyEditFromTransformEdit(edit);
        return propertyEdit.Count == 0
            ? SceneCommandResult.Success
            : await this.EditPropertiesAsync(
            context,
            targets.ConvertAll(static target => target.Node.Id),
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
        using var authoring = EnterAuthoring(context);
        return authoring is null ? new(Succeeded: false) : await this.EditGeometryCoreAsync(context, nodeIds, edit, session).ConfigureAwait(true);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditMaterialSlotAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        int slotIndex,
        Uri? newMaterialUri,
        EditSessionToken session)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

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

        var before = targets.ConvertAll(static target => MaterialSlotState.Capture(target.Node, target.Geometry!));
        foreach (var target in targets)
        {
            ApplyMaterialSlotEdit(target.Geometry!, newMaterialUri);
        }

        var after = targets.ConvertAll(static target => MaterialSlotState.Capture(target.Node, target.Geometry!));
        if (MaterialSlotStatesEqual(before, after))
        {
            return SceneCommandResult.Success;
        }

        this.RecordMaterialSlotHistory(context, before, after);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        var operationResultId = await this.SyncEditedNodesAsync(
            context,
            targets.ConvertAll(static target => target.Node),
            SceneOperationKinds.EditMaterialSlot,
            node => this.sceneEngineSync.UpdateMaterialSlotAsync(context.Scene, node, slotIndex, newMaterialUri)).ConfigureAwait(true);
        return new SceneCommandResult(Succeeded: true, operationResultId);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditPerspectiveCameraAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        PerspectiveCameraEdit edit,
        EditSessionToken session)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

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

        var before = targets.ConvertAll(static target => CameraState.Capture(target.Node, target.Camera!));
        foreach (var target in targets)
        {
            ApplyPerspectiveCameraEdit(target.Camera!, edit);
        }

        var after = targets.ConvertAll(static target => CameraState.Capture(target.Node, target.Camera!));
        this.RecordCameraHistory(context, before, after);
        var propertyEntries = BuildPerspectiveCameraPropertyEntries(edit);
        var metadataUpdate = this.MarkDirtyAsync(context, out var revision);
        var operationResultId = await CompletePublicationAsync(metadataUpdate, this.SyncEditedNodesAsync(
            context,
            targets.ConvertAll(static target => target.Node),
            SceneOperationKinds.EditPerspectiveCamera,
            node => this.sceneEngineSync.UpdatePropertiesAsync(context.Scene, node, propertyEntries, revision))).ConfigureAwait(true);
        return new SceneCommandResult(Succeeded: true, operationResultId);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditDirectionalLightAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        DirectionalLightEdit edit,
        EditSessionToken session)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

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
            .Select(static node => (node, light: node.Components.OfType<DirectionalLightComponent>().FirstOrDefault()))
            .Where(static target => target.light is not null)
            .ToList();
        return targets.Count == 0
            ? this.ValidationFailure(
                SceneOperationKinds.EditDirectionalLight,
                SceneDiagnosticCodes.ComponentRemoveDenied,
                "Light was not edited",
                "No selected node has a directional light component.",
                context)
            : await this.ApplyDirectionalLightTargetsAsync(context, targets, edit).ConfigureAwait(true);
    }

    /// <inheritdoc />
    public async Task<SceneValueCommandResult<GameComponent>> AddComponentAsync(
        SceneDocumentCommandContext context,
        Guid nodeId,
        Type componentType)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return SceneCommandResults.Failure<GameComponent>();
        }

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
            return SceneCommandResults.Failure<GameComponent>(operationResultId);
        }

        if (!CanAddComponent(node, componentType, out var reason))
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.AddComponent,
                SceneDiagnosticCodes.ComponentAddDenied,
                "Component was not added",
                reason,
                context);
            return SceneCommandResults.Failure<GameComponent>(operationResultId);
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

        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        var operationResultIdFromSync = await this.SyncComponentAddAsync(context, node, component).ConfigureAwait(true);
        if (defaultTransformAfter is not null)
        {
            _ = await this.SyncEditedNodesAsync(context, [node], SceneOperationKinds.EditTransform, syncNode => this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, syncNode)).ConfigureAwait(true);
        }

        _ = this.messenger.Send(new ComponentAddedMessage(node, component, added: true));
        return new SceneValueCommandResult<GameComponent>(Succeeded: true, component, operationResultIdFromSync);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> RemoveComponentAsync(
        SceneDocumentCommandContext context,
        Guid nodeId,
        Guid componentId)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

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
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        var operationResultId = await this.SyncComponentRemoveAsync(context, node, component).ConfigureAwait(true);
        _ = this.messenger.Send(new ComponentRemovedMessage(node, component, removed: true));
        return new SceneCommandResult(Succeeded: true, operationResultId);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditSceneEnvironmentAsync(
        SceneDocumentCommandContext context,
        SceneEnvironmentEdit edit,
        EditSessionToken session)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

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
        if (validation?.IsFailure == true)
        {
            return this.ValidationFailure(SceneOperationKinds.EditEnvironment, validation.Value.Code, validation.Value.Title, validation.Value.Message, context);
        }

        var before = context.Scene.Environment;
        var after = ApplyEnvironmentEdit(before, edit);
        context.Scene.SetEnvironment(after);

        this.RecordEnvironmentHistory(context, before, after);

        var metadataUpdate = this.MarkDirtyAsync(context, out var revision);
        var operationResultId = await CompletePublicationAsync(metadataUpdate, this.PublishEnvironmentSyncAsync(context, after, revision)).ConfigureAwait(true);
        if (validation is not null)
        {
            operationResultId ??= this.PublishSceneWarning(
                SceneOperationKinds.EditEnvironment,
                validation.Value.Code,
                validation.Value.Title,
                validation.Value.Message,
                context);
        }

        return new SceneCommandResult(Succeeded: true, operationResultId);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> SaveSceneAsync(SceneDocumentCommandContext context)
    {
        using var authoring = EnterAuthoring(context);
        return authoring is null ? new(Succeeded: false) : await this.SaveSceneCoreAsync(context).ConfigureAwait(true);
    }

    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The authoring operation boundary preserves committed state and reports failures to the editor instead of terminating the command loop.")]
    public async Task<SceneCommandResult> RenameItemAsync(
        SceneDocumentCommandContext context,
        ITreeItem item,
        string newName)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

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
            return new SceneCommandResult(Succeeded: false, operationResultId);
        }

        try
        {
            await this.sceneExplorerService.RenameItemAsync(item, newName).ConfigureAwait(true);
            if (SceneAuthoringGate.IsRetired(context.Scene))
            {
                return new(Succeeded: false);
            }

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
            return new SceneCommandResult(Succeeded: false, operationResultId);
        }
    }

    private static SceneAuthoringGate.Operation? EnterAuthoring(SceneDocumentCommandContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        return SceneAuthoringGate.TryEnter(context.Scene);
    }

    private static async Task CompletePublicationAsync(Task metadata, Task projection)
        => await Task.WhenAll(metadata, projection).ConfigureAwait(true);

    private static async Task<T> CompletePublicationAsync<T>(Task metadata, Task<T> projection)
    {
        await Task.WhenAll(metadata, projection).ConfigureAwait(true);
        return await projection.ConfigureAwait(true);
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

    private static List<SceneNode> ResolveNodes(Scene scene, IReadOnlyList<Guid> nodeIds)
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
           edit.AtmosphereSlot.HasValue ||
           edit.UsePerPixelAtmosphereTransmittance.HasValue ||
           edit.CastsShadows.HasValue ||
           edit.AffectsWorld.HasValue ||
           edit.AngularSizeRadians.HasValue ||
           edit.ExposureCompensation.HasValue ||
           edit.AtmosphereDiskLuminanceScaleRgb.HasValue ||
           edit.ShadowBias.HasValue ||
           edit.ShadowNormalBias.HasValue ||
           edit.ContactShadows.HasValue ||
           edit.ShadowResolutionHint.HasValue ||
           edit.CascadeCount.HasValue ||
           edit.SplitMode.HasValue ||
           edit.MaxShadowDistance.HasValue ||
           edit.CascadeDistance0.HasValue ||
           edit.CascadeDistance1.HasValue ||
           edit.CascadeDistance2.HasValue ||
           edit.CascadeDistance3.HasValue ||
           edit.DistributionExponent.HasValue ||
           edit.TransitionFraction.HasValue ||
           edit.DistanceFadeoutFraction.HasValue;

    private static bool HasAnyEnvironmentField(SceneEnvironmentEdit edit)
        => edit.AtmosphereEnabled.HasValue ||
           edit.ExposureMode.HasValue ||
           edit.ManualExposureEv.HasValue ||
           edit.ExposureCompensation.HasValue ||
           edit.ToneMapping.HasValue ||
           edit.BackgroundColor.HasValue ||
           edit.SkyAtmosphere.HasValue ||
           edit.PostProcess.HasValue;

    private static T Get<T>(OptionalEditValue<T> optional) => optional.Value!;

    private static ValidationIssue? ValidateTransformEdit(TransformEdit edit)
        => (edit.Position.HasValue && !IsFinite(Get(edit.Position))) ||
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
            (edit.ScaleZ.HasValue && !float.IsFinite(Get(edit.ScaleZ)))
            ? new(
                SceneDiagnosticCodes.TransformFieldNotFinite,
                "Transform was not edited",
                "Transform values must be finite numbers.",
                IsFailure: true)
            : edit.Scale.HasValue && Get(edit.Scale) is { } scale && (scale.X == 0f || scale.Y == 0f || scale.Z == 0f)
            ? new(
                SceneDiagnosticCodes.TransformScaleZeroAxis,
                "Transform was not edited",
                "Scale cannot contain a zero axis.",
                IsFailure: true)
            : (edit.ScaleX.HasValue && Get(edit.ScaleX) == 0f) ||
            (edit.ScaleY.HasValue && Get(edit.ScaleY) == 0f) ||
            (edit.ScaleZ.HasValue && Get(edit.ScaleZ) == 0f)
            ? new(
                SceneDiagnosticCodes.TransformScaleZeroAxis,
                "Transform was not edited",
                "Scale cannot contain a zero axis.",
                IsFailure: true)
            : null;

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
        => (edit.Color.HasValue && !IsFinite(Get(edit.Color))) ||
            (edit.IntensityLux.HasValue && !float.IsFinite(Get(edit.IntensityLux))) ||
            (edit.AngularSizeRadians.HasValue && !float.IsFinite(Get(edit.AngularSizeRadians))) ||
            (edit.ExposureCompensation.HasValue && !float.IsFinite(Get(edit.ExposureCompensation))) ||
            (edit.ShadowBias.HasValue && !float.IsFinite(Get(edit.ShadowBias))) ||
            (edit.ShadowNormalBias.HasValue && !float.IsFinite(Get(edit.ShadowNormalBias))) ||
            (edit.MaxShadowDistance.HasValue && !float.IsFinite(Get(edit.MaxShadowDistance))) ||
            (edit.CascadeDistance0.HasValue && !float.IsFinite(Get(edit.CascadeDistance0))) ||
            (edit.CascadeDistance1.HasValue && !float.IsFinite(Get(edit.CascadeDistance1))) ||
            (edit.CascadeDistance2.HasValue && !float.IsFinite(Get(edit.CascadeDistance2))) ||
            (edit.CascadeDistance3.HasValue && !float.IsFinite(Get(edit.CascadeDistance3))) ||
            (edit.DistributionExponent.HasValue && !float.IsFinite(Get(edit.DistributionExponent))) ||
            (edit.TransitionFraction.HasValue && !float.IsFinite(Get(edit.TransitionFraction))) ||
            (edit.DistanceFadeoutFraction.HasValue && !float.IsFinite(Get(edit.DistanceFadeoutFraction)))
            ? new(
                SceneDiagnosticCodes.DirectionalLightFieldNotFinite,
                "Light was not edited",
                "Directional light values must be finite numbers.",
                IsFailure: true)
            : (edit.AtmosphereSlot.HasValue && !Enum.IsDefined(Get(edit.AtmosphereSlot))) ||
            (edit.ShadowResolutionHint.HasValue && !Enum.IsDefined(Get(edit.ShadowResolutionHint))) ||
            (edit.SplitMode.HasValue && !Enum.IsDefined(Get(edit.SplitMode)))
            ? new(
                SceneDiagnosticCodes.DirectionalLightFieldNotFinite,
                "Light was not edited",
                "Directional light enum values must be valid.",
                IsFailure: true)
            : null;

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

        var candidatePostProcess = ApplyEnvironmentEdit(scene.Environment, edit).PostProcess;
        if (Oxygen.Editor.ContentPipeline.SceneDescriptorGenerator.ValidatePostProcess(candidatePostProcess) is { } postProcessIssue)
        {
            return new(SceneDiagnosticCodes.EnvironmentManualExposureInvalid, "Environment was not edited", postProcessIssue, IsFailure: true);
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
        ApplyDirectionalLightCommonEdit(light, edit);
        ApplyDirectionalLightShadowEdit(light, edit);
        ApplyDirectionalLightCascadeEdit(light, edit);
    }

    private static void ApplyDirectionalLightCommonEdit(DirectionalLightComponent light, DirectionalLightEdit edit)
    {
        if (edit.Color.HasValue)
        {
            light.Color = Get(edit.Color);
        }

        if (edit.IntensityLux.HasValue)
        {
            light.IntensityLux = Get(edit.IntensityLux);
        }

        if (edit.AtmosphereSlot.HasValue)
        {
            light.AtmosphereSlot = Get(edit.AtmosphereSlot);
        }

        if (edit.UsePerPixelAtmosphereTransmittance.HasValue)
        {
            light.UsePerPixelAtmosphereTransmittance = Get(edit.UsePerPixelAtmosphereTransmittance);
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
            light.AngularSizeRadians = Get(edit.AngularSizeRadians);
        }

        if (edit.ExposureCompensation.HasValue)
        {
            light.ExposureCompensation = Get(edit.ExposureCompensation);
        }

        if (edit.AtmosphereDiskLuminanceScaleRgb.HasValue)
        {
            light.AtmosphereDiskLuminanceScaleRgb = Get(edit.AtmosphereDiskLuminanceScaleRgb);
        }
    }

    private static void ApplyDirectionalLightShadowEdit(DirectionalLightComponent light, DirectionalLightEdit edit)
    {
        if (edit.ShadowBias.HasValue)
        {
            light.ShadowBias = Get(edit.ShadowBias);
        }

        if (edit.ShadowNormalBias.HasValue)
        {
            light.ShadowNormalBias = Get(edit.ShadowNormalBias);
        }

        if (edit.ContactShadows.HasValue)
        {
            light.ContactShadows = Get(edit.ContactShadows);
        }

        if (edit.ShadowResolutionHint.HasValue)
        {
            light.ShadowResolutionHint = Get(edit.ShadowResolutionHint);
        }
    }

    private static void ApplyDirectionalLightCascadeEdit(DirectionalLightComponent light, DirectionalLightEdit edit)
    {
        if (edit.CascadeCount.HasValue)
        {
            light.CascadeCount = Get(edit.CascadeCount);
        }

        if (edit.SplitMode.HasValue)
        {
            light.SplitMode = Get(edit.SplitMode);
        }

        if (edit.MaxShadowDistance.HasValue)
        {
            light.MaxShadowDistance = Get(edit.MaxShadowDistance);
        }

        if (edit.CascadeDistance0.HasValue)
        {
            var distances = light.CascadeDistances;
            light.CascadeDistances = new Vector4(Get(edit.CascadeDistance0), distances.Y, distances.Z, distances.W);
        }

        if (edit.CascadeDistance1.HasValue)
        {
            var distances = light.CascadeDistances;
            light.CascadeDistances = new Vector4(distances.X, Get(edit.CascadeDistance1), distances.Z, distances.W);
        }

        if (edit.CascadeDistance2.HasValue)
        {
            var distances = light.CascadeDistances;
            light.CascadeDistances = new Vector4(distances.X, distances.Y, Get(edit.CascadeDistance2), distances.W);
        }

        if (edit.CascadeDistance3.HasValue)
        {
            var distances = light.CascadeDistances;
            light.CascadeDistances = new Vector4(distances.X, distances.Y, distances.Z, Get(edit.CascadeDistance3));
        }

        if (edit.DistributionExponent.HasValue)
        {
            light.DistributionExponent = Get(edit.DistributionExponent);
        }

        if (edit.TransitionFraction.HasValue)
        {
            light.TransitionFraction = Get(edit.TransitionFraction);
        }

        if (edit.DistanceFadeoutFraction.HasValue)
        {
            light.DistanceFadeoutFraction = Get(edit.DistanceFadeoutFraction);
        }
    }

    private static SceneEnvironmentData ApplyEnvironmentEdit(SceneEnvironmentData current, SceneEnvironmentEdit edit)
    {
        var postProcess = edit.PostProcess.HasValue
            ? Get(edit.PostProcess)
            : current.PostProcess ?? new();

        if (edit.ExposureMode.HasValue)
        {
            postProcess = postProcess with { ExposureMode = Get(edit.ExposureMode) };
        }

        if (edit.ManualExposureEv.HasValue)
        {
            postProcess = postProcess with { ManualExposureEv = Get(edit.ManualExposureEv) };
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
        => componentType switch
        {
            _ when componentType == typeof(GeometryComponent) => new GeometryComponent
            {
                Name = "Geometry",
                Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
            },
            _ when componentType == typeof(PerspectiveCamera) => new PerspectiveCamera { Name = "Perspective Camera" },
            _ when componentType == typeof(OrthographicCamera) => new OrthographicCamera { Name = "Orthographic Camera" },
            _ when componentType == typeof(DirectionalLightComponent) => new DirectionalLightComponent { Name = "Directional Light", CastsShadows = true, AtmosphereSlot = AtmosphereLightSlot.None },
            _ when componentType == typeof(PointLightComponent) => new PointLightComponent
            {
                Name = "Point Light",
                LuminousFluxLumens = 1_600f,
                Range = 10f,
            },
            _ when componentType == typeof(SpotLightComponent) => new SpotLightComponent
            {
                Name = "Spot Light",
                LuminousFluxLumens = 1_600f,
                Range = 15f,
            },
            _ => throw new NotSupportedException($"Component type '{componentType.Name}' is not supported."),
        };

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

    private async Task<SceneCommandResult> EditGeometryCoreAsync(
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

        var before = targets.ConvertAll(static target => GeometryState.Capture(target.Node, target.Geometry!));
        foreach (var target in targets)
        {
            target.Geometry!.Geometry = new AssetReference<GeometryAsset>(edit.GeometryUri.Value);
        }

        var after = targets.ConvertAll(static target => GeometryState.Capture(target.Node, target.Geometry!));
        if (GeometryStatesEqual(before, after))
        {
            return SceneCommandResult.Success;
        }

        this.RecordGeometryHistory(context, before, after);
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        var operationResultId = await this.SyncGeometryStatesAsync(context, after, SceneOperationKinds.EditGeometry).ConfigureAwait(true);
        return new SceneCommandResult(Succeeded: true, operationResultId);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The authoring operation boundary preserves committed state and reports failures to the editor instead of terminating the command loop.")]
    private async Task<SceneCommandResult> SaveSceneCoreAsync(SceneDocumentCommandContext context)
    {
        await this.CompleteEditSessionsAsync(context, commit: true).ConfigureAwait(true);
        var gate = SaveGates.GetValue(context.Scene, static _ => new SemaphoreSlim(1, 1));
        await gate.WaitAsync().ConfigureAwait(true);
        try
        {
            await this.CompleteEditSessionsAsync(context, commit: true).ConfigureAwait(true);
            var version = context.Metadata.ChangeVersion;
            var snapshot = SceneSaveSnapshot.Capture(context.Scene);
            var previousSource = this.projectManager.GetSceneSourceVersion(context.Scene);
            var success = await this.projectManager.SaveSceneSnapshotAsync(snapshot).ConfigureAwait(true);
            if (!success)
            {
                var operationResultId = this.PublishSceneFailure(
                    SceneOperationKinds.Save,
                    DiagnosticCodes.DocumentPrefix + "SAVE_FAILED",
                    "Scene was not saved",
                    "The scene data could not be saved.",
                    context,
                    domain: FailureDomain.Document);
                return new SceneCommandResult(Succeeded: false, operationResultId);
            }

            var result = await this.AcknowledgeSceneSaveAsync(context, version).ConfigureAwait(true);
            this.NotifySceneSaved(context.Scene, previousSource?.Version.Sha256);
            return result;
        }
        catch (DroidNet.Storage.StorageWriteConflictException exception)
        {
            var operationResultId = this.PublishSceneFailure(
                SceneOperationKinds.Save,
                DiagnosticCodes.DocumentPrefix + "Conflict",
                "Scene changed outside this document",
                exception.Message,
                context,
                exception,
                FailureDomain.Document);
            return new SceneCommandResult(Succeeded: false, operationResultId) { IsConflict = true, HasUnsavedChanges = context.Metadata.IsDirty };
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
            return new SceneCommandResult(Succeeded: false, operationResultId);
        }
        finally
        {
            _ = gate.Release();
        }
    }

    private void NotifySceneSaved(Scene scene, string? previousHash)
    {
        if (this.projectManager.GetSceneSourceVersion(scene) is { } savedSource)
        {
            automaticCooking.NotifySaved(
                savedSource.SourcePath,
                savedSource.Version.Sha256,
                contentChanged: !string.Equals(previousHash, savedSource.Version.Sha256, StringComparison.Ordinal));
        }
    }

    private async Task<SceneCommandResult> AcknowledgeSceneSaveAsync(SceneDocumentCommandContext context, long version)
    {
        if (SceneAuthoringGate.IsRetired(context.Scene))
        {
            return new(Succeeded: true) { HasUnsavedChanges = context.Metadata.IsDirty };
        }

        context.Metadata.MarkSaved(version);
        _ = await this.documentService.UpdateMetadataAsync(this.windowId, context.DocumentId, context.Metadata).ConfigureAwait(true);
        _ = this.messenger.Send(new AssetsChangedMessage());
        var notice = context.Metadata.IsDirty
            ? this.PublishSceneWarning(SceneOperationKinds.Save, DiagnosticCodes.DocumentPrefix + "NEWER_CHANGES_UNSAVED", "Scene snapshot saved", "Saved; newer changes remain unsaved", context, domain: FailureDomain.Document)
            : (Guid?)null;
        return new SceneCommandResult(Succeeded: true, notice) { HasUnsavedChanges = context.Metadata.IsDirty };
    }

    private async Task<SceneCommandResult> ApplyDirectionalLightTargetsAsync(
        SceneDocumentCommandContext context,
        List<(SceneNode node, DirectionalLightComponent? light)> targets,
        DirectionalLightEdit edit)
    {
        var edits = targets.ToDictionary(target => target.node.Id, _ => edit);
        if (ValidateDirectionalLightCandidates(context.Scene, edits) is { } invalid)
        {
            return this.ValidationFailure(SceneOperationKinds.EditDirectionalLight,
                invalid.Code, invalid.Title, invalid.Message, context);
        }
        var before = targets.ConvertAll(static target => DirectionalLightState.Capture(target.node, target.light!));
        foreach (var (_, light) in targets) ApplyDirectionalLightEdit(light!, edit);
        var after = targets.ConvertAll(static target => DirectionalLightState.Capture(target.node, target.light!));
        this.RecordDirectionalLightHistory(context, new(before), new(after));
        var nodes = targets.Select(static target => target.node).ToList();
        var payloads = targets.ToDictionary(target => target.node.Id,
            target => BuildDirectionalLightPropertyEntries(edit, target.light!));
        var metadataUpdate = this.MarkDirtyAsync(context, out var revision);
        var operationResultId = await CompletePublicationAsync(metadataUpdate, this.SyncEditedNodesAsync(
            context, nodes, SceneOperationKinds.EditDirectionalLight,
            node => this.sceneEngineSync.UpdatePropertiesAsync(context.Scene, node, payloads[node.Id], revision))).ConfigureAwait(true);
        return new SceneCommandResult(Succeeded: true, operationResultId);
    }

    private async Task AddRootNodeAsync(
        SceneDocumentCommandContext context,
        SceneNode node,
        string operationKind,
        string undoLabel)
    {
        context.Scene.RootNodes.Add(node);
        context.History.AddChange($"Remove {undoLabel}", async () => await this.RemoveRootNodeForUndoAsync(context, node, operationKind).ConfigureAwait(true));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        await this.TrySyncCreateAsync(context, node, operationKind).ConfigureAwait(true);
        this.PublishNodeAdded(context, node);
    }

    private async Task RemoveRootNodeForUndoAsync(SceneDocumentCommandContext context, SceneNode node, string operationKind)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        _ = context.Scene.RootNodes.Remove(node);
        context.History.AddChange($"Restore {node.Name}", async () => await this.RestoreRootNodeForRedoAsync(context, node, operationKind).ConfigureAwait(true));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        await this.TrySyncRemoveAsync(context, node, operationKind).ConfigureAwait(true);
        this.PublishNodeRemoved(context, node);
    }

    private async Task RestoreRootNodeForRedoAsync(SceneDocumentCommandContext context, SceneNode node, string operationKind)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        if (!context.Scene.RootNodes.Contains(node))
        {
            context.Scene.RootNodes.Add(node);
        }

        context.History.AddChange($"Remove {node.Name}", async () => await this.RemoveRootNodeForUndoAsync(context, node, operationKind).ConfigureAwait(true));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        await this.TrySyncCreateAsync(context, node, operationKind).ConfigureAwait(true);
        this.PublishNodeAdded(context, node);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The authoring operation boundary preserves committed state and reports failures to the editor instead of terminating the command loop.")]
    private async Task TrySyncCreateAsync(SceneDocumentCommandContext context, SceneNode node, string operationKind)
    {
        try
        {
            await this.sceneEngineSync.CreateNodeAsync(node, parentGuid: null).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            _ = this.PublishLiveSyncWarning(operationKind, DiagnosticCodes.LiveSyncPrefix + "CREATE_NODE_FAILED", "Scene was updated but live preview was not", context, node, ex);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The authoring operation boundary preserves committed state and reports failures to the editor instead of terminating the command loop.")]
    private async Task TrySyncRemoveAsync(SceneDocumentCommandContext context, SceneNode node, string operationKind)
    {
        try
        {
            await this.sceneEngineSync.RemoveNodeHierarchyAsync(context.Scene, node.Id).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            _ = this.PublishLiveSyncWarning(operationKind, DiagnosticCodes.LiveSyncPrefix + "REMOVE_NODE_FAILED", "Scene was updated but live preview was not", context, node, ex);
        }
    }

    private Task MarkDirtyAsync(SceneDocumentCommandContext context)
        => this.MarkDirtyAsync(context, out _);

    private Task MarkDirtyAsync(SceneDocumentCommandContext context, out SceneSyncRevision revision)
    {
        var wasDirty = context.Metadata.IsDirty;
        context.Metadata.IsDirty = true;
        revision = this.sceneEngineSync.CaptureRevision(context.Scene, context.Metadata);
        return wasDirty ? Task.CompletedTask : this.PublishDirtyMetadataAsync(context);
    }

    private async Task PublishDirtyMetadataAsync(SceneDocumentCommandContext context)
        => _ = await this.documentService.UpdateMetadataAsync(this.windowId, context.DocumentId, context.Metadata).ConfigureAwait(true);

    private void RecordGeometryHistory(SceneDocumentCommandContext context, IReadOnlyList<GeometryState> before, IReadOnlyList<GeometryState> after)
        => context.History.AddChange("Restore Geometry", async () => await this.ApplyGeometryStatesForHistoryAsync(context, before, after).ConfigureAwait(true));

    private async Task ApplyGeometryStatesForHistoryAsync(SceneDocumentCommandContext context, IReadOnlyList<GeometryState> states, IReadOnlyList<GeometryState> inverse)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        foreach (var state in states)
        {
            state.Apply();
        }

        context.History.AddChange("Reapply Geometry", async () => await this.ApplyGeometryStatesForHistoryAsync(context, inverse, states).ConfigureAwait(true));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        _ = await this.SyncGeometryStatesAsync(context, states, SceneOperationKinds.EditGeometry).ConfigureAwait(true);
    }

    private void RecordMaterialSlotHistory(SceneDocumentCommandContext context, IReadOnlyList<MaterialSlotState> before, IReadOnlyList<MaterialSlotState> after)
        => context.History.AddChange("Restore Material Slot", async () => await this.ApplyMaterialSlotStatesForHistoryAsync(context, before, after).ConfigureAwait(true));

    private async Task ApplyMaterialSlotStatesForHistoryAsync(SceneDocumentCommandContext context, IReadOnlyList<MaterialSlotState> states, IReadOnlyList<MaterialSlotState> inverse)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        foreach (var state in states)
        {
            state.Apply();
        }

        context.History.AddChange("Reapply Material Slot", async () => await this.ApplyMaterialSlotStatesForHistoryAsync(context, inverse, states).ConfigureAwait(true));
        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        _ = await this.SyncEditedNodesAsync(context, states.Select(static state => state.Node).ToList(), SceneOperationKinds.EditMaterialSlot, node => this.sceneEngineSync.UpdateMaterialSlotAsync(context.Scene, node, 0, ToMaterialSyncUri(states.First(state => state.Node == node).MaterialUri))).ConfigureAwait(true);
    }

    private void RecordCameraHistory(SceneDocumentCommandContext context, IReadOnlyList<CameraState> before, IReadOnlyList<CameraState> after)
        => context.History.AddChange("Restore Camera", async () => await this.ApplyCameraStatesForHistoryAsync(context, before, after).ConfigureAwait(true));

    private async Task ApplyCameraStatesForHistoryAsync(SceneDocumentCommandContext context, IReadOnlyList<CameraState> states, IReadOnlyList<CameraState> inverse)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        foreach (var state in states)
        {
            state.Apply();
        }

        context.History.AddChange("Reapply Camera", async () => await this.ApplyCameraStatesForHistoryAsync(context, inverse, states).ConfigureAwait(true));
        var payloads = states.ToDictionary(state => state.Node.Id, state => BuildPerspectiveCameraPropertyEntries(state.Node.Components.OfType<PerspectiveCamera>().First()));
        var metadataUpdate = this.MarkDirtyAsync(context, out var revision);
        _ = await CompletePublicationAsync(metadataUpdate, this.SyncEditedNodesAsync(
            context,
            states.Select(static state => state.Node).ToList(),
            SceneOperationKinds.EditPerspectiveCamera,
            node => this.sceneEngineSync.UpdatePropertiesAsync(context.Scene, node, payloads[node.Id], revision))).ConfigureAwait(true);
    }

    private void RecordDirectionalLightHistory(
        SceneDocumentCommandContext context,
        DirectionalLightSceneState before,
        DirectionalLightSceneState after)
        => context.History.AddChange("Restore Directional Light", async () => await this.ApplyDirectionalLightStatesForHistoryAsync(context, before, after).ConfigureAwait(true));

    private async Task ApplyDirectionalLightStatesForHistoryAsync(
        SceneDocumentCommandContext context,
        DirectionalLightSceneState state,
        DirectionalLightSceneState inverse)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        foreach (var target in state.Targets)
        {
            target.Apply();
        }

        context.History.AddChange("Reapply Directional Light", async () => await this.ApplyDirectionalLightStatesForHistoryAsync(context, inverse, state).ConfigureAwait(true));
        var syncNodes = state.Targets.Select(static target => target.Node).ToList();
        var payloads = syncNodes.ToDictionary(node => node.Id, node => BuildDirectionalLightPropertyEntries(node.Components.OfType<DirectionalLightComponent>().First()));
        var metadataUpdate = this.MarkDirtyAsync(context, out var revision);
        _ = await CompletePublicationAsync(metadataUpdate, this.SyncEditedNodesAsync(
            context,
            syncNodes,
            SceneOperationKinds.EditDirectionalLight,
            node => this.sceneEngineSync.UpdatePropertiesAsync(context.Scene, node, payloads[node.Id], revision))).ConfigureAwait(true);
    }

    private void RecordEnvironmentHistory(
        SceneDocumentCommandContext context,
        SceneEnvironmentData before,
        SceneEnvironmentData after)
        => context.History.AddChange("Restore Environment", async () => await this.ApplyEnvironmentForHistoryAsync(context, before, after).ConfigureAwait(true));

    private async Task ApplyEnvironmentForHistoryAsync(
        SceneDocumentCommandContext context,
        SceneEnvironmentData environment,
        SceneEnvironmentData inverse)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return;
        }

        context.Scene.SetEnvironment(environment);
        context.History.AddChange("Reapply Environment", async () => await this.ApplyEnvironmentForHistoryAsync(context, inverse, environment).ConfigureAwait(true));
        var metadataUpdate = this.MarkDirtyAsync(context, out var revision);
        _ = await CompletePublicationAsync(metadataUpdate, this.PublishEnvironmentSyncAsync(context, environment, revision)).ConfigureAwait(true);
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

        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        _ = await this.SyncComponentRemoveAsync(context, node, component).ConfigureAwait(true);
        if (defaultTransformBefore is not null)
        {
            _ = await this.SyncEditedNodesAsync(context, [node], SceneOperationKinds.EditTransform, syncNode => this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, syncNode)).ConfigureAwait(true);
        }

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

        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        _ = await this.SyncComponentAddAsync(context, node, component).ConfigureAwait(true);
        if (defaultTransformAfter is not null)
        {
            _ = await this.SyncEditedNodesAsync(context, [node], SceneOperationKinds.EditTransform, syncNode => this.sceneEngineSync.UpdateNodeTransformAsync(context.Scene, syncNode)).ConfigureAwait(true);
        }

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

    private async Task<Guid?> PublishEnvironmentSyncAsync(SceneDocumentCommandContext context, SceneEnvironmentData environment, SceneSyncRevision revision)
    {
        var result = await this.sceneEngineSync.UpdateEnvironmentAsync(context.Scene, environment, revision).ConfigureAwait(true);
        if (result.Overall == SyncStatus.Accepted)
        {
            return null;
        }

        if (result.PerField.Count > 0)
        {
            Guid? firstResult = null;
            foreach (var fieldOutcome in result.PerField.Values
                .Where(value => value.Status != SyncStatus.Accepted)
                .DistinctBy(value => (value.Status, value.Code, value.Message)))
            {
                var published = await this.PublishSyncOutcomeAsync(context, SceneOperationKinds.EditEnvironment, fieldOutcome).ConfigureAwait(true);
                firstResult ??= published;
            }

            return firstResult;
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
            Code = outcome.Code ?? (DiagnosticCodes.LiveSyncPrefix + "Unknown"),
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
        if (SceneAuthoringGate.IsRetired(context.Scene) || !ReferenceEquals(FindNode(context.Scene, node.Id), node))
        {
            return;
        }

        this.selectionService.SetSelection(context.DocumentId, [node], "Command");
        _ = this.messenger.Send(new SceneNodeAddedMessage([node]));
        _ = this.messenger.Send(new SceneNodeSelectionChangedMessage([node]));
    }

    private void PublishNodeRemoved(SceneDocumentCommandContext context, SceneNode node)
    {
        if (SceneAuthoringGate.IsRetired(context.Scene))
        {
            return;
        }

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
        return new(Succeeded: false, operationResultId) { ValidationCode = code, ValidationMessage = message };
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
        AtmosphereLightSlot AtmosphereSlot,
        bool UsePerPixelAtmosphereTransmittance,
        bool CastsShadows,
        bool AffectsWorld,
        float AngularSizeRadians,
        float ExposureCompensation,
        Vector3 AtmosphereDiskLuminanceScaleRgb,
        float ShadowBias,
        float ShadowNormalBias,
        bool ContactShadows,
        ShadowResolutionHint ShadowResolutionHint,
        int CascadeCount,
        DirectionalCsmSplitMode SplitMode,
        float MaxShadowDistance,
        Vector4 CascadeDistances,
        float DistributionExponent,
        float TransitionFraction,
        float DistanceFadeoutFraction)
    {
        public static DirectionalLightState Capture(SceneNode node, DirectionalLightComponent light)
            => new(
                node,
                light,
                light.Color,
                light.IntensityLux,
                light.AtmosphereSlot,
                light.UsePerPixelAtmosphereTransmittance,
                light.CastsShadows,
                light.AffectsWorld,
                light.AngularSizeRadians,
                light.ExposureCompensation,
                light.AtmosphereDiskLuminanceScaleRgb,
                light.ShadowBias,
                light.ShadowNormalBias,
                light.ContactShadows,
                light.ShadowResolutionHint,
                light.CascadeCount,
                light.SplitMode,
                light.MaxShadowDistance,
                light.CascadeDistances,
                light.DistributionExponent,
                light.TransitionFraction,
                light.DistanceFadeoutFraction);

        public void Apply()
        {
            this.Light.Color = this.Color;
            this.Light.IntensityLux = this.IntensityLux;
            this.Light.AtmosphereSlot = this.AtmosphereSlot;
            this.Light.UsePerPixelAtmosphereTransmittance = this.UsePerPixelAtmosphereTransmittance;
            this.Light.CastsShadows = this.CastsShadows;
            this.Light.AffectsWorld = this.AffectsWorld;
            this.Light.AngularSizeRadians = this.AngularSizeRadians;
            this.Light.ExposureCompensation = this.ExposureCompensation;
            this.Light.AtmosphereDiskLuminanceScaleRgb = this.AtmosphereDiskLuminanceScaleRgb;
            this.Light.ShadowBias = this.ShadowBias;
            this.Light.ShadowNormalBias = this.ShadowNormalBias;
            this.Light.ContactShadows = this.ContactShadows;
            this.Light.ShadowResolutionHint = this.ShadowResolutionHint;
            this.Light.CascadeCount = this.CascadeCount;
            this.Light.SplitMode = this.SplitMode;
            this.Light.MaxShadowDistance = this.MaxShadowDistance;
            this.Light.CascadeDistances = this.CascadeDistances;
            this.Light.DistributionExponent = this.DistributionExponent;
            this.Light.TransitionFraction = this.TransitionFraction;
            this.Light.DistanceFadeoutFraction = this.DistanceFadeoutFraction;
        }
    }


    private sealed record DirectionalLightSceneState(IReadOnlyList<DirectionalLightState> Targets);
}
