// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.SceneEditor;
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
    private readonly SceneCookInputRegistrar cookInputs;
    private readonly IDocumentService documentService;
    private readonly WindowId windowId;
    private readonly IContainer container;
    private IDisposable? cookInputRegistration;
    private Oxygen.Editor.World.Scene? scene;
    private bool sceneReady;
    private bool isClosing;
    private bool isDisposed;
    private Task<bool>? pendingSave;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneEditorViewModel"/> class.
    /// </summary>
    /// <param name="metadata">The scene document metadata.</param>
    /// <param name="documentService">The document service.</param>
    /// <param name="windowId">The window identifier.</param>
    /// <param name="engineService">Coordinates native engine usage for the document.</param>
    /// <param name="sceneEngineSync">Tracks the scene's runtime projection lifetime.</param>
    /// <param name="inputCommitter">Completes the focused inspector control's text input.</param>
    /// <param name="operationResults">The visible operation-result publisher.</param>
    /// <param name="statusReducer">The diagnostic status reducer.</param>
    /// <param name="commandService">The scene authoring command service.</param>
    /// <param name="contentPipelineService">The explicit content cooking service.</param>
    /// <param name="container">DI container used to create child services for viewports.</param>
    /// <param name="messenger">The messenger used for inter-component communication.</param>
    /// <param name="cookInputs">Registers saved scene inputs for coordinated cooking.</param>
    /// <param name="previewSettings">The shared project preview preferences.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    /// <param name="conflictPrompt">Presents recovery after an ordinary Save conflict.</param>
    public SceneEditorViewModel(
        SceneDocumentMetadata metadata,
        IDocumentService documentService,
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
        SceneCookInputRegistrar cookInputs,
        Workspace.PreviewSettingsService previewSettings,
        ILoggerFactory? loggerFactory = null,
        IDocumentConflictPrompt? conflictPrompt = null)
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
        this.cookInputs = cookInputs;
        this.documentService = documentService;
        this.windowId = windowId;
        this.loggerFactory = loggerFactory;
        this.Viewports = [];
        this.Metadata = metadata;
        this.scene = sceneEngineSync.GetDocumentScene(metadata);
        this.container = container;
        this.messenger = messenger ?? throw new ArgumentNullException(nameof(messenger));
        this.logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger(nameof(SceneEditorViewModel));

        // Try to restore layout from metadata if present
        this.CurrentLayout = SceneViewLayout.OnePane;

        // Initially defer creating viewports/layout until the scene has been
        // synchronized into the engine. SceneLoadedMessage will trigger the
        // actual layout restoration. This avoids creating engine views before
        // the scene exists (prevents "frame context has no scene").
        this.RegisterMessages();
        this.RefreshCookInputRegistration();
    }

    /// <summary>
    /// Gets or sets the scene document metadata.
    /// </summary>
    [ObservableProperty]
    public partial SceneDocumentMetadata Metadata { get; set; }

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
                await this.conflictPrompt.ShowAsync(this.windowId, this.Metadata, this).ConfigureAwait(true);
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
            await this.commandService.CompleteEditSessionsAsync(this.CreateCommandContext(), commit: true).ConfigureAwait(true);
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
        if (this.Metadata.IsDirty && !discard)
        {
            throw new InvalidOperationException("The scene has unsaved changes.");
        }

        // Histories contain delegates over scene objects which must not survive a reload.
        if (this.scene is not null)
        {
            await this.commandService.CompleteEditSessionsAsync(this.CreateCommandContext(), commit: false).ConfigureAwait(true);
        }

        using var replacement = this.scene is null ? null
            : await SceneAuthoringGate.BeginReplacementAsync(this.scene, CancellationToken.None).ConfigureAwait(true);
        if (this.scene is not null && replacement is null)
        {
            throw new InvalidOperationException("A scene operation is still finishing. Try closing again.");
        }

        if ((this.Metadata.IsDirty && !discard) || UndoRedo.GetHistory(this.Metadata.DocumentId).IsBusy)
        {
            throw new InvalidOperationException("The scene changed while preparing to close. Review its changes before closing.");
        }

        this.sceneEngineSync.CloseDocument(this.Metadata);
        UndoRedo.GetHistory(this.Metadata.DocumentId).Clear();
        replacement?.Retire();
        if (this.scene is not null)
        {
            this.container.Resolve<Oxygen.Editor.Projects.IProjectManagerService>().RetireScene(this.scene);
        }
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
            this.cookInputRegistration?.Dispose();
            this.cookInputRegistration = null;
            if (this.scene is not null)
            {
                SceneAuthoringGate.Retire(this.scene);
            }

            this.sceneEngineSync.CloseDocument(this.Metadata);
            this.LogUnregisteringFromMessages(this.Metadata.DocumentId);

            this.messenger.UnregisterAll(this);

            foreach (var viewport in this.Viewports)
            {
                this.ReleaseViewport(viewport);
            }

            this.Viewports.Clear();
        }
    }

    private static Uri GetSceneAssetUri(Oxygen.Editor.World.Scene scene)
    {
        var mountName = scene.Project.ProjectInfo.AuthoringMounts.FirstOrDefault(
                mount => string.Equals(mount.Name, "Content", StringComparison.OrdinalIgnoreCase))
            ?.Name
            ?? scene.Project.ProjectInfo.AuthoringMounts.FirstOrDefault()?.Name
            ?? "Content";
        return new Uri($"{AssetUris.Scheme}:///{mountName}/Scenes/{scene.Name}.oscene.json");
    }

    private void RegisterMessages()
    {
        this.LogRegisteringForSceneLoaded(this.Metadata.DocumentId);
        this.messenger.Register<SceneAuthoringLoadedMessage>(this, (_, message) =>
        {
            if (ReferenceEquals(this.Metadata, message.Metadata))
            {
                this.scene = message.Scene;
                this.sceneReady = false;
                this.RefreshCookInputRegistration();
            }
        });
        this.messenger.Register<SceneLoadedMessage>(this, (r, m) => ((SceneEditorViewModel)r).OnSceneLoadedMessage(r, m));
        this.RegisterCameraMessages();
        this.RegisterSelectionMessages();
    }

    private void RefreshCookInputRegistration()
    {
        this.cookInputRegistration?.Dispose();
        this.cookInputRegistration = this.scene is null || this.isDisposed
            ? null : this.cookInputs.Register(this.CreateCommandContext(), this.commandService);
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
            var sceneUri = GetSceneAssetUri(this.scene);
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
            this.operationResults.Publish(new OperationResult
            {
                OperationId = Guid.NewGuid(),
                OperationKind = SceneOperationKinds.Save,
                Status = OperationStatus.Failed,
                Severity = DiagnosticSeverity.Error,
                Title = "Scene was not saved",
                Message = "The scene is still loading. Wait for it to finish before saving.",
                AffectedScope = new AffectedScope { DocumentId = this.Metadata.DocumentId, DocumentName = this.Metadata.Title },
                CompletedAt = DateTimeOffset.UtcNow,
            });
            return false;
        }

        this.LogSaveRequested();
        var result = await this.commandService.SaveSceneAsync(this.CreateCommandContext()).ConfigureAwait(true);
        this.HasSaveConflict = !result.Succeeded && (this.HasSaveConflict || result.IsConflict);
        if (result.Succeeded)
        {
            this.RefreshCookInputRegistration();
            this.LogSaveSuccessful();
        }
        else
        {
            this.LogSaveFailed();
        }

        return result.Succeeded && !result.HasUnsavedChanges;
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
                DocumentId = this.Metadata.DocumentId,
                DocumentName = this.Metadata.Title,
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
                DocumentId = this.Metadata.DocumentId,
                DocumentName = this.Metadata.Title,
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

        if (this.Metadata != null && msg.Scene.Id == this.Metadata.DocumentId)
        {
            this.scene = msg.Scene;
            this.sceneReady = false;
            this.LogSceneLoadedReceived(this.CurrentLayout);

            // Build the panes now that the scene is ready, as the user left them.
            _ = this.RestoreViewportsAsync();
            if (this.SelectionService is { } selection)
            {
                this.UpdateSelectionOutline(selection.GetContext(this.Metadata.DocumentId));
            }
        }
    }

    private SceneDocumentCommandContext CreateCommandContext()
        => new(
            this.Metadata.DocumentId,
            this.Metadata,
            this.scene ?? throw new InvalidOperationException("Scene is not loaded."),
            UndoRedo.GetHistory(this.Metadata.DocumentId));
}
