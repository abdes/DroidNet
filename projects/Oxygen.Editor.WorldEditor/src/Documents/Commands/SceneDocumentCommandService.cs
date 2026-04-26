// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <inheritdoc />
public sealed class SceneDocumentCommandService : ISceneDocumentCommandService
{
    private readonly ISceneExplorerService sceneExplorerService;
    private readonly ISceneSelectionService selectionService;
    private readonly ISceneEngineSync sceneEngineSync;
    private readonly IProjectManagerService projectManager;
    private readonly IDocumentService documentService;
    private readonly WindowId windowId;
    private readonly IMessenger messenger;
    private readonly IOperationResultPublisher operationResults;
    private readonly IStatusReducer statusReducer;

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
            _ = node.AddComponent(CreateLightComponent(normalized));

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
            _ = this.messenger.Send(new AssetsCookedMessage());
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
}
