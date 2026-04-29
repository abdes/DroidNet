// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
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
using Oxygen.Core.Diagnostics;
using Oxygen.Core;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Interop;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// ViewModel for the Scene Editor.
/// </summary>
public partial class SceneEditorViewModel : ObservableObject, IAsyncSaveable, IDisposable
{
    // A small palette of candidate clear colors shared by viewports. We wrap the
    // palette here so the Scene Editor decides the per-viewport colors.
    private static readonly ColorManaged[] DefaultViewportClearColors = [
        new ColorManaged(0.10f, 0.12f, 0.15f, 1.0f), // default blue-ish
        new ColorManaged(0.18f, 0.09f, 0.09f, 1.0f), // warm
        new ColorManaged(0.09f, 0.18f, 0.09f, 1.0f), // green
        new ColorManaged(0.09f, 0.12f, 0.18f, 1.0f), // deep blue
        new ColorManaged(0.18f, 0.12f, 0.08f, 1.0f), // orange
        new ColorManaged(0.14f, 0.09f, 0.18f, 1.0f), // purple
    ];

    private readonly IMessenger messenger;
    private readonly ILogger logger;
    private readonly ILoggerFactory? loggerFactory;
    private readonly IEngineService engineService;
    private readonly IOperationResultPublisher operationResults;
    private readonly IStatusReducer statusReducer;
    private readonly ISceneDocumentCommandService commandService;
    private readonly IContentPipelineService contentPipelineService;
    private readonly IContentBrowserAssetProvider assetProvider;
    private readonly IDocumentService documentService;
    private readonly WindowId windowId;
    private readonly IContainer container;
    private IMenuSource? quickAddMenu;
    private SceneViewLayout? previousLayout;
    private Oxygen.Editor.World.Scene? scene;
    private bool sceneReady;

    /// <summary>
    /// Initializes a new instance of the <see cref="SceneEditorViewModel"/> class.
    /// </summary>
    /// <param name="metadata">The scene document metadata.</param>
    /// <param name="documentService">The document service.</param>
    /// <param name="windowId">The window identifier.</param>
    /// <param name="engineService">Coordinates native engine usage for the document.</param>
    /// <param name="container">DI container used to create child services for viewports.</param>
    /// <param name="messenger">The messenger used for inter-component communication.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    public SceneEditorViewModel(
        SceneDocumentMetadata metadata,
        IDocumentService documentService,
        WindowId windowId,
        IEngineService engineService,
        IOperationResultPublisher operationResults,
        IStatusReducer statusReducer,
        ISceneDocumentCommandService commandService,
        IContentPipelineService contentPipelineService,
        IContentBrowserAssetProvider assetProvider,
        IContainer container,
        IMessenger messenger,
        ILoggerFactory? loggerFactory = null)
    {
        this.engineService = engineService;
        this.operationResults = operationResults;
        this.statusReducer = statusReducer;
        this.commandService = commandService;
        this.contentPipelineService = contentPipelineService;
        this.assetProvider = assetProvider;
        this.documentService = documentService;
        this.windowId = windowId;
        this.loggerFactory = loggerFactory;
        this.Viewports = new ObservableCollection<ViewportViewModel>();
        this.Metadata = metadata;
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

        // Track mutations via the undo stack
        ((INotifyCollectionChanged)UndoRedo.GetHistory(metadata.DocumentId).UndoStack).CollectionChanged += this.OnUndoStackChanged;

        // RunAtFps is sourced directly from the engine service at runtime
        // (see property implementation). No constructor seeding required.
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Dispose(true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases resources used by the <see cref="SceneEditorViewModel"/>.
    /// </summary>
    /// <param name="disposing">True if called from Dispose, false if called from finalizer.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (disposing)
        {
            this.LogUnregisteringFromMessages(this.Metadata.DocumentId);

            ((INotifyCollectionChanged)UndoRedo.GetHistory(this.Metadata.DocumentId).UndoStack).CollectionChanged -= this.OnUndoStackChanged;
            this.messenger.UnregisterAll(this);

            foreach (var viewport in this.Viewports)
            {
                viewport.Dispose();
            }
            this.Viewports.Clear();
        }
    }

    private void RegisterMessages()
    {
        this.LogRegisteringForSceneLoaded(this.Metadata.DocumentId);
        this.messenger.Register<SceneLoadedMessage>(this, (r, m) => ((SceneEditorViewModel)r).OnSceneLoadedMessage(r, m));
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

    /// <summary>
    /// Gets or sets run rate target in frames per second for the editor preview.
    /// Always reads the current value from <see cref="IEngineService"/>.
    /// </summary>
    public int RunAtFps
    {
        get
        {
            try
            {
                var raw = (int)this.engineService.TargetFps;
                var max = (int)this.engineService.MaxTargetFps;
                return System.Math.Clamp(raw, 0, max);
            }
            catch (InvalidOperationException)
            {
                // Engine not created yet — return sensible default.
                return 60;
            }
        }

        set
        {
            try
            {
                // Clamp UI value to engine supported range before forwarding.
                var max = (int)this.engineService.MaxTargetFps;
                var clamped = System.Math.Clamp(value, 0, max);
                this.engineService.TargetFps = (uint)clamped;

                // Notify UI that the value may have changed (source of truth is the service).
                this.OnPropertyChanged(nameof(this.RunAtFps));
            }
            catch (InvalidOperationException ex)
            {
                this.LogFailedToSetEngineTargetFps(ex);
                this.PublishRuntimeSettingsFailure(
                    DiagnosticCodes.SettingsPrefix + "TARGET_FPS_REJECTED",
                    "Target FPS was not applied",
                    "The runtime rejected the target FPS setting.",
                    ex);
            }
        }
    }

    /// <summary>
    /// Gets minimum allowed native logging verbosity value for the engine (e.g. -9).
    /// </summary>
    public int MinLoggingVerbosity => EngineConstants.MinLoggingVerbosity;

    /// <summary>
    /// Gets maximum allowed native logging verbosity value for the engine (e.g. +9).
    /// </summary>
    public int MaxLoggingVerbosity => EngineConstants.MaxLoggingVerbosity;

    /// <summary>
    /// Gets or sets current native engine logging verbosity; sourced from the engine service.
    /// Setting writes to the native runtime through the service and is clamped
    /// to Min/Max.
    /// </summary>
    public int LoggingVerbosity
    {
        get
        {
            try
            {
                var raw = this.engineService.EngineLoggingVerbosity;
                return System.Math.Clamp(raw, this.MinLoggingVerbosity, this.MaxLoggingVerbosity);
            }
            catch (InvalidOperationException)
            {
                return 0;
            }
        }

        set
        {
            try
            {
                var clamped = System.Math.Clamp(value, this.MinLoggingVerbosity, this.MaxLoggingVerbosity);
                this.engineService.EngineLoggingVerbosity = clamped;
                this.OnPropertyChanged(nameof(this.LoggingVerbosity));
            }
            catch (Exception ex) when (ex is InvalidOperationException or ArgumentOutOfRangeException)
            {
                this.PublishRuntimeSettingsFailure(
                    DiagnosticCodes.SettingsPrefix + "LOGGING_VERBOSITY_REJECTED",
                    "Logging verbosity was not applied",
                    "The runtime rejected the logging verbosity setting.",
                    ex);
            }
        }
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

            // FIXME: (Debugging) Choose a color for this viewport deterministically using the viewport GUID.
            var paletteLen = DefaultViewportClearColors.Length;
            var preferred = (int)(((uint)viewport.ViewportId.GetHashCode()) % (uint)paletteLen);

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

            viewport.ClearColor = DefaultViewportClearColors[chosen];
            viewport.ToggleMaximizeCommand = new RelayCommand(() => this.ToggleMaximize(viewport));
            viewport.OnLayoutRequested = requestedLayout => this.ChangeLayoutCommand.Execute(requestedLayout);
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

    /// <summary>
    /// Marks the given viewport as focused and clears focus from all other viewports.
    /// </summary>
    /// <param name="viewport">The viewport to focus.</param>
    public void SetFocusedViewport(ViewportViewModel viewport)
    {
        if (viewport is null)
        {
            throw new ArgumentNullException(nameof(viewport));
        }

        this.FocusedViewportId = viewport.ViewportId;
        this.ApplyFocusedViewportFlags();
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
    private void ChangeLayout(SceneViewLayout layout)
    {
        this.CurrentLayout = layout;
    }

    [RelayCommand]
    private async Task Save()
    {
        await this.SaveAsync().ConfigureAwait(true);
    }

    [RelayCommand]
    private async Task CookCurrentSceneAsync()
    {
        if (this.scene is null)
        {
            this.PublishCookFailure("Scene is not loaded.");
            return;
        }

        try
        {
            var sceneUri = this.GetSceneAssetUri(this.scene);
            var result = await this.contentPipelineService.CookCurrentSceneAsync(this.scene, sceneUri, CancellationToken.None)
                .ConfigureAwait(true);
            result = await this.RefreshCatalogAfterCookAsync(result, sceneUri).ConfigureAwait(true);
            this.PublishCookResult(result, sceneUri);
            if (result.Validation?.Succeeded == true && result.Status is not OperationStatus.Failed)
            {
                _ = this.messenger.Send(new ValidatedCookedOutputMessage(GetValidatedCookedRoots(result)));
            }
        }
        catch (Exception ex)
        {
            this.PublishCookFailure(ex.Message, ex);
        }
    }

    /// <inheritdoc/>
    public async Task SaveAsync()
    {
        if (this.scene is null)
        {
            this.LogSaveRequestedButSceneNotReady();
            return;
        }

        this.LogSaveRequested();
        var result = await this.commandService.SaveSceneAsync(this.CreateCommandContext()).ConfigureAwait(true);
        if (result.Succeeded)
        {
            this.LogSaveSuccessful();
        }
        else
        {
            this.LogSaveFailed();
        }
    }

    private void OnUndoStackChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.Action == NotifyCollectionChangedAction.Add && !this.Metadata.IsDirty)
        {
            this.Metadata.IsDirty = true;
            _ = this.documentService.UpdateMetadataAsync(this.windowId, this.Metadata.DocumentId, this.Metadata);
        }
    }

    [RelayCommand]
    private void LocateInContentBrowser()
    {
        // TODO: Implement locate in content browser (publish a message / call service). For now log.
        this.LogLocateInContentBrowserRequested();
    }

    private Uri GetSceneAssetUri(Oxygen.Editor.World.Scene scene)
    {
        var mountName = scene.Project.ProjectInfo.AuthoringMounts.FirstOrDefault(
                mount => string.Equals(mount.Name, "Content", StringComparison.OrdinalIgnoreCase))
            ?.Name
            ?? scene.Project.ProjectInfo.AuthoringMounts.FirstOrDefault()?.Name
            ?? "Content";
        return new Uri($"{AssetUris.Scheme}:///{mountName}/Scenes/{scene.Name}.oscene.json");
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

    private async Task<ContentCookResult> RefreshCatalogAfterCookAsync(ContentCookResult result, Uri sceneUri)
    {
        if (result.Status == OperationStatus.Failed)
        {
            return result;
        }

        try
        {
            await this.assetProvider.RefreshAsync(AssetBrowserFilter.Default).ConfigureAwait(true);
            _ = this.messenger.Send(new AssetsChangedMessage(sceneUri));
            return result;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            var diagnostic = new DiagnosticRecord
            {
                OperationId = result.OperationId,
                Domain = FailureDomain.AssetIdentity,
                Severity = DiagnosticSeverity.Error,
                Code = AssetIdentityDiagnosticCodes.RefreshFailed,
                Message = "Asset catalog refresh failed after scene cook.",
                TechnicalMessage = ex.Message,
                ExceptionType = ex.GetType().FullName,
                AffectedEntity = new AffectedScope
                {
                    DocumentId = this.Metadata.DocumentId,
                    DocumentName = this.Metadata.Title,
                    AssetId = sceneUri.ToString(),
                    AssetVirtualPath = sceneUri.AbsolutePath,
                    SceneId = this.scene?.Id,
                    SceneName = this.scene?.Name,
                },
            };
            return result with
            {
                Status = result.Status == OperationStatus.Succeeded
                    ? OperationStatus.PartiallySucceeded
                    : result.Status,
                Diagnostics = [.. result.Diagnostics, diagnostic],
            };
        }
    }

    private static IReadOnlyList<string> GetValidatedCookedRoots(ContentCookResult result)
        => result.Validation?.CookedRoot
            .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Where(static root => !string.IsNullOrWhiteSpace(root))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList()
           ?? [];

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

    private void PublishRuntimeSettingsFailure(
        string code,
        string title,
        string message,
        Exception exception)
        => RuntimeOperationResults.PublishFailure(
            this.operationResults,
            this.statusReducer,
            RuntimeOperationKinds.SettingsApply,
            FailureDomain.Settings,
            code,
            title,
            message,
            new AffectedScope
            {
                DocumentId = this.Metadata.DocumentId,
                DocumentName = this.Metadata.Title,
            },
            exception: exception);
}
