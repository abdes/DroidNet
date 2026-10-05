// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls;
using DroidNet.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;

namespace Oxygen.Editor.World.SceneExplorer;

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
        this.documentService.DocumentClosed += this.OnSceneDocumentClosed;
        this.messenger.Register<InstallSceneRequestMessage>(this, (_, message) =>
        {
            if (!this.isDisposed && message.WindowId == this.windowId && !message.HasReceivedResponse)
            {
                message.Reply(this.InstallAcceptedSceneAsync(message));
            }
        });
        this.projectClipboardSubscription = this.projectContexts?.ProjectChanged.Subscribe(new ClipboardProjectObserver(this));
        this.messenger.Register<SceneUnavailableMessage>(this, (_, message) =>
        {
            if (!this.isDisposed && message.WindowId == this.windowId)
            {
                this.HasUnavailableScene = true;
                this.CanReloadPreviousScene = message.CanReloadPrevious;
            }
        });
    }

    private async Task<bool> InstallAcceptedSceneAsync(InstallSceneRequestMessage request)
    {
        if (this.Scene is not null || !ReferenceEquals(this.projectManager.CurrentProject, request.Scene.Project)
            || !this.documentService.GetOpenDocuments(this.windowId).Contains(request.Metadata))
        {
            return false;
        }

        var ct = await this.BeginSceneLoadAsync().ConfigureAwait(true);
        this.loadingDocumentId = request.Scene.Id;
        try
        {
            if (!await this.InstallLoadedSceneAsync(request.Scene, request.Metadata, ct).ConfigureAwait(true))
            {
                return false;
            }

            request.Metadata.IsSceneLoadPending = false;
            this.NotifySelectionDependentCommands();
            this.HasUnavailableScene = false;
            this.CanReloadPreviousScene = false;
            return true;
        }
        finally
        {
            this.loadingDocumentId = Guid.Empty;
        }
    }

    private void OnSceneDocumentClosed(object? sender, DocumentClosedEventArgs args)
    {
        if (args.WindowId != this.windowId || args.Metadata is not SceneDocumentMetadata
            || this.Scene?.AttachedObject.Id != args.Metadata.DocumentId)
        {
            return;
        }

        this.loadSceneCts?.Cancel();
        this.selectionApplyGeneration++;
        _ = Interlocked.Increment(ref this.searchGeneration);
        this.selectionSyncDepth++;
        try
        {
            this.ClearRoot();
            this.projection.Clear();
            this.Scene = null;
            this.ClearCutMarks();
            this.ClipboardItemStore = [];
            this.FilterPredicate = null;
            this.SearchText = string.Empty;
            this.SearchResultCount = 0;
            this.searchExpandedFolderIds.Clear();
            this.searchExpandedNodeIds.Clear();
            this.interaction?.ClearActiveScene();
            _ = this.messenger.Send(new SceneAuthoringUnloadedMessage(args.Metadata.DocumentId));
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
        var request = this.messenger.Send(new ReloadPreviousSceneRequestMessage(this.windowId));
        if (request.HasReceivedResponse)
        {
            _ = await request.Response.ConfigureAwait(true);
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
                || !string.Equals(owner.clipboardProjectRoot, value?.ProjectRoot, StringComparison.OrdinalIgnoreCase)))
            {
                owner.InvalidateClipboard("The clipboard was cleared because the project changed.");
            }
        }
    }
}
