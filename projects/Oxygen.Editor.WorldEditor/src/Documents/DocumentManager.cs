// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Documents;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Inspection;

namespace Oxygen.Editor.World.Documents;

/// <summary>
/// Manages the lifecycle of documents in the World Editor, handling requests to open or create documents.
/// </summary>
public sealed partial class DocumentManager : IDisposable
{
    private readonly ILogger logger;
    private readonly IEditorDocumentService documentService;
    private readonly IMessenger messenger;
    private readonly IProjectContextService projectContextService;
    private readonly IProjectUsageService projectUsage;
    private readonly IMaterialDocumentService materialDocumentService;
    private readonly WindowId windowId;
    private readonly IProjectManagerService projectManager;
    private readonly Services.ISceneEngineSync sceneEngineSync;
    private readonly SemaphoreSlim sceneReplacementGate = new(1, 1);
    private readonly Oxygen.Managed.Core.Diagnostics.IOperationResultPublisher? operationResults;
    private readonly Oxygen.Managed.Core.Diagnostics.IStatusReducer? statusReducer;
    private long sceneRequestId;
    private (Guid projectId, string? projectRoot, Guid sceneId)? previousSavedScene;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="DocumentManager"/> class.
    /// </summary>
    /// <param name="documentService">The service used to manage documents.</param>
    /// <param name="messenger">The messenger for inter-component communication.</param>
    /// <param name="projectContextService">The active project and its activation lifetime.</param>
    /// <param name="projectUsage">The workspace's recent-scene persistence.</param>
    /// <param name="materialDocumentService">The material authoring service.</param>
    /// <param name="windowId">The identifier of the window associated with this manager.</param>
    /// <param name="projectManager">The project owner used to stage, accept and retire editable scenes.</param>
    /// <param name="sceneEngineSync">The owner of open scene runtime lifetimes.</param>
    /// <param name="loggerFactory">Optional logger factory for logging.</param>
    /// <param name="operationResults">Optional visible operation-result publisher.</param>
    /// <param name="statusReducer">Optional reducer for scene-load diagnostics.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "MA0147:Avoid async void method for delegate", Justification = "messenger handlers reply to the requester once the awaited open or creation completes")]
    public DocumentManager(
        IEditorDocumentService documentService,
        IMessenger messenger,
        IProjectContextService projectContextService,
        IProjectUsageService projectUsage,
        IMaterialDocumentService materialDocumentService,
        WindowId windowId,
        IProjectManagerService projectManager,
        Services.ISceneEngineSync sceneEngineSync,
        ILoggerFactory? loggerFactory = null,
        Oxygen.Managed.Core.Diagnostics.IOperationResultPublisher? operationResults = null,
        Oxygen.Managed.Core.Diagnostics.IStatusReducer? statusReducer = null)
    {
        this.logger = loggerFactory?.CreateLogger<DocumentManager>() ?? NullLoggerFactory.Instance.CreateLogger<DocumentManager>();

        this.documentService = documentService;
        this.messenger = messenger;
        this.projectContextService = projectContextService;
        this.projectUsage = projectUsage;
        this.materialDocumentService = materialDocumentService;
        this.windowId = windowId;
        this.projectManager = projectManager;
        this.sceneEngineSync = sceneEngineSync;
        this.operationResults = operationResults;
        this.statusReducer = statusReducer;

        this.messenger.Register<OpenSceneRequestMessage>(this, this.OnOpenSceneRequested);
        this.messenger.Register<OpenMaterialRequestMessage>(this, this.OnOpenMaterialRequested);
        this.messenger.Register<CreateMaterialRequestMessage>(this, this.OnCreateMaterialRequested);
        this.messenger.Register<OpenCookedInspectionRequestMessage>(this, (_, message) => message.Reply(this.OpenInspectionAsync(message)));
        this.RegisterRelocationMessages();
        this.messenger.Register<ReloadPreviousSceneRequestMessage>(this, (_, message) =>
        {
            if (message.WindowId == this.windowId && !message.HasReceivedResponse)
            {
                message.Reply(this.ReloadPreviousSceneAsync());
            }
        });
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.disposed = true;
        _ = Interlocked.Increment(ref this.sceneRequestId);
        this.messenger.UnregisterAll(this);
        this.sceneReplacementGate.Dispose();
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Opens or activates the specified scene document for this workspace.
    /// </summary>
    /// <param name="scene">The scene to open.</param>
    /// <param name="cancellationToken">Cancels staging or guard preparation before the previous graph is retired.</param>
    /// <returns><see langword="true"/> when the scene document is open and selected.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Scene replacement preserves the accepted lifetime before retirement and reports unavailable state afterwards.")]
    public async Task<bool> OpenSceneAsync(World.Scene scene, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        this.LogOnOpenSceneRequested(scene);

        if (this.windowId.Value == 0)
        {
            this.LogCannotOpenSceneWindowIdInvalid(scene);
            return false;
        }

        var requestId = Interlocked.Increment(ref this.sceneRequestId);
        try
        {
            await this.sceneReplacementGate.WaitAsync(cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return false;
        }

        var replacement = new SceneReplacement();
        try
        {
            return await this.ReplaceSceneAsync(scene, requestId, replacement, cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return false;
        }
        catch (Exception exception)
        {
            this.LogSceneReplacementFailed(exception, scene.Id);
            if (!replacement.Retired && replacement.Incoming is null)
            {
                this.PublishSceneLoadFailure(scene, unavailable: false);
            }

            return false;
        }
        finally
        {
            await this.FinishSceneReplacementAsync(scene, replacement).ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Opens or activates the specified material document for this workspace.
    /// </summary>
    /// <param name="materialUri">The material source asset URI.</param>
    /// <param name="title">The document title.</param>
    /// <returns><see langword="true"/> when the material document is open and selected.</returns>
    public async Task<bool> OpenMaterialAsync(Uri materialUri, string title)
    {
        ArgumentNullException.ThrowIfNull(materialUri);

        if (this.windowId.Value == 0)
        {
            return false;
        }

        var openDocs = this.documentService.GetOpenDocuments(this.windowId);
        var existing = openDocs
            .OfType<MaterialDocumentMetadata>()
            .FirstOrDefault(d => UriValuesEqual(d.MaterialUri, materialUri));
        if (existing is not null)
        {
            return await this.documentService.SelectDocumentAsync(this.windowId, existing.DocumentId).ConfigureAwait(true);
        }

        var metadata = new MaterialDocumentMetadata(materialUri)
        {
            Title = string.IsNullOrWhiteSpace(title) ? Path.GetFileNameWithoutExtension(materialUri.AbsolutePath) : title,
        };

        var openedId = await this.documentService.OpenDocumentAsync(this.windowId, metadata).ConfigureAwait(true);
        return openedId != Guid.Empty;
    }

    private static bool UriValuesEqual(Uri left, Uri right)
        => string.Equals(left.ToString(), right.ToString(), StringComparison.OrdinalIgnoreCase);

    private async Task FinishSceneReplacementAsync(World.Scene scene, SceneReplacement replacement)
    {
        try
        {
            if (!replacement.Installed && replacement.Incoming is not null)
            {
                _ = await this.documentService.CloseDocumentAsync(this.windowId, replacement.Incoming.DocumentId, force: true).ConfigureAwait(true);
                if (scene.Project.Scenes.FirstOrDefault(value => value.Id == replacement.Incoming.DocumentId) is { } failed)
                {
                    this.projectManager.RetireScene(failed);
                }
            }
        }
        finally
        {
            _ = this.sceneReplacementGate.Release();

            if (!replacement.Installed && (replacement.Retired || replacement.Incoming is not null))
            {
                this.PublishSceneLoadFailure(scene, unavailable: true);
            }
        }
    }

    private async Task<bool> ReplaceSceneAsync(World.Scene scene, long requestId, SceneReplacement replacement, CancellationToken cancellationToken)
    {
        var project = this.projectManager.CurrentProject;
        if (this.disposed || requestId != Volatile.Read(ref this.sceneRequestId) || !ReferenceEquals(scene.Project, project)
            || !scene.Project.Scenes.Contains(scene))
        {
            return false;
        }

        var openScenes = this.documentService.GetOpenDocuments(this.windowId).OfType<SceneDocumentMetadata>().ToArray();
        var existing = openScenes.FirstOrDefault(document => document.DocumentId == scene.Id);
        if (existing is not null && this.sceneEngineSync.GetDocumentScene(existing) is { } current)
        {
            var selected = await this.documentService.SelectDocumentAsync(this.windowId, scene.Id).ConfigureAwait(true);
            if (selected)
            {
                await this.MarkSceneActivatedAsync(current).ConfigureAwait(true);
            }

            return selected;
        }

        var staged = await this.projectManager.StageSceneLoadAsync(scene, cancellationToken).ConfigureAwait(true);
        if (staged is null)
        {
            this.PublishSceneLoadFailure(scene, unavailable: false);
            return false;
        }

        if (requestId != Volatile.Read(ref this.sceneRequestId)
            || !ReferenceEquals(project, this.projectManager.CurrentProject))
        {
            return false;
        }

        if (!await this.RetireOpenScenesAsync(openScenes, project, requestId, replacement, cancellationToken).ConfigureAwait(true))
        {
            return false;
        }

        return await this.InstallStagedSceneAsync(staged, project, replacement).ConfigureAwait(true);
    }

    private async Task<bool> RetireOpenScenesAsync(
        SceneDocumentMetadata[] openScenes,
        IProject? project,
        long requestId,
        SceneReplacement replacement,
        CancellationToken cancellationToken)
    {
        foreach (var previous in openScenes)
        {
            var graph = this.sceneEngineSync.GetDocumentScene(previous);
            using var closing = await this.documentService.PrepareCloseDocumentAsync(this.windowId, previous.DocumentId).ConfigureAwait(true);
            cancellationToken.ThrowIfCancellationRequested();
            if (closing is null || requestId != Volatile.Read(ref this.sceneRequestId)
                || !ReferenceEquals(project, this.projectManager.CurrentProject)
                || !await closing.CommitAsync().ConfigureAwait(true))
            {
                return false;
            }

            if (graph is not null)
            {
                this.previousSavedScene = (graph.Project.ProjectInfo.Id, graph.Project.ProjectInfo.Location, graph.Id);
                this.sceneEngineSync.CloseDocument(previous);
                SceneAuthoringGate.Retire(graph);
                this.projectManager.RetireScene(graph);
            }

            replacement.Retired = true;
        }

        return true;
    }

    private async Task<bool> InstallStagedSceneAsync(SceneLoadSnapshot staged, IProject? project, SceneReplacement replacement)
    {
        var loadedScene = this.projectManager.AcceptSceneLoad(staged);
        var incoming = new SceneDocumentMetadata(loadedScene.Id) { Title = loadedScene.Name, IsSceneLoadPending = true };
        replacement.Incoming = incoming;
        var openedId = await this.documentService.OpenDocumentAsync(this.windowId, incoming, shouldSelect: false).ConfigureAwait(true);
        if (openedId == Guid.Empty)
        {
            this.projectManager.RetireScene(loadedScene);
            return false;
        }

        if (!await this.documentService.SelectDocumentAsync(this.windowId, loadedScene.Id).ConfigureAwait(true))
        {
            return false;
        }

        var installation = this.messenger.Send(new InstallSceneRequestMessage(this.windowId, loadedScene, incoming));
        if (!installation.HasReceivedResponse || !await installation.Response.ConfigureAwait(true)
            || !ReferenceEquals(project, this.projectManager.CurrentProject)
            || !this.documentService.GetOpenDocuments(this.windowId).Contains(incoming)
            || !ReferenceEquals(this.sceneEngineSync.GetDocumentScene(incoming), loadedScene))
        {
            return false;
        }

        replacement.Installed = true;
        incoming.IsSceneLoadPending = false;
        loadedScene.Project.ActiveScene = loadedScene;
        this.LogOpenedNewScene(loadedScene);
        await this.MarkSceneActivatedAsync(loadedScene).ConfigureAwait(true);
        return true;
    }

    private void OnOpenSceneRequested(object recipient, OpenSceneRequestMessage message)
        => message.Reply(this.OpenSceneAsync(message.Scene));

    private async Task<bool> OpenInspectionAsync(OpenCookedInspectionRequestMessage request)
    {
        if (this.windowId.Value == 0 || !ReferenceEquals(request.Project, this.projectContextService.ActiveProject))
        {
            return false;
        }

        var existing = this.documentService.GetOpenDocuments(this.windowId).OfType<CookedInspectionDocumentMetadata>()
            .FirstOrDefault(document => document.Project.ProjectId == request.Project.ProjectId
                && string.Equals(document.Project.ProjectRoot, request.Project.ProjectRoot, StringComparison.OrdinalIgnoreCase)
                && document.ScopeUri == request.ScopeUri && document.AssetUri == request.AssetUri
                && string.Equals(document.CookedSource?.RootFolderPath, request.CookedSource?.RootFolderPath, StringComparison.OrdinalIgnoreCase));
        if (existing is not null)
        {
            existing.RequestRefresh(request.Validate);
            return await this.documentService.SelectDocumentAsync(this.windowId, existing.DocumentId).ConfigureAwait(true);
        }

        var name = request.DisplayName ?? (request.ScopeUri is null ? request.Project.Name : Path.GetFileName(Uri.UnescapeDataString(request.ScopeUri.AbsolutePath).TrimEnd('/')));
        var metadata = new CookedInspectionDocumentMetadata(request.Project, request.ScopeUri, request.Validate)
        {
            AssetUri = request.AssetUri,
            CookedSource = request.CookedSource,
            Title = "Inspect · " + (string.IsNullOrEmpty(name) || string.Equals(name, "Cooked", StringComparison.OrdinalIgnoreCase) ? request.Project.Name : name),
        };
        return await this.documentService.OpenDocumentAsync(this.windowId, metadata).ConfigureAwait(true) != Guid.Empty;
    }

    private async void OnOpenMaterialRequested(object recipient, OpenMaterialRequestMessage message)
    {
        var opened = await this.OpenMaterialAsync(message.MaterialUri, message.Title).ConfigureAwait(true);
        message.Reply(opened);
    }

    private async void OnCreateMaterialRequested(object recipient, CreateMaterialRequestMessage message)
    {
        try
        {
            var created = await this.materialDocumentService.CreateAsync(message.MaterialUri).ConfigureAwait(true);
            await this.materialDocumentService.CloseAsync(created.DocumentId, discard: false).ConfigureAwait(true);
            _ = this.messenger.Send(new AssetsChangedMessage(message.MaterialUri));
            var opened = await this.OpenMaterialAsync(message.MaterialUri, message.Title).ConfigureAwait(true);
            message.Reply(opened);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.LogMaterialCreationFailed(ex, message.MaterialUri);
            message.Reply(response: false);
        }
    }

    private async Task MarkSceneActivatedAsync(World.Scene scene)
    {
        if (this.projectContextService.ActiveProject is not { } project
            || scene.Project.ProjectInfo.Id != project.ProjectId
            || !string.Equals(scene.Project.ProjectInfo.Location, project.ProjectRoot, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        try
        {
            await this.projectUsage.UpdateLastOpenedSceneAsync(project.Name, project.ProjectRoot, scene.Name).ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.LogSceneUsageUpdateFailed(ex, scene.Name, project.Name);
        }
    }

    private sealed class SceneReplacement
    {
        public SceneDocumentMetadata? Incoming { get; set; }

        public bool Installed { get; set; }

        public bool Retired { get; set; }
    }
}
