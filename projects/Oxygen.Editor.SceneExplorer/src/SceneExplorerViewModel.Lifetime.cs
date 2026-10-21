// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Documents;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Views;
using Oxygen.Editor.World;

namespace Oxygen.Editor.SceneExplorer;

public partial class SceneExplorerViewModel
{
    private IDisposable? projectClipboardSubscription;

    /// <summary>Gets whether the accepted scene was retired and no replacement graph could be installed.</summary>
    [ObservableProperty]
    public partial bool HasUnavailableScene { get; private set; }

    /// <summary>Gets whether the previous saved scene is available for explicit recovery.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ReloadPreviousSceneCommand))]
    public partial bool CanReloadPreviousScene { get; private set; }

    private void RegisterSceneLifetime()
    {
        this.openDocuments.Opened += this.OnSceneDocumentOpened;
        this.openDocuments.Closed += this.OnSceneDocumentClosed;
        this.projectClipboardSubscription = this.projectContexts?.ProjectChanged.Subscribe(new ClipboardProjectObserver(this));
        if (this.availability is { } source)
        {
            source.SceneUnavailable += this.OnSceneUnavailable;
        }
    }

    private void OnSceneUnavailable(object? sender, SceneUnavailableEventArgs args)
    {
        _ = sender;
        if (!this.isDisposed)
        {
            this.HasUnavailableScene = true;
            this.CanReloadPreviousScene = args.CanReloadPrevious;
        }
    }

    // The explorer shows the scene document opened last: a scene that opens replaces the one shown.
    private void OnSceneDocumentOpened(object? sender, DocumentEventArgs args)
    {
        if (!this.isDisposed && args.Document.Content is SceneContent)
        {
            _ = this.InstallOpenedSceneAsync(args.Document);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Installing runs from a document event; a failure is logged and leaves the tree empty instead of terminating the editor.")]
    private async Task<bool> InstallOpenedSceneAsync(Document document)
    {
        var content = SceneContent.Of(document);
        var scene = content.Scene;
        if (this.projectContexts is { } projects && projects.ActiveProject?.ProjectId != content.Project.ProjectId)
        {
            return false;
        }

        if (this.sceneDocument is { } previous && !ReferenceEquals(previous, document))
        {
            this.ReleaseShownScene(previous);
        }

        var ct = await this.BeginSceneLoadAsync().ConfigureAwait(true);
        this.loadingDocumentId = document.Id;
        try
        {
            if (!await this.InstallLoadedSceneAsync(scene, document, ct).ConfigureAwait(true))
            {
                return false;
            }

            this.NotifySelectionDependentCommands();
            this.HasUnavailableScene = false;
            this.CanReloadPreviousScene = false;
            return true;
        }
        catch (OperationCanceledException)
        {
            return false;
        }
        catch (Exception exception)
        {
            this.LogAuthoringLoadFailed(exception, scene.Id, scene.Name);
            return false;
        }
        finally
        {
            if (this.loadingDocumentId == document.Id)
            {
                this.loadingDocumentId = DocumentId.Empty;
            }
        }
    }

    private void OnSceneDocumentClosed(object? sender, DocumentEventArgs args)
    {
        if (!ReferenceEquals(this.sceneDocument, args.Document))
        {
            return;
        }

        this.ReleaseShownScene(args.Document);

        // A replacement that did not commit closes the incoming scene: the one still open is shown again.
        if (!this.isDisposed && this.openDocuments.Documents.LastOrDefault(static document => document.Content is SceneContent) is { } remaining)
        {
            _ = this.InstallOpenedSceneAsync(remaining);
        }
    }

    // A reload replaced the shown document's scene: the tree shows the new one.
    private void OnShownContentReplaced(object? sender, DocumentContentReplacedEventArgs args)
    {
        if (!this.isDisposed && sender is Document document && ReferenceEquals(this.sceneDocument, document) && args.Current is SceneContent content)
        {
            _ = this.ReloadShownSceneAsync(document, content.Scene);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Reloading runs from a document event; a failure is logged and leaves the tree empty instead of terminating the editor.")]
    private async Task ReloadShownSceneAsync(Document document, Scene scene)
    {
        try
        {
            var ct = await this.BeginSceneLoadAsync().ConfigureAwait(true);
            this.loadingDocumentId = document.Id;
            this.nextEntityIndex = scene.AllNodes.Count();
            _ = await this.InstallLoadedSceneAsync(scene, document, ct).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception exception)
        {
            this.LogAuthoringLoadFailed(exception, scene.Id, scene.Name);
        }
        finally
        {
            if (this.loadingDocumentId == document.Id)
            {
                this.loadingDocumentId = DocumentId.Empty;
            }
        }
    }

    private void WatchSceneDocument(Document document)
    {
        if (ReferenceEquals(this.sceneDocument, document))
        {
            return;
        }

        if (this.sceneDocument is { } previous)
        {
            previous.ContentReplaced -= this.OnShownContentReplaced;
        }

        this.sceneDocument = document;
        document.ContentReplaced += this.OnShownContentReplaced;
    }

    private void ReleaseShownScene(Document closed)
    {
        closed.ContentReplaced -= this.OnShownContentReplaced;
        this.loadSceneCts?.Cancel();
        this.selectionApplyGeneration++;
        _ = Interlocked.Increment(ref this.searchGeneration);
        this.selectionSyncDepth++;
        try
        {
            this.ClearRoot();
            this.projection.Clear();
            this.Scene = null;
            this.sceneDocument = null;
            this.ClearCutMarks();
            this.ClipboardItemStore = [];
            this.FilterPredicate = null;
            this.SearchText = string.Empty;
            this.SearchResultCount = 0;
            this.searchExpandedFolderIds.Clear();
            this.searchExpandedNodeIds.Clear();
            this.interaction?.ClearActiveScene();
        }
        finally
        {
            this.selectionSyncDepth--;
        }

        this.NotifyCategoryAvailabilityChanged();
        this.OnPropertyChanged(nameof(this.CanChangeCategories));
        this.RefreshContextActions();
        this.NotifySelectionDependentCommands();
    }

    [RelayCommand(CanExecute = nameof(CanReloadPreviousScene))]
    private async Task ReloadPreviousSceneAsync()
    {
        if (this.availability is { } source)
        {
            _ = await source.ReloadPreviousAsync().ConfigureAwait(true);
        }
    }

    private sealed class ClipboardProjectObserver(SceneExplorerViewModel owner) : IObserver<ProjectContext?>
    {
        public void OnCompleted()
        {
        }

        public void OnError(Exception error)
        {
        }

        public void OnNext(ProjectContext? value)
        {
            if (owner.clipboardProjectId is { } origin && (origin != value?.ProjectId
                || owner.clipboardProjectRoot != value?.ProjectRoot))
            {
                owner.InvalidateClipboard("The clipboard was cleared because the project changed.");
            }
        }
    }
}
