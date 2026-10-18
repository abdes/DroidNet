// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.World.Documents;

/// <summary>
/// Opens the documents a window's open requests name, and places scenes: a window shows one scene at a time.
/// </summary>
/// <remarks>
/// Opening another scene replaces the open one in three steps: its close is prepared, which asks about unsaved edits;
/// the next scene opens; then the close commits. A cancelled prompt opens nothing, and a scene that does not open keeps
/// the current one open and shown. The engine preview, the Scene Explorer and the Inspector follow the open documents.
/// </remarks>
public sealed partial class DocumentManager : IDisposable
{
    private readonly ILogger logger;
    private readonly IDocumentArea documentArea;
    private readonly IMessenger messenger;
    private readonly IProjectContextService projectContextService;
    private readonly IProjectUsageService projectUsage;
    private readonly WindowId windowId;
    private readonly OpenDocuments documents;
    private readonly IAssetRelocationService relocation;

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "An open in flight releases the gate after Dispose; the gate holds no wait handle.")]
    private readonly SemaphoreSlim sceneReplacementGate = new(1, 1);
    private readonly Oxygen.Managed.Core.Diagnostics.IOperationResultPublisher? operationResults;
    private readonly Oxygen.Managed.Core.Diagnostics.IStatusReducer? statusReducer;
    private long sceneRequestId;
    private (Guid ProjectId, string ProjectRoot, Uri SceneUri)? previousSavedScene;
    private bool disposed;
    private DocumentFile? installing;

    /// <summary>Initializes a new instance of the <see cref="DocumentManager"/> class.</summary>
    /// <param name="documentArea">The document area of the window whose requests this manager answers.</param>
    /// <param name="messenger">The messenger that carries open requests and the window's scene messages.</param>
    /// <param name="projectContextService">The active project, which resolves assets to files.</param>
    /// <param name="projectUsage">Remembers the project's last opened scene.</param>
    /// <param name="windowId">The window, which the scene messages name.</param>
    /// <param name="documents">The open documents of the session.</param>
    /// <param name="relocation">The relocation workflow, whose deletes close open documents.</param>
    /// <param name="loggerFactory">The logger factory, if any.</param>
    /// <param name="operationResults">Publishes scene load failures, if present.</param>
    /// <param name="statusReducer">Reduces the published failures, if present.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "MA0147:Avoid async void method for delegate", Justification = "messenger handlers reply to the requester once the awaited open completes")]
    public DocumentManager(
        IDocumentArea documentArea,
        IMessenger messenger,
        IProjectContextService projectContextService,
        IProjectUsageService projectUsage,
        WindowId windowId,
        OpenDocuments documents,
        IAssetRelocationService relocation,
        ILoggerFactory? loggerFactory = null,
        Oxygen.Managed.Core.Diagnostics.IOperationResultPublisher? operationResults = null,
        Oxygen.Managed.Core.Diagnostics.IStatusReducer? statusReducer = null)
    {
        this.logger = loggerFactory?.CreateLogger<DocumentManager>() ?? NullLoggerFactory.Instance.CreateLogger<DocumentManager>();

        this.documentArea = documentArea;
        this.messenger = messenger;
        this.projectContextService = projectContextService;
        this.projectUsage = projectUsage;
        this.windowId = windowId;
        this.documents = documents;
        this.relocation = relocation;
        this.operationResults = operationResults;
        this.statusReducer = statusReducer;

        this.messenger.Register<OpenSceneRequestMessage>(this, this.OnOpenSceneRequested);
        this.messenger.Register<OpenMaterialRequestMessage>(this, this.OnOpenMaterialRequested);
        this.RegisterRelocationMessages();
        this.messenger.Register<ReloadPreviousSceneRequestMessage>(this, (_, message) =>
        {
            if (message.WindowId == this.windowId && !message.HasReceivedResponse)
            {
                message.Reply(this.ReloadPreviousSceneAsync());
            }
        });
    }

    public void Dispose()
    {
        this.disposed = true;
        _ = Interlocked.Increment(ref this.sceneRequestId);
        this.messenger.UnregisterAll(this);
        this.relocation.FilesChanged -= this.OnFilesChanged;
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Opens or activates a scene for this workspace: the scene in its document replaces the one this window shows,
    /// once that one closes.
    /// </summary>
    /// <param name="sceneUri">The scene's asset URI.</param>
    /// <param name="cancellationToken">Cancels reading the scene or the close guards before the previous scene closes.</param>
    /// <returns><see langword="true"/> when the scene document is open and selected.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Scene replacement keeps the current scene until the incoming one is read, and reports the unavailable state afterwards.")]
    public async Task<bool> OpenSceneAsync(Uri sceneUri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(sceneUri);
        this.LogOnOpenSceneRequested(sceneUri);

        if (this.windowId.Value == 0)
        {
            this.LogCannotOpenSceneWindowIdInvalid(sceneUri);
            return false;
        }

        if (this.projectContextService.ActiveProject is not { } project
            || project.FindByAssetUri(sceneUri) is not { MountKind: ProjectMountKind.Authoring, IsMountRoot: false } location)
        {
            this.PublishSceneLoadFailure(sceneUri, unavailable: false);
            return false;
        }

        var file = new DocumentFile(location.FilePath, location.AssetUri);

        // Showing the incoming scene's tab activates it; the request that shows it already owns its installation.
        if (this.installing is { } installing && installing.IsAt(file.Path))
        {
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
            return await this.ReplaceSceneAsync(file, project, requestId, replacement, cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return false;
        }
        catch (Exception exception)
        {
            this.LogSceneReplacementFailed(exception, file.Path);
            if (!replacement.Retired && replacement.Incoming is null)
            {
                this.PublishSceneLoadFailure(sceneUri, unavailable: false);
            }

            return false;
        }
        finally
        {
            await this.FinishSceneReplacementAsync(file, replacement).ConfigureAwait(true);
        }
    }

    /// <summary>Opens the document of a material and shows it in this window.</summary>
    /// <param name="materialUri">The material's asset URI.</param>
    /// <param name="cancellationToken">Cancels the open.</param>
    /// <returns>Whether the material's document is shown.</returns>
    public async Task<bool> OpenMaterialAsync(Uri materialUri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(materialUri);
        if (this.projectContextService.ActiveProject is not { } project
            || project.FindByAssetUri(materialUri) is not { MountKind: ProjectMountKind.Authoring, IsMountRoot: false } location)
        {
            return false;
        }

        var opened = await this.documents.OpenAsync(new DocumentFile(location.FilePath, location.AssetUri), cancellationToken).ConfigureAwait(true);
        if (opened.Document is not { } document)
        {
            this.LogDocumentNotOpened(location.FilePath, opened.Problem ?? "it could not be read");
            return false;
        }

        return await this.documentArea.ShowAsync(document).ConfigureAwait(true);
    }

    private async Task FinishSceneReplacementAsync(DocumentFile file, SceneReplacement replacement)
    {
        try
        {
            if (!replacement.Installed && replacement.Incoming is { } incoming)
            {
                // Closing the tab closes the document; a document that never got a tab is closed directly.
                if (this.documentArea.Documents.Contains(incoming))
                {
                    _ = await this.documentArea.CloseAsync(incoming, force: true).ConfigureAwait(true);
                }

                this.documents.Close(incoming);
            }
        }
        finally
        {
            _ = this.sceneReplacementGate.Release();

            if (!replacement.Installed && (replacement.Retired || replacement.Incoming is not null))
            {
                this.PublishSceneLoadFailure(file.AssetUri, unavailable: true);
            }
        }
    }

    private async Task<bool> ReplaceSceneAsync(DocumentFile file, ProjectContext project, long requestId, SceneReplacement replacement, CancellationToken cancellationToken)
    {
        if (this.disposed || requestId != Volatile.Read(ref this.sceneRequestId))
        {
            return false;
        }

        var openScenes = this.documentArea.Documents.Where(static document => document.Content is SceneContent).ToArray();
        var existing = openScenes.FirstOrDefault(document => document.File.IsAt(file.Path));
        if (existing is { Content: SceneContent current })
        {
            var selected = await this.documentArea.ShowAsync(existing).ConfigureAwait(true);
            if (selected)
            {
                await this.MarkSceneActivatedAsync(current).ConfigureAwait(true);
            }

            return selected;
        }

        // Ask about the current scene's unsaved edits first: a cancelled prompt opens nothing.
        var closing = openScenes.Length == 0 ? null : await this.documentArea.PrepareCloseAsync(openScenes).ConfigureAwait(true);
        Document document;
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if ((openScenes.Length != 0 && closing is null) || !this.IsCurrentRequest(project, requestId))
            {
                return false;
            }

            // Read the incoming scene before the current one closes: a file that does not open keeps the current scene.
            var opened = await this.documents.OpenAsync(file, cancellationToken).ConfigureAwait(true);
            if (opened.Document is not { Content: SceneContent } incoming)
            {
                this.LogSceneNotOpened(file.Path, opened.Problem ?? "it is not a scene");
                this.PublishSceneLoadFailure(file.AssetUri, unavailable: false, opened.Problem);
                return false;
            }

            if (opened.WasOpen)
            {
                this.LogSceneNotOpened(file.Path, "it is open in another window");
                this.PublishSceneLoadFailure(file.AssetUri, unavailable: false, "The scene is open in another window.");
                return false;
            }

            if (!this.IsCurrentRequest(project, requestId) || (closing is not null && !await closing.CommitAsync().ConfigureAwait(true)))
            {
                this.documents.Close(incoming);
                return false;
            }

            document = incoming;
        }
        finally
        {
            // The window takes no new tab while its close is prepared.
            closing?.Dispose();
        }

        if (openScenes.Length != 0)
        {
            this.previousSavedScene = (project.ProjectId, project.ProjectRoot, openScenes[^1].File.AssetUri);
            replacement.Retired = true;
        }

        return await this.InstallSceneAsync(document, project, replacement).ConfigureAwait(true);
    }

    private bool IsCurrentRequest(ProjectContext project, long requestId)
        => requestId == Volatile.Read(ref this.sceneRequestId) && ReferenceEquals(project, this.projectContextService.ActiveProject);

    private async Task<bool> InstallSceneAsync(Document document, ProjectContext project, SceneReplacement replacement)
    {
        var content = (SceneContent)document.Content;
        var scene = content.Scene;
        replacement.Incoming = document;
        this.installing = document.File;
        try
        {
            if (!await this.documentArea.ShowAsync(document).ConfigureAwait(true))
            {
                return false;
            }
        }
        finally
        {
            this.installing = null;
        }

        if (!ReferenceEquals(project, this.projectContextService.ActiveProject)
            || !this.documentArea.Documents.Contains(document)
            || this.documents.Find(document.Id) is null)
        {
            return false;
        }

        replacement.Installed = true;
        this.LogOpenedNewScene(scene);
        await this.MarkSceneActivatedAsync(content).ConfigureAwait(true);
        return true;
    }

    private void OnOpenSceneRequested(object recipient, OpenSceneRequestMessage message)
        => message.Reply(this.OpenSceneAsync(message.SceneUri));

    private async void OnOpenMaterialRequested(object recipient, OpenMaterialRequestMessage message)
        => message.Reply(await this.OpenMaterialAsync(message.MaterialUri).ConfigureAwait(true));

    private async Task MarkSceneActivatedAsync(SceneContent content)
    {
        if (this.projectContextService.ActiveProject is not { } project
            || content.Project.ProjectId != project.ProjectId
            || !string.Equals(content.Project.ProjectRoot, project.ProjectRoot, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        // The project remembers its last scene by identity: a name is unique only within its folder, and a rename keeps
        // the identity. The workspace still finds scenes remembered by name before.
        try
        {
            await this.projectUsage.UpdateLastOpenedSceneAsync(project.Name, project.ProjectRoot, content.Scene.Id.ToString()).ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.LogSceneUsageUpdateFailed(ex, content.Scene.Name, project.Name);
        }
    }

    private sealed class SceneReplacement
    {
        public Document? Incoming { get; set; }

        public bool Installed { get; set; }

        public bool Retired { get; set; }
    }
}
