// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// ViewModel for the Scene Editor.
/// </summary>
public partial class SceneEditorViewModel : ObservableObject, IAsyncSaveable, IDocumentCloseParticipant, IDocumentConflictParticipant, IDisposable
{
    private readonly IMessenger messenger;
    private readonly ILogger logger;
    private readonly ILoggerFactory? loggerFactory;
    private readonly IEngineService engineService;
    private readonly ISceneEngineSync sceneEngineSync;
    private readonly IDocumentInputCommitter inputCommitter;
    private readonly IDocumentConflictPrompt? conflictPrompt;
    private readonly IOperationResultPublisher operationResults;
    private readonly IStatusReducer statusReducer;
    private readonly ISceneDocumentCommandService commandService;
    private readonly IContentPipelineService contentPipelineService;
    private readonly IDocumentArea documentArea;
    private readonly WindowId windowId;
    private readonly IContainer container;
    private Oxygen.Editor.World.Scene? scene;
    private bool sceneReady;
    private bool isClosing;
    private bool isDisposed;
    private Task<bool>? pendingSave;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneEditorViewModel"/> class.
    /// </summary>
    /// <param name="tab">The scene document's tab.</param>
    /// <param name="documentArea">The document area of the editor's window, which says whether the scene is active.</param>
    /// <param name="windowId">The window identifier, for the window's prompts, input and shortcuts.</param>
    /// <param name="engineService">Coordinates native engine usage for the document.</param>
    /// <param name="sceneEngineSync">Tracks the scene's runtime projection lifetime.</param>
    /// <param name="inputCommitter">Completes the focused inspector control's text input.</param>
    /// <param name="operationResults">The visible operation-result publisher.</param>
    /// <param name="statusReducer">The diagnostic status reducer.</param>
    /// <param name="commandService">The scene authoring command service.</param>
    /// <param name="contentPipelineService">The explicit content cooking service.</param>
    /// <param name="container">DI container used to create child services for viewports.</param>
    /// <param name="messenger">The messenger used for inter-component communication.</param>
    /// <param name="previewSettings">The shared project preview preferences.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    /// <param name="conflictPrompt">Presents recovery after an ordinary Save conflict.</param>
    /// <param name="catalog">Reports when the scene's file is removed or restored outside the editor; optional.</param>
    public SceneEditorViewModel(
        DocumentTab tab,
        IDocumentArea documentArea,
        WindowId windowId,
        IEngineService engineService,
        ISceneEngineSync sceneEngineSync,
        IDocumentInputCommitter inputCommitter,
        IOperationResultPublisher operationResults,
        IStatusReducer statusReducer,
        ISceneDocumentCommandService commandService,
        IContentPipelineService contentPipelineService,
        IContainer container,
        IMessenger messenger,
        Workspace.PreviewSettingsService previewSettings,
        ILoggerFactory? loggerFactory = null,
        IDocumentConflictPrompt? conflictPrompt = null,
        IAssetCatalog? catalog = null)
    {
        this.engineService = engineService;
        this.PreviewSettings = previewSettings;
        this.sceneEngineSync = sceneEngineSync;
        this.inputCommitter = inputCommitter;
        this.conflictPrompt = conflictPrompt;
        this.operationResults = operationResults;
        this.statusReducer = statusReducer;
        this.commandService = commandService;
        this.contentPipelineService = contentPipelineService;
        this.documentArea = documentArea;
        this.windowId = windowId;
        this.loggerFactory = loggerFactory;
        this.Viewports = [];
        this.Tab = tab ?? throw new ArgumentNullException(nameof(tab));
        this.Document = tab.Document;
        this.scene = sceneEngineSync.GetDocumentScene(tab.DocumentId) ?? (tab.Document.Content as SceneContent)?.Scene;
        this.container = container;
        this.messenger = messenger ?? throw new ArgumentNullException(nameof(messenger));
        this.logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger(nameof(SceneEditorViewModel));
        this.InitializeTransformTools();

        // The stored layout of the scene is restored with its viewports.
        this.CurrentLayout = SceneViewLayout.OnePane;

        // Initially defer creating viewports/layout until the scene has been
        // synchronized into the engine. SceneLoadedMessage, or the sync service's
        // own notification for this document, triggers the actual layout
        // restoration. This avoids creating engine views before the scene exists
        // (prevents "frame context has no scene").
        this.RegisterMessages();
        this.Document.ContentReplaced += this.OnDocumentContentReplaced;
        this.InitializePlay();
        sceneEngineSync.SceneSynchronized += this.OnSceneSynchronized;
        this.WatchSourcePresence(catalog);
        if (this.scene is { } loaded && sceneEngineSync.IsSceneSynchronized(loaded))
        {
            // The scene finished synchronizing before this editor existed.
            this.OnSceneReady(loaded);
        }
    }

    /// <summary>
    /// Gets the scene document's tab.
    /// </summary>
    public DocumentTab Tab { get; }

    /// <summary>Gets the scene document the editor edits.</summary>
    public Document Document { get; }

    /// <summary>
    /// Gets or sets the current layout of the viewports.
    /// </summary>
    [ObservableProperty]
    public partial SceneViewLayout CurrentLayout { get; set; }

    /// <summary>
    /// Gets or sets the currently focused viewport identifier.
    /// </summary>
    [ObservableProperty]
    public partial Guid FocusedViewportId { get; set; }

    /// <summary>
    /// Gets the collection of active viewports.
    /// </summary>
    public ObservableCollection<ViewportViewModel> Viewports { get; }

    /// <summary>Gets the shared project preview preferences.</summary>
    public Workspace.PreviewSettingsService PreviewSettings { get; }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc/>
    public async Task SaveAsync()
    {
        if (!this.isClosing && !this.isDisposed)
        {
            _ = await this.SaveForCloseAsync().ConfigureAwait(true);
            if (this.HasSaveConflict && !this.isClosing && !this.isDisposed && this.conflictPrompt is not null)
            {
                await this.conflictPrompt.ShowAsync(this.windowId, this.Tab, this).ConfigureAwait(true);
            }
        }
    }

    /// <inheritdoc/>
    public async Task PrepareForCloseAsync()
    {
        await this.inputCommitter.CommitAsync(this.windowId).ConfigureAwait(true);
        await this.SaveViewportStateForCloseAsync().ConfigureAwait(true);
        await this.PreviewSettings.FlushAsync().ConfigureAwait(true);
        if (this.scene is not null)
        {
            await this.commandService.CompleteEditSessionsAsync(this.scene, commit: true).ConfigureAwait(true);
        }

        this.isClosing = true;
        await this.pendingConflict.ConfigureAwait(true);
        if (this.pendingSave is { } save)
        {
            _ = await save.ConfigureAwait(true);
        }
    }

    /// <inheritdoc/>
    public void ResumeEditing() => this.isClosing = false;

    /// <inheritdoc/>
    public async Task CloseAsync(bool discard)
    {
        await this.pendingConflict.ConfigureAwait(true);
        if (this.Document.IsDirty && !discard)
        {
            throw new InvalidOperationException("The scene has unsaved changes.");
        }

        if (this.scene is not null)
        {
            await this.commandService.CompleteEditSessionsAsync(this.scene, commit: false).ConfigureAwait(true);
        }

        using var replacement = this.scene is null ? null
            : await SceneAuthoringGate.BeginReplacementAsync(this.scene, CancellationToken.None).ConfigureAwait(true);
        if (this.scene is not null && replacement is null)
        {
            throw new InvalidOperationException("A scene operation is still finishing. Try closing again.");
        }

        if ((this.Document.IsDirty && !discard) || this.Document.History.IsBusy)
        {
            throw new InvalidOperationException("The scene changed while preparing to close. Review its changes before closing.");
        }

        // The engine lets the scene go; the document area closes the document once its tab has closed.
        this.sceneEngineSync.CloseDocument(this.Document.Id);
        replacement?.Retire();
    }

    /// <inheritdoc/>
    public Task<bool> SaveForCloseAsync()
        => this.pendingSave is { IsCompleted: false } save ? save : this.pendingSave = this.SaveCoreAsync();

    /// <summary>
    /// Releases resources used by the <see cref="SceneEditorViewModel"/>.
    /// </summary>
    /// <param name="disposing">True if called from Dispose, false if called from finalizer.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (disposing)
        {
            this.isDisposed = true;
            this.Document.ContentReplaced -= this.OnDocumentContentReplaced;
            this.StopWatchingSourcePresence();
            this.DisposeRelocationParticipant();
            if (this.scene is not null)
            {
                SceneAuthoringGate.Retire(this.scene);
            }

            this.sceneEngineSync.SceneSynchronized -= this.OnSceneSynchronized;
            this.sceneEngineSync.CloseDocument(this.Document.Id);
            this.LogUnregisteringFromMessages(this.Document.Id);

            this.messenger.UnregisterAll(this);
            this.DisposeTransformTools();
            this.DisposePlay();

            foreach (var viewport in this.Viewports)
            {
                this.ReleaseViewport(viewport);
            }

            this.Viewports.Clear();
        }
    }

    private void RegisterMessages()
    {
        this.LogRegisteringForSceneLoaded(this.Document.Id);
        this.messenger.Register<SceneAuthoringLoadedMessage>(this, (_, message) =>
        {
            if (ReferenceEquals(this.Document, message.Document))
            {
                this.scene = message.Scene;
                this.sceneReady = false;
                this.RefreshPlayScene();
            }
        });
        this.messenger.Register<SceneLoadedMessage>(this, (r, m) => ((SceneEditorViewModel)r).OnSceneLoadedMessage(r, m));
        this.RegisterCameraMessages();
        this.RegisterSelectionMessages();
        this.RegisterShortcutMessages();
        this.RegisterRelocationMessages();
    }

    [RelayCommand]
    private async Task Save() => await this.SaveAsync().ConfigureAwait(true);

    [RelayCommand]
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "User-triggered cook failures must be published as operation results rather than escape the UI command.")]
    private async Task CookCurrentSceneAsync()
    {
        if (this.scene is null)
        {
            this.PublishCookFailure("Scene is not loaded.");
            return;
        }

        try
        {
            var sceneUri = this.Document.File.AssetUri;
            var result = await this.contentPipelineService.CookCurrentSceneAsync(sceneUri, CancellationToken.None)
                .ConfigureAwait(true);

            this.PublishCookResult(result, sceneUri);
        }
        catch (Exception ex)
        {
            this.PublishCookFailure(ex.Message, ex);
        }
    }

    private async Task<bool> SaveCoreAsync()
    {
        await this.pendingConflict.ConfigureAwait(true);
        await this.inputCommitter.CommitAsync(this.windowId).ConfigureAwait(true);
        await this.PreviewSettings.FlushAsync().ConfigureAwait(true);
        if (this.scene is null)
        {
            this.LogSaveRequestedButSceneNotReady();
            this.PublishSaveFailure("The scene is still loading. Wait for it to finish before saving.", "SAVE_FAILED");
            return false;
        }

        this.LogSaveRequested();
        DocumentSaveResult result;
        try
        {
            result = await this.Document.SaveAsync().ConfigureAwait(true);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or DroidNet.Storage.StorageException)
        {
            this.LogSaveFailed();
            this.PublishSaveFailure($"The scene could not be written: {exception.Message}", "SAVE_EXCEPTION");
            return false;
        }

        this.HasSaveConflict = result.Outcome == DocumentSaveOutcome.Conflict;
        if (!result.Succeeded)
        {
            this.LogSaveFailed();
            this.PublishSaveFailure(result.Message ?? "The scene was not saved.", result.Outcome == DocumentSaveOutcome.Conflict ? "Conflict" : "SAVE_FAILED");
            return false;
        }

        this.LogSaveSuccessful();
        if (this.Document.IsDirty)
        {
            this.operationResults.Publish(new OperationResult
            {
                OperationId = Guid.NewGuid(),
                OperationKind = SceneOperationKinds.Save,
                Status = OperationStatus.SucceededWithWarnings,
                Severity = DiagnosticSeverity.Warning,
                Title = "Scene snapshot saved",
                Message = "Saved; newer changes remain unsaved",
                AffectedScope = this.DocumentScope(),
                CompletedAt = DateTimeOffset.UtcNow,
            });
        }

        return !this.Document.IsDirty;
    }

    private void PublishSaveFailure(string message, string code)
    {
        var operationId = Guid.NewGuid();
        this.operationResults.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = SceneOperationKinds.Save,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = code == "Conflict" ? "Scene changed outside this document" : "Scene was not saved",
            Message = message,
            AffectedScope = this.DocumentScope(),
            CompletedAt = DateTimeOffset.UtcNow,
            Diagnostics =
            [
                new DiagnosticRecord
                {
                    OperationId = operationId,
                    Domain = FailureDomain.Document,
                    Severity = DiagnosticSeverity.Error,
                    Code = DiagnosticCodes.DocumentPrefix + code,
                    Message = message,
                    AffectedEntity = this.DocumentScope(),
                },
            ],
        });
    }

    private AffectedScope DocumentScope()
        => new()
        {
            DocumentId = this.Document.Id.Value,
            DocumentName = this.Document.Title,
            DocumentPath = this.Document.File.Path,
            SceneId = this.scene?.Id,
            SceneName = this.scene?.Name,
        };

    // A reload replaced the scene: the engine and the views show the new one, and the history was cleared with it.
    private void OnDocumentContentReplaced(object? sender, DocumentContentReplacedEventArgs args)
    {
        if (this.isDisposed || args.Current is not SceneContent current)
        {
            return;
        }

        var replacement = current.Scene;
        _ = this.sceneEngineSync.RegisterDocument(replacement, this.Document.Id);
        this.scene = replacement;
        this.sceneReady = false;
        this.HasSaveConflict = false;
        this.RefreshPlayScene();
        _ = this.messenger.Send(new SceneAuthoringLoadedMessage(replacement, this.Document));

        // The Scene Explorer answers when it shows this document; it shows the new scene, or the tab must be reopened.
        var reloaded = this.messenger.Send(new SceneReloadedMessage(replacement, this.Document, this.windowId));
        if (reloaded.HasReceivedResponse)
        {
            _ = this.ReportTreeRefreshAsync(reloaded.Response);
        }
    }

    private async Task ReportTreeRefreshAsync(Task<bool> refresh)
    {
        if (await refresh.ConfigureAwait(true) || this.isDisposed)
        {
            return;
        }

        this.LogTreeNotRefreshed();
        this.operationResults.Publish(new OperationResult
        {
            OperationId = Guid.NewGuid(),
            OperationKind = SceneOperationKinds.Reload,
            Status = OperationStatus.SucceededWithWarnings,
            Severity = DiagnosticSeverity.Warning,
            Title = "Scene tree not refreshed",
            Message = "Reloaded from disk. The scene tree could not be refreshed; reopen the tab to refresh its display.",
            AffectedScope = this.DocumentScope(),
            CompletedAt = DateTimeOffset.UtcNow,
        });
    }

    private void PublishCookResult(ContentCookResult result, Uri sceneUri)
    {
        var diagnostics = result.Diagnostics;
        this.operationResults.Publish(new OperationResult
        {
            OperationId = result.OperationId,
            OperationKind = ContentPipelineOperationKinds.CookScene,
            Status = result.Status,
            Severity = result.Status == OperationStatus.Failed && diagnostics.Count == 0
                ? DiagnosticSeverity.Error
                : this.statusReducer.ComputeSeverity(diagnostics),
            Title = "Cook Current Scene",
            Message = result.Status == OperationStatus.Failed
                ? $"Scene cook failed: {sceneUri}."
                : $"Cooked current scene to {result.Validation?.CookedRoot ?? result.Inspection?.CookedRoot ?? "(no cooked root)"}.",
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = new AffectedScope
            {
                DocumentId = this.Document.Id.Value,
                DocumentName = this.Document.Title,
                AssetId = sceneUri.ToString(),
                AssetVirtualPath = sceneUri.AbsolutePath,
                SceneId = this.scene?.Id,
                SceneName = this.scene?.Name,
            },
            Diagnostics = diagnostics,
        });
    }

    private void PublishCookFailure(string message, Exception? exception = null)
    {
        var operationId = Guid.NewGuid();
        var diagnostic = new DiagnosticRecord
        {
            OperationId = operationId,
            Domain = FailureDomain.ContentPipeline,
            Severity = DiagnosticSeverity.Error,
            Code = AssetCookDiagnosticCodes.CookFailed,
            Message = message,
            TechnicalMessage = exception?.Message,
            ExceptionType = exception?.GetType().FullName,
        };
        this.operationResults.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = ContentPipelineOperationKinds.CookScene,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = "Cook Current Scene",
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = new AffectedScope
            {
                DocumentId = this.Document.Id.Value,
                DocumentName = this.Document.Title,
                SceneId = this.scene?.Id,
                SceneName = this.scene?.Name,
            },
            Diagnostics = [diagnostic],
        });
    }

    private void OnSceneLoadedMessage(object? recipient, SceneLoadedMessage msg)
    {
        _ = recipient;

        if (msg?.Scene is null)
        {
            return;
        }

        if (ReferenceEquals(msg.Scene, this.scene))
        {
            this.OnSceneReady(msg.Scene);
        }
    }

    /// <summary>
    ///     Builds the panes when the sync service reports this document's scene synchronized and no restoration
    ///     is under way. The relayed <see cref="SceneLoadedMessage"/> is dropped while another document is active,
    ///     for example while tabs change during a scene switch, which would leave this editor without panes.
    /// </summary>
    private void OnSceneSynchronized(object? sender, SceneSynchronizationCompletedEventArgs args)
    {
        _ = sender;
        if (!this.isDisposed && args.DocumentId == this.Document.Id && !this.sceneReady
            && !this.viewportRestorePending)
        {
            this.OnSceneReady(args.Scene);
        }
    }

    private void OnSceneReady(Oxygen.Editor.World.Scene readyScene)
    {
        this.scene = readyScene;
        this.sceneReady = false;
        this.RefreshPlayScene();
        this.LogSceneLoadedReceived(this.CurrentLayout);

        // Build the panes now that the scene is ready, as the user left them.
        _ = this.RestoreViewportsAsync();
        if (this.SelectionService is { } selection)
        {
            this.UpdateSelectionOutline(selection.GetContext(this.Document.Id));
        }

        this.UpdateTransformGizmo();
    }

    private SceneDocumentCommandContext CreateCommandContext()
        => new(
            this.Document.Id,
            this.scene ?? throw new InvalidOperationException("Scene is not loaded."),
            this.Document.History);
}
