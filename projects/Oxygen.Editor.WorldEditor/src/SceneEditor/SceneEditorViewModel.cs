// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using DroidNet.Controls.Menus;
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
using Oxygen.Editor.World.Components;
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
    // A small palette of candidate clear colors shared by viewports. We wrap the
    // palette here so the Scene Editor decides the per-viewport colors.
    private static readonly RuntimeColor[] DefaultViewportClearColors = [
        new RuntimeColor(0.10f, 0.12f, 0.15f, 1.0f), // default blue-ish
        new RuntimeColor(0.18f, 0.09f, 0.09f, 1.0f), // warm
        new RuntimeColor(0.09f, 0.18f, 0.09f, 1.0f), // green
        new RuntimeColor(0.09f, 0.12f, 0.18f, 1.0f), // deep blue
        new RuntimeColor(0.18f, 0.12f, 0.08f, 1.0f), // orange
        new RuntimeColor(0.14f, 0.09f, 0.18f, 1.0f), // purple
    ];

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
    private IMenuSource? quickAddMenu;
    private SceneViewLayout? previousLayout;
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

    /// <summary>
    /// Gets the command that changes the current layout (generated by [RelayCommand]).
    /// </summary>
    /// <remarks>The command property is generated by CommunityToolkit [RelayCommand] applied to ChangeLayout method.</remarks>

    /// <summary>
    /// Menu source used by the top Add MenuButton for quickly adding scene items.
    /// </summary>
    public IMenuSource QuickAddMenu => this.quickAddMenu ??= this.BuildQuickAddMenu();

    /// <summary>Gets the shared project preview preferences.</summary>
    public Workspace.PreviewSettingsService PreviewSettings { get; }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Marks the given viewport as focused and clears focus from all other viewports.
    /// </summary>
    /// <param name="viewport">The viewport to focus.</param>
    public void SetFocusedViewport(ViewportViewModel viewport)
    {
        ArgumentNullException.ThrowIfNull(viewport);

        this.FocusedViewportId = viewport.ViewportId;
        this.ApplyFocusedViewportFlags();
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
                viewport.Dispose();
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
    }

    private void RefreshCookInputRegistration()
    {
        this.cookInputRegistration?.Dispose();
        this.cookInputRegistration = this.scene is null || this.isDisposed
            ? null : this.cookInputs.Register(this.CreateCommandContext(), this.commandService);
    }

    partial void OnCurrentLayoutChanging(SceneViewLayout value)
    {
        // If the scene is not yet synchronized into the engine, defer
        // creating viewports/layout until `SceneLoadedMessage` arrives.
        if (!this.sceneReady)
        {
            this.LogDeferringLayoutChange(value);
            return;
        }

        this.UpdateLayout(value);
    }

    private void UpdateLayout(SceneViewLayout targetLayout)
    {
        var metadata = this.Metadata ?? throw new InvalidOperationException("Scene metadata is not initialized.");
        metadata.Layout = targetLayout;

        var placements = SceneLayoutHelpers.GetPlacements(targetLayout);
        var requiredCount = placements.Count;

        // Adjust viewports count
        while (this.Viewports.Count < requiredCount)
        {
            var settings = this.container.Resolve<ISettingsService<IAppearanceSettings>>();
            var viewport = new ViewportViewModel(
                metadata.DocumentId,
                this.engineService,
                this.operationResults,
                this.statusReducer,
                settings,
                this.loggerFactory);
            var newIndex = this.Viewports.Count;

            viewport.ClearColor = this.ChooseViewportClearColor(viewport.ViewportId);
            viewport.ToggleMaximizeCommand = new RelayCommand(() => this.ToggleMaximize(viewport));
            viewport.OnLayoutRequested = requestedLayout => this.ChangeLayoutCommand.Execute(requestedLayout);
            viewport.SceneCamerasProvider = this.GetSceneCameras;
            this.LogCreatingViewport(newIndex, viewport);
            this.Viewports.Add(viewport);
        }

        while (this.Viewports.Count > requiredCount)
        {
            this.Viewports.RemoveAt(this.Viewports.Count - 1);
        }

        // Update IsMaximized state and metadata for all viewports
        for (var i = 0; i < this.Viewports.Count; i++)
        {
            var viewport = this.Viewports[i];
            viewport.IsMaximized = targetLayout == SceneViewLayout.OnePane && this.previousLayout != null;

            // The first viewport is considered the main camera
            viewport.UpdateLayoutMetadata(i, i == 0);
            viewport.OnLayoutRequested = requestedLayout => this.ChangeLayoutCommand.Execute(requestedLayout);
        }

        this.EnsureFocusedViewportIsValid();
    }

    private IReadOnlyList<SceneCameraChoice> GetSceneCameras()
        => this.scene is null
            ? []
            : [.. this.scene.AllNodes
                .Where(node => node.Components.OfType<CameraComponent>().Any())
                .Select(node => new SceneCameraChoice(node.Id, node.Name))];

    private RuntimeColor ChooseViewportClearColor(Guid viewportId)
    {
        // FIXME: (Debugging) Choose a color for this viewport deterministically using the viewport GUID.
        var paletteLen = DefaultViewportClearColors.Length;
        var preferred = (int)(((uint)viewportId.GetHashCode()) % (uint)paletteLen);

        // Build a set of colors already assigned to current viewports so
        // we can avoid duplicates when possible.
        var used = new HashSet<int>(this.Viewports
            .Select(vm =>
            {
                // map existing color back to palette index; if not found, -1
                for (var idx = 0; idx < DefaultViewportClearColors.Length; idx++)
                {
                    var c = DefaultViewportClearColors[idx];
                    if (vm.ClearColor.R == c.R && vm.ClearColor.G == c.G && vm.ClearColor.B == c.B && vm.ClearColor.A == c.A)
                    {
                        return idx;
                    }
                }

                return -1;
            })
            .Where(i => i >= 0));

        var chosen = -1;
        for (var i = 0; i < paletteLen; i++)
        {
            var idx = (preferred + i) % paletteLen;
            if (!used.Contains(idx))
            {
                chosen = idx;
                break;
            }
        }

        if (chosen < 0)
        {
            chosen = preferred; // fall back to preferred if all are used
        }

        return DefaultViewportClearColors[chosen];
    }

    private void EnsureFocusedViewportIsValid()
    {
        if (this.Viewports.Count == 0)
        {
            this.FocusedViewportId = Guid.Empty;
            return;
        }

        var isValid = this.FocusedViewportId != Guid.Empty && this.Viewports.Any(v => v.ViewportId == this.FocusedViewportId);
        if (!isValid)
        {
            // Default focus to primary viewport (index 0).
            this.FocusedViewportId = this.Viewports[0].ViewportId;
        }

        this.ApplyFocusedViewportFlags();
    }

    private void ApplyFocusedViewportFlags()
    {
        var focusedId = this.FocusedViewportId;
        foreach (var viewport in this.Viewports)
        {
            viewport.IsFocused = focusedId != Guid.Empty && viewport.ViewportId == focusedId;
        }
    }

    private void ToggleMaximize(ViewportViewModel viewport)
    {
        if (this.CurrentLayout == SceneViewLayout.OnePane)
        {
            // Restore
            if (this.previousLayout != null)
            {
                this.CurrentLayout = this.previousLayout.Value;
                this.previousLayout = null;
            }
        }
        else
        {
            // Maximize — move the requested viewport into the first position
            // before we change the layout. This prevents the collapse path in
            // UpdateLayout from removing the intended viewport when the list
            // is truncated to a single viewport.
            var index = this.Viewports.IndexOf(viewport);
            if (index > 0)
            {
                this.Viewports.Move(index, 0);
            }

            this.previousLayout = this.CurrentLayout;
            this.CurrentLayout = SceneViewLayout.OnePane;
        }

        this.EnsureFocusedViewportIsValid();
    }

    [RelayCommand]
    private void ChangeLayout(SceneViewLayout layout) => this.CurrentLayout = layout;

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

    // TODO: Implement locate in content browser (publish a message / call service). For now log.
    [RelayCommand]
    private void LocateInContentBrowser() => this.LogLocateInContentBrowserRequested();

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

    private IMenuSource BuildQuickAddMenu()
    {
        var builder = new MenuBuilder(this.loggerFactory);

        // Shapes submenu
        _ = builder.AddSubmenu("Shapes", shapes =>
        {
            _ = shapes.AddMenuItem("Sphere", new AsyncRelayCommand(() => this.AddPrimitive("Sphere")));
            _ = shapes.AddMenuItem("Cube", new AsyncRelayCommand(() => this.AddPrimitive("Cube")));
            _ = shapes.AddMenuItem("Cylinder", new AsyncRelayCommand(() => this.AddPrimitive("Cylinder")));
            _ = shapes.AddMenuItem("Cone", new AsyncRelayCommand(() => this.AddPrimitive("Cone")));
            _ = shapes.AddMenuItem("Plane", new AsyncRelayCommand(() => this.AddPrimitive("Plane")));
            _ = shapes.AddMenuItem("Capsule", new AsyncRelayCommand(() => this.AddPrimitive("Capsule")));
            _ = shapes.AddMenuItem("IcoSphere", new AsyncRelayCommand(() => this.AddPrimitive("IcoSphere")));
            _ = shapes.AddMenuItem("Torus", new AsyncRelayCommand(() => this.AddPrimitive("Torus")));
            _ = shapes.AddMenuItem("Quad", new AsyncRelayCommand(() => this.AddPrimitive("Quad")));
            _ = shapes.AddMenuItem("SubdividedCube", new AsyncRelayCommand(() => this.AddPrimitive("SubdividedCube")));
        });

        _ = builder.AddSeparator();

        // Lights submenu
        _ = builder.AddSubmenu("Lights", lights =>
        {
            _ = lights.AddMenuItem("Directional Light", new AsyncRelayCommand(() => this.AddLight("Directional")));
            _ = lights.AddMenuItem("Point Light", new AsyncRelayCommand(() => this.AddLight("Point")));
            _ = lights.AddMenuItem("Spot Light", new AsyncRelayCommand(() => this.AddLight("Spot")));
        });

        return builder.Build();
    }

    [RelayCommand]
    private async Task AddPrimitive(string kind)
    {
        this.LogRequestToAddPrimitive(kind);

        if (this.scene is null)
        {
            this.LogSaveRequestedButSceneNotReady();
            return;
        }

        _ = await this.commandService.CreatePrimitiveAsync(this.CreateCommandContext(), kind).ConfigureAwait(true);
    }

    [RelayCommand]
    private async Task AddLight(string kind)
    {
        this.LogRequestToAddLight(kind);

        if (this.scene is null)
        {
            this.LogSaveRequestedButSceneNotReady();
            return;
        }

        _ = await this.commandService.CreateLightAsync(this.CreateCommandContext(), kind).ConfigureAwait(true);
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
            this.sceneReady = true;
            this.LogSceneLoadedReceived(this.CurrentLayout);

            // Rebuild layout now that scene is ready.
            this.UpdateLayout(this.CurrentLayout);
        }
    }

    private SceneDocumentCommandContext CreateCommandContext()
        => new(
            this.Metadata.DocumentId,
            this.Metadata,
            this.scene ?? throw new InvalidOperationException("Scene is not loaded."),
            UndoRedo.GetHistory(this.Metadata.DocumentId));
}
