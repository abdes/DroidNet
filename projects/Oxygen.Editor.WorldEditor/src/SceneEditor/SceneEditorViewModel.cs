// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Config;
using DroidNet.Documents;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// ViewModel for the Scene Editor.
/// </summary>
public partial class SceneEditorViewModel : ObservableObject, IAsyncSaveable, IDocumentCloseParticipant, IDisposable
{
    private readonly IMessenger messenger;
    private readonly ILogger logger;
    private readonly ILoggerFactory? loggerFactory;
    private readonly IEngineService engineService;
    private readonly ISettingsService<IEngineStartupSettings>? engineSettings;
    private readonly IScenePreview scenePreview;
    private readonly IDocumentInputCommitter inputCommitter;
    private readonly IOperationResultPublisher operationResults;
    private readonly IStatusReducer statusReducer;
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
    /// <param name="windowId">The window identifier, for the window's camera messages.</param>
    /// <param name="engineService">Coordinates native engine usage for the document.</param>
    /// <param name="scenePreview">Reports when the engine shows the document's scene.</param>
    /// <param name="inputCommitter">Completes the focused inspector control's text input.</param>
    /// <param name="operationResults">The visible operation-result publisher.</param>
    /// <param name="statusReducer">The diagnostic status reducer.</param>
    /// <param name="contentPipelineService">The explicit content cooking service.</param>
    /// <param name="container">DI container used to create child services for viewports.</param>
    /// <param name="messenger">The messenger used for inter-component communication.</param>
    /// <param name="previewSettings">The shared project preview preferences.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    /// <param name="engineSettings">The engine startup settings, whose logging verbosity the editor edits; optional.</param>
    public SceneEditorViewModel(
        DocumentTab tab,
        IDocumentArea documentArea,
        WindowId windowId,
        IEngineService engineService,
        IScenePreview scenePreview,
        IDocumentInputCommitter inputCommitter,
        IOperationResultPublisher operationResults,
        IStatusReducer statusReducer,
        IContentPipelineService contentPipelineService,
        IContainer container,
        IMessenger messenger,
        Workspace.PreviewSettingsService previewSettings,
        ILoggerFactory? loggerFactory = null,
        ISettingsService<IEngineStartupSettings>? engineSettings = null)
    {
        this.engineService = engineService;
        this.engineSettings = engineSettings;
        this.PreviewSettings = previewSettings;

        // The scene editor owns the preview preferences: they are restored when it opens and reach the engine once it runs.
        _ = previewSettings.EnsureRestoredAsync();
        this.scenePreview = scenePreview;
        this.inputCommitter = inputCommitter;
        this.operationResults = operationResults;
        this.statusReducer = statusReducer;
        this.contentPipelineService = contentPipelineService;
        this.documentArea = documentArea;
        this.windowId = windowId;
        this.loggerFactory = loggerFactory;
        this.Viewports = [];
        this.Tab = tab ?? throw new ArgumentNullException(nameof(tab));
        this.Document = tab.Document;
        this.scene = (tab.Document.Content as SceneContent)?.Scene;
        this.container = container;
        this.messenger = messenger ?? throw new ArgumentNullException(nameof(messenger));
        this.logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger(nameof(SceneEditorViewModel));
        this.InitializeTransformTools();

        // The stored layout of the scene is restored with its viewports.
        this.CurrentLayout = SceneViewLayout.OnePane;

        // Defer creating viewports until the engine shows the document's scene: the preview's readiness triggers the
        // layout restoration, which avoids creating engine views before the scene exists ("frame context has no scene").
        this.RegisterMessages();
        this.Document.ContentReplaced += this.OnDocumentContentReplaced;
        this.InitializePlay();
        scenePreview.ReadyChanged += this.OnPreviewReadyChanged;
        if (this.scene is { } loaded && scenePreview.IsReady(this.Document.Id))
        {
            // The engine showed the scene before this editor existed.
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

    /// <summary>Gets or sets the native engine logging verbosity, an engine startup setting the running engine takes live.</summary>
    public int EngineLoggingVerbosity
    {
        get => this.engineSettings?.Settings.LoggingVerbosity ?? EngineConstants.DefaultLoggingVerbosity;
        set
        {
            if (this.engineSettings is { } settings && settings.Settings.LoggingVerbosity != value)
            {
                settings.Settings.LoggingVerbosity = value;
                this.OnPropertyChanged();
            }
        }
    }

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
            // A save that meets a newer file leaves the document changed on disk; the document area's banner offers the way out.
            _ = await this.SaveForCloseAsync().ConfigureAwait(true);
        }
    }

    /// <inheritdoc/>
    public async Task PrepareForCloseAsync()
    {
        await this.inputCommitter.CommitAsync().ConfigureAwait(true);
        await this.SaveViewportStateForCloseAsync().ConfigureAwait(true);
        await this.PreviewSettings.FlushAsync().ConfigureAwait(true);
        if (this.Authoring is { } authoring)
        {
            await authoring.FinishEditsAsync().ConfigureAwait(true);
        }

        this.isClosing = true;
        if (this.pendingSave is { } save)
        {
            _ = await save.ConfigureAwait(true);
        }
    }

    /// <inheritdoc/>
    public void ResumeEditing() => this.isClosing = false;

    /// <inheritdoc/>
    public Task CloseAsync(bool discard)
    {
        if (this.Document.IsDirty && !discard)
        {
            throw new InvalidOperationException("The scene has unsaved changes.");
        }

        // Closing the document disposes its content, which cancels the gestures still open and retires the authoring.
        return this.Document.History.IsBusy
            ? throw new InvalidOperationException("The scene changed while preparing to close. Review its changes before closing.")
            : Task.CompletedTask;
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

            this.scenePreview.ReadyChanged -= this.OnPreviewReadyChanged;
            this.LogUnregisteringFromMessages(this.Document.Id);

            this.messenger.UnregisterAll(this);
            this.DisposeTransformTools();
            this.DisposePlay();

            foreach (var viewport in this.Viewports)
            {
                this.ReleaseViewport(viewport);
            }

            this.Viewports.Clear();

            // The document's viewports are owned by its id; the engine drops whatever surface is left of them.
            _ = this.engineService.ReleaseOwnerSurfacesAsync(new SurfaceOwnerId(this.Document.Id.Value));
        }
    }

    private void RegisterMessages()
    {
        this.RegisterCameraMessages();
        this.RegisterSelectionMessages();
        this.RegisterShortcutMessages();
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
            var sceneUri = this.Document.File.Asset;
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
        await this.inputCommitter.CommitAsync().ConfigureAwait(true);
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
                AffectedScope = this.SceneScope(),
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
            AffectedScope = this.SceneScope(),
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
                    AffectedEntity = this.SceneScope(),
                },
            ],
        });
    }

    private AffectedScope SceneScope()
        => new()
        {
            SceneId = this.scene?.Id,
            SceneName = this.scene?.Name,
        };

    // A reload replaced the scene: the engine and the views follow the document to the new one, and the history was
    // cleared with it. The panes come back when the engine shows the new scene.
    private void OnDocumentContentReplaced(object? sender, DocumentContentReplacedEventArgs args)
    {
        if (this.isDisposed || args.Current is not SceneContent current)
        {
            return;
        }

        this.scene = current.Scene;
        this.sceneReady = false;
        this.RefreshPlayScene();
    }

    private void PublishCookResult(ContentCookResult result, AssetPath sceneUri)
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
                AssetId = sceneUri.ToUriString(),
                AssetVirtualPath = sceneUri.VirtualPath,
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
                SceneId = this.scene?.Id,
                SceneName = this.scene?.Name,
            },
            Diagnostics = [diagnostic],
        });
    }

    /// <summary>Builds the panes when the engine shows this document's scene and no restoration is under way.</summary>
    private void OnPreviewReadyChanged(object? sender, ScenePreviewEventArgs args)
    {
        _ = sender;
        if (!this.isDisposed && args.Document == this.Document.Id && !this.sceneReady && !this.viewportRestorePending
            && this.scenePreview.IsReady(args.Document) && this.scene is { } readyScene)
        {
            this.OnSceneReady(readyScene);
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

    // The authoring of the document's current content; a reload replaces it.
    private SceneAuthoring? Authoring => (this.Document.Content as SceneContent)?.Authoring;
}
