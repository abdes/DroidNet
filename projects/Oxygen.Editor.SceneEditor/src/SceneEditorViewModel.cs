// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneEditor;

/// <summary>
/// ViewModel for the Scene Editor.
/// </summary>
/// <remarks>
/// The editor belongs to the project session. The window that shows its scene lends it the window's area, commands,
/// placement and asset reveal (<see cref="IDocumentWindowEditor"/>); without a window, what needs one does nothing.
/// </remarks>
public partial class SceneEditorViewModel : ObservableObject, IAsyncSaveable, IDocumentCloseParticipant, IDocumentWindowEditor, IDisposable
{
    private readonly ILogger logger;
    private readonly SceneEditorServices services;
    private DocumentWindowServices? window;
    private Oxygen.Editor.World.Scene? scene;
    private bool sceneReady;
    private bool isClosing;
    private bool isDisposed;
    private Task<bool>? pendingSave;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneEditorViewModel"/> class.
    /// </summary>
    /// <param name="tab">The scene document's tab.</param>
    /// <param name="services">The session's services that the scene editors use.</param>
    public SceneEditorViewModel(DocumentTab tab, SceneEditorServices services)
    {
        ArgumentNullException.ThrowIfNull(services);
        this.logger = services.LoggerFactory.CreateLogger<SceneEditorViewModel>();
        this.services = services;

        // The scene editor owns the preview preferences: they are restored when it opens and reach the engine once it runs.
        _ = services.PreviewSettings.EnsureRestoredAsync();
        this.Viewports = [];
        this.Tab = tab ?? throw new ArgumentNullException(nameof(tab));
        this.Document = tab.Document;
        this.scene = (tab.Document.Content as SceneContent)?.Scene;
        this.InitializeTransformTools();

        // The stored layout of the scene is restored with its viewports.
        this.CurrentLayout = SceneViewLayout.OnePane;

        // Defer creating viewports until the engine shows the document's scene: the preview's readiness triggers the
        // layout restoration, which avoids creating engine views before the scene exists ("frame context has no scene").
        this.RegisterSubscriptions();
        services.Views.Attach(this);
        this.Document.ContentReplaced += this.OnDocumentContentReplaced;
        this.InitializePlay();
        this.services.ScenePreview.ReadyChanged += this.OnPreviewReadyChanged;
        if (this.scene is { } loaded && this.services.ScenePreview.IsReady(this.Document.Id))
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
    public PreviewSettingsService PreviewSettings => this.services.PreviewSettings;

    /// <summary>Gets or sets the native engine logging verbosity, an engine startup setting the running engine takes live.</summary>
    public int EngineLoggingVerbosity
    {
        get => this.services.EngineSettings.Settings.LoggingVerbosity;
        set
        {
            if (this.services.EngineSettings.Settings.LoggingVerbosity != value)
            {
                this.services.EngineSettings.Settings.LoggingVerbosity = value;
                this.OnPropertyChanged();
            }
        }
    }

    /// <inheritdoc/>
    public void AttachWindow(DocumentWindowServices window)
    {
        ArgumentNullException.ThrowIfNull(window);
        this.window = window;
    }

    /// <inheritdoc/>
    public void DetachWindow() => this.window = null;

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
        await this.services.InputCommitter.CommitAsync(this.Document.Id).ConfigureAwait(true);
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

            this.services.ScenePreview.ReadyChanged -= this.OnPreviewReadyChanged;
            this.UnregisterSubscriptions();
            this.services.Views.Detach(this);
            this.DisposeTransformTools();
            this.DisposePlay();

            foreach (var viewport in this.Viewports)
            {
                this.ReleaseViewport(viewport);
            }

            this.Viewports.Clear();

            // The document's viewports are owned by its id; the engine drops whatever surface is left of them.
            _ = this.services.Engine.ReleaseOwnerSurfacesAsync(new SurfaceOwnerId(this.Document.Id.Value));
        }
    }

    private void RegisterSubscriptions()
        => this.SelectionService.SelectionChanged += this.OnSelectionServiceChanged;

    private void UnregisterSubscriptions()
        => this.SelectionService.SelectionChanged -= this.OnSelectionServiceChanged;

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

        if (!await this.SaveInputsAsync(SaveBeforeOperation.Cook).ConfigureAwait(true))
        {
            return;
        }

        try
        {
            var sceneUri = this.Document.File.Asset;
            var result = await this.services.ContentPipeline.CookCurrentSceneAsync(sceneUri, CancellationToken.None)
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
        await this.services.InputCommitter.CommitAsync(this.Document.Id).ConfigureAwait(true);
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
            this.services.OperationResults.Publish(new OperationResult
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
        this.services.OperationResults.Publish(new OperationResult
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
        this.services.OperationResults.Publish(new OperationResult
        {
            OperationId = result.OperationId,
            OperationKind = ContentPipelineOperationKinds.CookScene,
            Status = result.Status,
            Severity = result.Status == OperationStatus.Failed && diagnostics.Count == 0
                ? DiagnosticSeverity.Error
                : this.services.StatusReducer.ComputeSeverity(diagnostics),
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
        this.services.OperationResults.Publish(new OperationResult
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
            && this.services.ScenePreview.IsReady(args.Document) && this.scene is { } readyScene)
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
        this.UpdateSelectionOutline(this.SelectionService.GetContext(this.Document.Id));

        this.UpdateTransformGizmo();
    }

    // The authoring of the document's current content; a reload replaces it.
    private SceneAuthoring? Authoring => (this.Document.Content as SceneContent)?.Authoring;
}
