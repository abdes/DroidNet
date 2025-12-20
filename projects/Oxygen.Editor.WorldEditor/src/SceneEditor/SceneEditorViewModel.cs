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
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.WorldEditor.SceneEditor;
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
    private readonly IProjectManagerService projectManager;
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
    /// <param name="projectManager">The project manager service.</param>
    /// <param name="container">DI container used to create child services for viewports.</param>
    /// <param name="messenger">The messenger used for inter-component communication.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    public SceneEditorViewModel(
        SceneDocumentMetadata metadata,
        IDocumentService documentService,
        WindowId windowId,
        IEngineService engineService,
        IProjectManagerService projectManager,
        IContainer container,
        IMessenger messenger,
        ILoggerFactory? loggerFactory = null)
    {
        this.engineService = engineService;
        this.projectManager = projectManager;
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
        ((INotifyCollectionChanged)UndoRedo.Default[metadata.DocumentId].UndoStack).CollectionChanged += this.OnUndoStackChanged;

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

            ((INotifyCollectionChanged)UndoRedo.Default[this.Metadata.DocumentId].UndoStack).CollectionChanged -= this.OnUndoStackChanged;
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
            var raw = this.engineService.EngineLoggingVerbosity;
            return System.Math.Clamp(raw, this.MinLoggingVerbosity, this.MaxLoggingVerbosity);
        }

        set
        {
            var clamped = System.Math.Clamp(value, this.MinLoggingVerbosity, this.MaxLoggingVerbosity);
            this.engineService.EngineLoggingVerbosity = clamped;
            this.OnPropertyChanged(nameof(this.LoggingVerbosity));
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
            var viewport = new ViewportViewModel(metadata.DocumentId, this.engineService, settings, this.loggerFactory);
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

    /// <inheritdoc/>
    public async Task SaveAsync()
    {
        if (this.scene is null)
        {
            this.LogSaveRequestedButSceneNotReady();
            return;
        }

        this.LogSaveRequested();
        var success = await this.projectManager.SaveSceneAsync(this.scene).ConfigureAwait(true);
        if (success)
        {
            this.Metadata.IsDirty = false;
            _ = await this.documentService.UpdateMetadataAsync(this.windowId, this.Metadata.DocumentId, this.Metadata).ConfigureAwait(true);
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

    private IMenuSource BuildQuickAddMenu()
    {
        var builder = new MenuBuilder(this.loggerFactory);

        // Shapes submenu
        _ = builder.AddSubmenu("Shapes", shapes =>
        {
            _ = shapes.AddMenuItem("Sphere", new RelayCommand(() => this.AddPrimitive("Sphere")));
            _ = shapes.AddMenuItem("Cube", new RelayCommand(() => this.AddPrimitive("Cube")));
            _ = shapes.AddMenuItem("Cylinder", new RelayCommand(() => this.AddPrimitive("Cylinder")));
            _ = shapes.AddMenuItem("Cone", new RelayCommand(() => this.AddPrimitive("Cone")));
            _ = shapes.AddMenuItem("Plane", new RelayCommand(() => this.AddPrimitive("Plane")));
        });

        _ = builder.AddSeparator();

        // Lights submenu
        _ = builder.AddSubmenu("Lights", lights =>
        {
            _ = lights.AddMenuItem("Directional Light", new RelayCommand(() => this.AddLight("Directional")));
            _ = lights.AddMenuItem("Point Light", new RelayCommand(() => this.AddLight("Point")));
            _ = lights.AddMenuItem("Spot Light", new RelayCommand(() => this.AddLight("Spot")));
            _ = lights.AddMenuItem("Rect Light", new RelayCommand(() => this.AddLight("Rect")));
            _ = lights.AddMenuItem("Sky Light", new RelayCommand(() => this.AddLight("Sky")));
        });

        return builder.Build();
    }

    [RelayCommand]
    private void AddPrimitive(string kind)
    {
        this.LogRequestToAddPrimitive(kind);

        // TODO: Implement real add logic via document/engine APIs.
    }

    [RelayCommand]
    private void AddLight(string kind)
    {
        this.LogRequestToAddLight(kind);

        // TODO: Implement real add logic via document/engine APIs.
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
}
