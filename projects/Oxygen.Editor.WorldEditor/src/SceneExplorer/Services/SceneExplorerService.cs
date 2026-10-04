// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Services;

/// <summary>
/// Implementation of the <see cref="ISceneExplorerService"/>.
/// </summary>
/// <param name="sceneMutator">The scene mutator.</param>
/// <param name="sceneOrganizer">The scene organizer.</param>
/// <param name="sceneEngineSync">The scene engine sync.</param>
/// <param name="operationResults">The operation results.</param>
/// <param name="statusReducer">The status reducer.</param>
/// <param name="logger">The logger.</param>
public partial class SceneExplorerService(
    ISceneMutator sceneMutator,
    ISceneOrganizer sceneOrganizer,
    ISceneEngineSync sceneEngineSync,
    IOperationResultPublisher operationResults,
    IStatusReducer statusReducer,
    ILogger<SceneExplorerService>? logger = null) : ISceneExplorerService
{
    private readonly ISceneMutator sceneMutator = sceneMutator;
    private readonly ISceneOrganizer sceneOrganizer = sceneOrganizer;
    private readonly ISceneEngineSync sceneEngineSync = sceneEngineSync;
    private readonly IOperationResultPublisher operationResults = operationResults;
    private readonly IStatusReducer statusReducer = statusReducer;
    private readonly ILogger<SceneExplorerService> logger = logger ?? NullLogger<SceneExplorerService>.Instance;

    /// <inheritdoc/>
    public event EventHandler<SceneAuthoringChangedEventArgs>? AuthoringChanged;

    /// <inheritdoc />
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Live synchronization failures are reported without rolling back authoring changes.")]
    public async Task<SceneNodeChangeRecord?> AddNodeAsync(ITreeItem parent, SceneNode node)
    {
        var scene = this.GetScene(parent) ?? throw new InvalidOperationException("Could not resolve scene from parent.");
        using var authoring = SceneAuthoringGate.TryEnter(scene);
        if (authoring is null)
        {
            return null;
        }

        SceneNodeChangeRecord? change = null;

        if (parent is SceneAdapter)
        {
            change = this.sceneMutator.CreateNodeAtRoot(node, scene);
        }
        else if (parent is SceneNodeAdapter nodeAdapter)
        {
            change = this.sceneMutator.CreateNodeUnderParent(node, nodeAdapter.AttachedObject, scene);
        }
        else if (parent is FolderAdapter folderAdapter)
        {
            var sceneParentItem = FindSceneParent(folderAdapter);
            if (sceneParentItem is SceneAdapter)
            {
                change = this.sceneMutator.CreateNodeAtRoot(node, scene);
            }
            else if (sceneParentItem is SceneNodeAdapter sna)
            {
                change = this.sceneMutator.CreateNodeUnderParent(node, sna.AttachedObject, scene);
            }

            // Also update layout to put it in the folder
            _ = this.sceneOrganizer.MoveNodeToFolder(node.Id, folderAdapter.Id, scene);
        }

        this.NotifyAuthoringChanged(scene);
        if (change?.RequiresEngineSync == true)
        {
            try
            {
                await this.sceneEngineSync.CreateNodeAsync(node, change.NewParentId).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                this.LogAuthoringSyncFailed(ex, node.Id, "add");
                this.PublishLiveSyncWarning(
                    SceneOperationKinds.NodeCreate,
                    DiagnosticCodes.LiveSyncPrefix + "CREATE_NODE_FAILED",
                    "Scene was updated but live preview was not",
                    node,
                    ex);
            }
        }

        return change;
    }

    /// <inheritdoc />
    public async Task<IList<SceneNodeChangeRecord>> DeleteItemsAsync(IEnumerable<ITreeItem> items)
    {
        var itemsList = items.ToList();
        var changes = new List<SceneNodeChangeRecord>();
        List<SceneNode> pendingSync = [];
        if (itemsList.Count == 0)
        {
            return changes;
        }

        var scene = this.GetScene(itemsList[0]);
        if (scene == null)
        {
            return changes;
        }

        using var authoring = SceneAuthoringGate.TryEnter(scene);
        if (authoring is null)
        {
            return changes;
        }

        foreach (var item in itemsList)
        {
            if (item is SceneNodeAdapter nodeAdapter)
            {
                var change = this.sceneMutator.RemoveNode(nodeAdapter.AttachedObject.Id, scene);
                if (change.RequiresEngineSync)
                {
                    pendingSync.Add(nodeAdapter.AttachedObject);
                }

                // Also remove from layout if present
                _ = this.sceneOrganizer.RemoveNodeFromLayout(nodeAdapter.AttachedObject.Id, scene);
                changes.Add(change);
                this.NotifyAuthoringChanged(scene);
            }
            else if (item is FolderAdapter folderAdapter)
            {
                _ = this.sceneOrganizer.RemoveFolder(folderAdapter.Id, promoteChildrenToParent: false, scene);
                this.NotifyAuthoringChanged(scene);
            }
        }

        foreach (var node in pendingSync)
        {
            await this.SyncRemovedNodeAsync(node).ConfigureAwait(true);
        }

        return changes;
    }

    private static ITreeItem? FindSceneParent(FolderAdapter item)
    {
        var current = item.Parent;
        while (current != null)
        {
            if (current is SceneAdapter or SceneNodeAdapter)
            {
                return current;
            }

            current = current.Parent;
        }

        return null;
    }

    private void NotifyAuthoringChanged(Scene scene) => this.AuthoringChanged?.Invoke(this, new SceneAuthoringChangedEventArgs(scene));

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Failed live synchronization does not roll back committed authoring state.")]
    private async Task SyncRemovedNodeAsync(SceneNode node)
    {
        try
        {
            await this.sceneEngineSync.RemoveNodeAsync(node.Scene, node.Id).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            this.PublishLiveSyncWarning(SceneOperationKinds.NodeDelete, DiagnosticCodes.LiveSyncPrefix + "REMOVE_NODE_FAILED", "Scene was updated but live preview was not", node, exception);
        }
    }

    private Scene? GetScene(ITreeItem item)
    {
        Scene? scene = null;
        if (item is SceneAdapter sa)
        {
            scene = sa.AttachedObject;
        }
        else if (item is SceneNodeAdapter sna)
        {
            scene = sna.AttachedObject.Scene;
        }
        else
        {
            var parent = item.Parent;
            while (parent != null)
            {
                if (parent is SceneAdapter pSa)
                {
                    scene = pSa.AttachedObject;
                    break;
                }

                if (parent is SceneNodeAdapter pSna)
                {
                    scene = pSna.AttachedObject.Scene;
                    break;
                }

                parent = parent.Parent;
            }
        }

        if (scene != null)
        {
            this.LogGetSceneFound(item, scene);
        }
        else
        {
            this.LogGetSceneNotFound(item);
        }

        return scene;
    }

    private void PublishLiveSyncWarning(
        string operationKind,
        string code,
        string title,
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
            new AffectedScope
            {
                SceneId = node.Scene.Id,
                SceneName = node.Scene.Name,
                NodeId = node.Id,
                NodeName = node.Name,
            },
            exception);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Scene node {NodeId} authoring {Action} succeeded, but live sync failed.")]
    private partial void LogAuthoringSyncFailed(Exception exception, Guid nodeId, string action);
}
