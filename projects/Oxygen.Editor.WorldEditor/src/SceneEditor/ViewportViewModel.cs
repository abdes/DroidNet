// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Globalization;
using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using DroidNet.Controls.Menus;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// ViewModel for the Viewport control, managing camera, shading, and menus.
/// </summary>
public partial class ViewportViewModel : ObservableObject, IDisposable
{
    private const string DegreeUnit = "\u00b0";
    private const string PerspectiveCameraModeGroup = "PerspectiveCameraMode";
    private const string OrthographicCameraGroup = "OrthographicCamera";
    private const string SceneCameraGroup = "SceneCamera";
    private const string PilotCameraText = "Pilot Camera";
    private const string AlignCameraText = "Align Selected Camera to View";
    private const string LockedCameraText = "Unlock the camera to move it.";
    private const string MovementSpeedText = "Movement Speed";
    private const string FieldOfViewText = "Field of View";
    private const string NearViewPlaneText = "Near View Plane";
    private const string FarViewPlaneText = "Far View Plane";

    private static readonly CameraType[] OrthographicCameraTypes =
    [
        CameraType.Top,
        CameraType.Bottom,
        CameraType.Left,
        CameraType.Right,
        CameraType.Front,
        CameraType.Back,
    ];

    private readonly ILogger logger;
    private readonly ISettingsService<IAppearanceSettings> appearanceSettings;
    private readonly IOperationResultPublisher operationResults;
    private readonly IStatusReducer statusReducer;
    private readonly ViewportCameraNumberBoxItemModel movementSpeedItem;
    private readonly ViewportCameraNumberBoxItemModel fieldOfViewItem;
    private readonly ViewportCameraNumberBoxItemModel nearViewPlaneItem;
    private readonly ViewportCameraNumberBoxItemModel farViewPlaneItem;
    private IMenuSource? cameraMenu;
    private CancellationTokenSource? pilotCommitDelay;
    private bool pilotPoseDirty;
    private IMenuSource? shadingMenu;
    private IMenuSource? layoutMenu;
    private DataTemplate? cameraNumberBoxItemTemplate;
    private ElementTheme? effectiveThemeOverride;
    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="ViewportViewModel"/> class.
    /// </summary>
    /// <param name="documentId">The owning document identifier.</param>
    /// <param name="engineService">The shared engine service.</param>
    /// <param name="operationResults">The host-level operation result publisher.</param>
    /// <param name="statusReducer">The shared operation status reducer.</param>
    /// <param name="appearanceSettings">
    ///     The <see cref="ISettingsService{IAppearanceSettings}" /> used to provide appearance and theme settings.
    ///     This service supplies the current theme and notifies the view model of changes.
    /// </param>
    /// <param name="loggerFactory">
    ///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    ///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    public ViewportViewModel(
        Guid documentId,
        IEngineService engineService,
        IOperationResultPublisher operationResults,
        IStatusReducer statusReducer,
        ISettingsService<IAppearanceSettings> appearanceSettings,
        ILoggerFactory? loggerFactory = null)
    {
        this.LoggerFactory = loggerFactory;
        this.logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger("Oxygen.Editor.LevelEditor.ViewportViewModel");

        this.DocumentId = documentId;
        this.EngineService = engineService;
        this.operationResults = operationResults;
        this.statusReducer = statusReducer;
        this.appearanceSettings = appearanceSettings;
        this.movementSpeedItem = this.CreateCameraNumberBoxModel(
            value: 1.0f,
            minimum: 1.0f,
            maximum: float.PositiveInfinity,
            unit: string.Empty,
            propertyName: nameof(this.MovementSpeed));
        this.fieldOfViewItem = this.CreateCameraNumberBoxModel(
            value: 90.0f,
            minimum: 0.0f,
            maximum: 180.0f,
            unit: DegreeUnit,
            propertyName: nameof(this.FieldOfViewDegrees));
        this.nearViewPlaneItem = this.CreateCameraNumberBoxModel(
            value: 0.1f,
            minimum: 0.0f,
            maximum: float.PositiveInfinity,
            unit: "m",
            propertyName: nameof(this.NearViewPlane),
            mask: "~.###");
        this.farViewPlaneItem = this.CreateCameraNumberBoxModel(
            value: 1000.0f,
            minimum: 0.0f,
            maximum: float.PositiveInfinity,
            unit: "m",
            propertyName: nameof(this.FarViewPlane),
            mask: "~.###");

        // Seed effective theme from settings and subscribe for changes.
        this.SetEffectiveTheme(this.appearanceSettings.Settings.AppThemeMode);
        this.appearanceSettings.PropertyChanged += this.AppearanceSettings_PropertyChanged;
        this.ToggleMaximizeCommand = new RelayCommand(() => this.IsMaximized = !this.IsMaximized);
        this.LogInitialized();
    }

    // Overlay view toggles
    [ObservableProperty]
    public partial bool ShowFps { get; set; }

    [ObservableProperty]
    public partial bool ShowStats { get; set; }

    [ObservableProperty]
    public partial bool ShowToolbar { get; set; }

    [ObservableProperty]
    public partial bool Stat1 { get; set; }

    [ObservableProperty]
    public partial bool Stat2 { get; set; }

    [ObservableProperty]
    public partial bool Stat3 { get; set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CameraMenuLabel))]
    public partial CameraType CameraType { get; set; } = CameraType.Perspective;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CameraMenuLabel))]
    [NotifyPropertyChangedFor(nameof(CameraControlModeLabel))]
    public partial CameraControlMode CameraControlMode { get; set; } = CameraControlMode.OrbitTurntable;

    /// <summary>
    /// Gets or sets the authored scene camera this viewport renders through, or <see langword="null"/>
    /// when it renders through its editor camera.
    /// </summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CameraMenuLabel))]
    public partial SceneCameraChoice? SceneCamera { get; set; }

    /// <summary>
    /// Gets or sets a value indicating whether navigation moves the scene camera this viewport looks through.
    /// </summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CameraMenuLabel))]
    public partial bool IsPilotingSceneCamera { get; set; }

    [ObservableProperty]
    public partial ShadingMode ShadingMode { get; set; } = ShadingMode.Wireframe;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(MaximizeGlyph))]
    public partial bool IsMaximized { get; set; }

    [ObservableProperty]
    public partial bool IsFocused { get; set; }

    /// <summary>
    /// Gets the document identifier owning this viewport.
    /// </summary>
    public Guid DocumentId { get; }

    /// <summary>
    /// Gets the unique viewport identifier.
    /// </summary>
    public Guid ViewportId { get; } = Guid.NewGuid();

    /// <summary>
    /// Gets or sets if the UI requested an engine view for this viewport, the engine-assigned
    /// identifier will be stored here so the view can be destroyed later during
    /// teardown. Managed and owned by the UI layer — engine surface/lease code is
    /// unaffected by this property.
    /// </summary>
    public RuntimeViewId AssignedViewId { get; set; } = RuntimeViewId.Invalid;

    /// <summary>Gets or sets the input target captured for this view creation.</summary>
    public RuntimeViewTarget? AssignedInputTarget { get; set; }

    /// <summary>
    /// Gets the engine service reference, enabling views to request surfaces.
    /// </summary>
    public IEngineService EngineService { get; }

    /// <summary>
    /// Gets the zero-based viewport index in the current layout.
    /// </summary>
    public int ViewportIndex { get; private set; }

    /// <summary>
    /// Gets a value indicating whether this viewport should be considered primary.
    /// </summary>
    public bool IsPrimaryViewport { get; private set; }

    /// <summary>
    /// Gets the menu source for viewport camera projection, movement, and lens settings.
    /// </summary>
    public IMenuSource CameraMenu => this.cameraMenu ??= this.BuildCameraMenu();

    /// <summary>
    /// Gets the display label for the combined camera menu button.
    /// </summary>
    public string CameraMenuLabel => this.SceneCamera is { } camera
        ? (this.IsPilotingSceneCamera ? $"Piloting {camera.Name}" : camera.Name)
        : (this.CameraType == CameraType.Perspective ? this.CameraControlModeLabel : this.CameraType.ToString());

    /// <summary>
    /// Gets or sets the source of the authored scene cameras listed in the camera menu.
    /// </summary>
    public Func<IReadOnlyList<SceneCameraChoice>>? SceneCamerasProvider { get; set; }

    /// <summary>
    /// Gets or sets the source of the selected scene camera, the target of "Align to View".
    /// </summary>
    public Func<SceneCameraChoice?>? SelectedCameraProvider { get; set; }

    /// <summary>
    /// Gets or sets whether a camera node is locked against edits; a locked camera cannot be piloted or aligned.
    /// </summary>
    public Func<Guid, bool>? CameraLockProvider { get; set; }

    /// <summary>
    /// Gets or sets the authoring commit for a camera pose read from this viewport: it records an
    /// undoable edit of the node and completes with whether it applied.
    /// </summary>
    public Func<Guid, RuntimeViewCameraPose, Task<bool>>? CameraPoseCommitter { get; set; }

    /// <summary>
    /// Gets or sets how long navigation must pause before a pilot gesture is committed.
    /// </summary>
    internal TimeSpan PilotCommitDelay { get; set; } = TimeSpan.FromMilliseconds(300);

    /// <summary>
    /// Gets the display label for the editor camera control mode.
    /// </summary>
    public string CameraControlModeLabel => this.CameraControlMode switch
    {
        CameraControlMode.OrbitTurntable => "Turntable",
        CameraControlMode.OrbitTrackball => "Trackball",
        CameraControlMode.Fly => "Fly",
        _ => "Camera",
    };

    /// <summary>
    /// Gets or sets the editor fly movement speed.
    /// </summary>
    public float MovementSpeed
    {
        get => this.movementSpeedItem.NumberValue;
        set => this.movementSpeedItem.NumberValue = value;
    }

    /// <summary>
    /// Gets or sets the editor camera field of view in degrees.
    /// </summary>
    public float FieldOfViewDegrees
    {
        get => this.fieldOfViewItem.NumberValue;
        set => this.fieldOfViewItem.NumberValue = value;
    }

    /// <summary>
    /// Gets or sets the editor camera near view plane in meters.
    /// </summary>
    public float NearViewPlane
    {
        get => this.nearViewPlaneItem.NumberValue;
        set => this.nearViewPlaneItem.NumberValue = value;
    }

    /// <summary>
    /// Gets or sets the editor camera far view plane in meters.
    /// </summary>
    public float FarViewPlane
    {
        get => this.farViewPlaneItem.NumberValue;
        set => this.farViewPlaneItem.NumberValue = value;
    }

    /// <summary>
    /// Gets the menu source for the Shading menu. Built lazily on first access.
    /// </summary>
    public IMenuSource ShadingMenu => this.shadingMenu ??= this.BuildShadingMenu();

    /// <summary>
    /// Gets or sets the command to toggle maximize state.
    /// </summary>
    public ICommand? ToggleMaximizeCommand { get; set; }

    /// <summary>
    /// Gets the neutral clear color of the engine view, shared by every pane.
    /// </summary>
    public RuntimeColor ClearColor { get; } = new(0.1f, 0.12f, 0.15f, 1.0f);

    /// <summary>
    /// Gets the menu source for the Layout menu. Built lazily on first access.
    /// </summary>
    public IMenuSource LayoutMenu => this.layoutMenu ??= this.BuildLayoutMenu();

    /// <summary>
    /// Gets or sets callback invoked when a layout is requested from the viewport's layout menu.
    /// </summary>
    public Action<SceneViewLayout>? OnLayoutRequested { get; set; }

    /// <summary>
    /// Gets the glyph for the maximize/restore button.
    /// </summary>
    public string MaximizeGlyph => this.IsMaximized ? "\uE923" : "\uE922";

    /// <summary>
    /// Gets the logger factory.
    /// </summary>
    public ILoggerFactory? LoggerFactory { get; }

    /// <summary>
    /// Dispose of transient subscriptions.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Builds the surface request describing this viewport.
    /// </summary>
    /// <param name="tag">Optional diagnostic tag.</param>
    /// <returns>The surface request payload.</returns>
    public ViewportSurfaceRequest CreateSurfaceRequest(string? tag = null)
        => new()
        {
            DocumentId = this.DocumentId,
            ViewportId = this.ViewportId,
            ViewportIndex = this.ViewportIndex,
            IsPrimary = this.IsPrimaryViewport,
            Tag = tag,
        };

    /// <summary>
    /// Set the effective theme used for selecting themed resources (Light/Dark).
    /// Call this from the view (code-behind) using the view's ActualTheme.
    /// </summary>
    /// <param name="theme">The theme reported by the view (ActualTheme).</param>
    public void UpdateTheme(ElementTheme theme) => this.SetEffectiveTheme(theme);

    /// <summary>
    /// Applies the view-owned template used to render camera menu NumberBox rows.
    /// </summary>
    /// <param name="numberBoxItemTemplate">The template that renders <see cref="ViewportCameraNumberBoxItemModel"/> instances.</param>
    public void ApplyCameraMenuInteractiveContentTemplate(DataTemplate numberBoxItemTemplate)
    {
        ArgumentNullException.ThrowIfNull(numberBoxItemTemplate);

        this.cameraNumberBoxItemTemplate = numberBoxItemTemplate;
        if (this.cameraMenu is not null)
        {
            this.cameraMenu = this.BuildCameraMenu();
            this.OnPropertyChanged(nameof(this.CameraMenu));
        }
    }

    /// <summary>
    /// Updates the layout metadata for the viewport, setting its index and primary status.
    /// </summary>
    /// <param name="index">The zero-based index of the viewport in the current layout.</param>
    /// <param name="isPrimary">True if this viewport is the primary viewport; otherwise, false.</param>
    internal void UpdateLayoutMetadata(int index, bool isPrimary)
    {
        this.ViewportIndex = index;
        this.IsPrimaryViewport = isPrimary;
    }

    /// <summary>
    /// Publishes a runtime failure scoped to this viewport.
    /// </summary>
    /// <param name="operationKind">The failed operation kind.</param>
    /// <param name="domain">The failure domain.</param>
    /// <param name="code">The diagnostic code.</param>
    /// <param name="title">The user-facing failure title.</param>
    /// <param name="message">The user-facing failure message.</param>
    /// <param name="exception">Optional exception details.</param>
    internal void PublishRuntimeFailure(
        string operationKind,
        FailureDomain domain,
        string code,
        string title,
        string message,
        Exception? exception = null)
        => RuntimeOperationResults.PublishFailure(
            this.operationResults,
            this.statusReducer,
            operationKind,
            domain,
            code,
            title,
            message,
            this.CreateAffectedScope(),
            exception: exception,
            technicalMessage: this.CreateViewportTechnicalMessage(exception));

    /// <summary>
    /// Publishes a runtime warning scoped to this viewport.
    /// </summary>
    /// <param name="operationKind">The warning operation kind.</param>
    /// <param name="domain">The warning domain.</param>
    /// <param name="code">The diagnostic code.</param>
    /// <param name="title">The user-facing warning title.</param>
    /// <param name="message">The user-facing warning message.</param>
    /// <param name="exception">Optional exception details.</param>
    internal void PublishRuntimeWarning(
        string operationKind,
        FailureDomain domain,
        string code,
        string title,
        string message,
        Exception? exception = null)
        => RuntimeOperationResults.PublishWarning(
            this.operationResults,
            this.statusReducer,
            operationKind,
            domain,
            code,
            title,
            message,
            this.CreateAffectedScope(),
            exception: exception,
            technicalMessage: this.CreateViewportTechnicalMessage(exception));

    /// <summary>
    /// Applies the currently selected editor camera control mode to the native view, when available.
    /// </summary>
    /// <returns>A task that completes when the mode has been submitted.</returns>
    /// <summary>
    /// Rebuilds the camera menu from the current scene cameras. A selected scene camera that
    /// no longer exists is dropped in favor of the editor camera.
    /// </summary>
    internal void RefreshCameraMenu()
    {
        if (this.SceneCamera is { } current)
        {
            var match = this.GetSceneCameras().FirstOrDefault(camera => camera.NodeId == current.NodeId);
            if (match is null)
            {
                _ = this.ApplySceneCameraAsync(camera: null);
                return;
            }

            this.SceneCamera = match;
        }

        this.RebuildCameraMenu();
    }

    /// <summary>
    /// Re-sends the selected scene camera and pilot state, for example after the runtime view was recreated.
    /// </summary>
    /// <returns>A task that completes when the request was handled.</returns>
    internal async Task ApplyCurrentSceneCameraAsync()
    {
        if (this.SceneCamera is not { } camera || !this.AssignedViewId.IsValid)
        {
            return;
        }

        _ = await this.SendSceneCameraAsync(camera).ConfigureAwait(true);
        if (this.IsPilotingSceneCamera)
        {
            _ = await this.SendScenePilotAsync(pilot: true).ConfigureAwait(true);
        }
    }

    /// <summary>Renders this viewport through a scene camera.</summary>
    /// <param name="camera">The camera to look through.</param>
    /// <returns>A task that completes when the request was handled.</returns>
    internal Task LookThroughCameraAsync(SceneCameraChoice camera) => this.ApplySceneCameraAsync(camera);

    /// <summary>Returns this viewport to its editor camera.</summary>
    /// <returns>A task that completes when the request was handled.</returns>
    internal Task ReturnToEditorCameraAsync() => this.ApplySceneCameraAsync(camera: null);

    /// <summary>Looks through a scene camera and pilots it: navigation moves the camera.</summary>
    /// <param name="camera">The camera to pilot.</param>
    /// <returns>A task that completes when the request was handled.</returns>
    internal async Task PilotCameraAsync(SceneCameraChoice camera)
    {
        if (this.SceneCamera?.NodeId != camera.NodeId)
        {
            await this.ApplySceneCameraAsync(camera).ConfigureAwait(true);
        }

        await this.SetPilotAsync(pilot: true).ConfigureAwait(true);
    }

    /// <summary>Stops piloting, committing the last gesture first; the viewport keeps looking through the camera.</summary>
    /// <returns>A task that completes when the request was handled.</returns>
    internal Task StopPilotingAsync() => this.SetPilotAsync(pilot: false);

    /// <summary>
    /// Moves a scene camera to this viewport's editor camera, as one undoable edit.
    /// </summary>
    /// <param name="camera">The camera to move.</param>
    /// <returns><see langword="true"/> when the camera was moved.</returns>
    internal async Task<bool> AlignCameraToViewAsync(SceneCameraChoice camera)
    {
        if (this.SceneCamera is not null || !this.AssignedViewId.IsValid || this.IsLocked(camera.NodeId))
        {
            // Only the editor camera's pose is visible to the user as "the view".
            return false;
        }

        return await this.CommitCameraPoseAsync(camera.NodeId).ConfigureAwait(true);
    }

    /// <summary>Moves the selected camera to this viewport's editor camera, as one undoable edit.</summary>
    /// <returns>A task that completes when the request was handled.</returns>
    internal async Task AlignSelectedCameraToViewAsync()
    {
        if (this.SelectedCameraProvider?.Invoke() is { } camera)
        {
            _ = await this.AlignCameraToViewAsync(camera).ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Reports navigation input in this viewport. While piloting, a gesture is committed as one
    /// undoable camera edit once no button or key is held and input has paused.
    /// </summary>
    /// <param name="inputHeld">Whether a mouse button or key is still held.</param>
    internal void NotifyNavigationInput(bool inputHeld)
    {
        if (!this.IsPilotingSceneCamera)
        {
            return;
        }

        this.pilotPoseDirty = true;
        this.CancelPilotCommit();
        if (inputHeld)
        {
            return;
        }

        this.pilotCommitDelay = new CancellationTokenSource();
        _ = this.CommitPilotAfterPauseAsync(this.pilotCommitDelay.Token);
    }

    internal async Task ApplyCurrentCameraControlModeAsync()
        => await this.ApplyCameraControlModeAsync(this.CameraControlMode).ConfigureAwait(true);

    /// <summary>
    /// Applies the current editor camera numeric settings to the native view, when available.
    /// </summary>
    /// <returns>A task that completes when the settings have been submitted.</returns>
    internal async Task ApplyCurrentCameraSettingsAsync()
    {
        await this.ApplyCameraMovementSpeedAsync(this.MovementSpeed).ConfigureAwait(true);
        await this.ApplyCameraSettingsAsync().ConfigureAwait(true);
    }

    /// <summary>
    /// Protected dispose pattern implementation.
    /// </summary>
    /// <param name="disposing">True if called from Dispose; false if called from finalizer.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (!this.isDisposed)
        {
            if (disposing)
            {
                this.appearanceSettings.PropertyChanged -= this.AppearanceSettings_PropertyChanged;
                this.CancelPilotCommit();
            }

            this.isDisposed = true;
        }
    }

    private static MenuItemData CreateToggleMenuItem(string text, Func<bool> getter, Action<bool> setter, string? accelerator = null)
        => new()
        {
            Text = text,
            IsCheckable = true,
            IsChecked = getter(),
            AcceleratorText = accelerator,
            Command = new RelayCommand<MenuItemData?>(item =>
            {
                if (item is null)
                {
                    return;
                }

                setter(item.IsChecked);
            }),
        };

    private ViewportCameraNumberBoxItemModel CreateCameraNumberBoxModel(
        float value,
        float minimum,
        float maximum,
        string unit,
        string propertyName,
        string mask = "~.##")
        => new(
            value,
            minimum,
            maximum,
            unit,
            mask,
            onNumberValueChanged: changedValue => this.OnCameraNumberBoxValueChanged(propertyName, changedValue));

    partial void OnIsMaximizedChanged(bool oldValue, bool newValue)
    {
        // keep MaximizeGlyph synched with IsMaximized
        this.OnPropertyChanged(nameof(this.MaximizeGlyph));
    }

    private void SetEffectiveTheme(ElementTheme theme)
    {
        var hadCameraMenu = this.cameraMenu is not null;
        var hadShadingMenu = this.shadingMenu is not null;
        var hadLayoutMenu = this.layoutMenu is not null;

        this.effectiveThemeOverride = theme;
        this.LogEffectiveThemeSet(theme);

        try
        {
            if (hadShadingMenu)
            {
                this.shadingMenu = this.BuildShadingMenu();
                this.OnPropertyChanged(nameof(this.ShadingMenu));
            }

            if (hadCameraMenu)
            {
                this.cameraMenu = this.BuildCameraMenu();
                this.OnPropertyChanged(nameof(this.CameraMenu));
            }

            if (hadLayoutMenu)
            {
                this.layoutMenu = this.BuildLayoutMenu();
                this.OnPropertyChanged(nameof(this.LayoutMenu));
            }
        }
        catch (Exception ex) when (Oxygen.Editor.World.Services.EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogMenuRebuildFailed(ex);
        }
    }

    private void AppearanceSettings_PropertyChanged(object? sender, PropertyChangedEventArgs? e)
    {
        if (string.Equals(e?.PropertyName, nameof(IAppearanceSettings.AppThemeMode), StringComparison.Ordinal))
        {
            var theme = this.appearanceSettings.Settings.AppThemeMode;
            this.SetEffectiveTheme(theme);
        }
    }

    private IconSource? ResolveIcon(string name)
    {
        // The settings service seeds the VM's effective theme; assert it's present.
        if (string.IsNullOrWhiteSpace(name))
        {
            this.LogResolveIconEmptyName();
            return null;
        }

        if (!this.effectiveThemeOverride.HasValue)
        {
            this.LogResolveIconBeforeTheme();
            return null;
        }

        var app = Application.Current;
        if (app is null)
        {
            return null;
        }

        var preferred = this.effectiveThemeOverride.Value == ElementTheme.Dark ? "Dark" : "Light";
        var key = $"Icon.{name}";

        // Only look in the ThemeDictionary that matches the effective theme.
        foreach (var md in app.Resources.MergedDictionaries)
        {
            if (md?.ThemeDictionaries is not { } td)
            {
                continue;
            }

            if (td.TryGetValue(preferred, out var pdObj) && pdObj is ResourceDictionary pd && pd.TryGetValue(key, out var pdVal) && pdVal is IconSource pdIcon)
            {
                return pdIcon;
            }
        }

        // No fallback: if the icon isn't found in the matching ThemeDictionary return null.
        return null;
    }

    private IMenuSource BuildCameraMenu()
    {
        var builder = new MenuBuilder(this.LoggerFactory);

        this.AddPerspectiveCameraItems(builder);

        _ = builder.AddSeparator("Orthographic");
        foreach (var type in OrthographicCameraTypes)
        {
            _ = builder.AddRadioMenuItem(
                type.ToString(),
                OrthographicCameraGroup,
                this.SceneCamera is null && this.CameraType == type,
                new RelayCommand(() => _ = this.ApplyOrthographicCameraPresetAsync(type)));
        }

        _ = builder.AddSeparator("Scene Cameras");
        var sceneCameras = this.GetSceneCameras();
        if (sceneCameras.Count == 0)
        {
            _ = builder.AddMenuItem(new MenuItemData { Text = "No cameras in scene", IsEnabled = false });
        }

        foreach (var camera in sceneCameras)
        {
            _ = builder.AddRadioMenuItem(
                camera.Name,
                SceneCameraGroup,
                this.SceneCamera?.NodeId == camera.NodeId,
                new RelayCommand(() => _ = this.ApplySceneCameraAsync(camera)));
        }

        if (this.SceneCamera is { } viewed)
        {
            var locked = this.IsLocked(viewed.NodeId);
            _ = builder.AddMenuItem(new MenuItemData
            {
                Text = PilotCameraText,
                IsCheckable = true,
                IsChecked = this.IsPilotingSceneCamera,
                HelpText = locked ? LockedCameraText : null,
                IsEnabled = !locked || this.IsPilotingSceneCamera,
                Command = new RelayCommand(() => _ = this.SetPilotAsync(!this.IsPilotingSceneCamera)),
            });
        }

        var selected = this.SelectedCameraProvider?.Invoke();
        var alignDisabledReason = selected is null ? "Select a camera node to move it to this view."
            : this.SceneCamera is not null ? "Return to the editor camera to align a camera to it."
            : this.IsLocked(selected.NodeId) ? LockedCameraText
            : null;
        _ = builder.AddMenuItem(new MenuItemData
        {
            Text = selected is null ? AlignCameraText : $"Align '{selected.Name}' to View",
            HelpText = alignDisabledReason,
            AcceleratorText = "Ctrl+Shift+F",
            IsEnabled = alignDisabledReason is null,
            Command = new RelayCommand(() => _ = this.AlignSelectedCameraToViewAsync()),
        });

        _ = builder
            .AddSeparator("View")
            .AddMenuItem(this.CreateCameraNumberBoxMenuItem(FieldOfViewText, this.fieldOfViewItem))
            .AddMenuItem(this.CreateCameraNumberBoxMenuItem(NearViewPlaneText, this.nearViewPlaneItem))
            .AddMenuItem(this.CreateCameraNumberBoxMenuItem(FarViewPlaneText, this.farViewPlaneItem));

        return builder.Build();
    }

    private void AddPerspectiveCameraItems(MenuBuilder builder)
        => _ = builder
            .AddSeparator("Perspective")
            .AddMenuItem(this.CreatePerspectiveCameraModeItem("Turntable", CameraControlMode.OrbitTurntable))
            .AddMenuItem(this.CreatePerspectiveCameraModeItem("Trackball", CameraControlMode.OrbitTrackball))
            .AddMenuItem(this.CreatePerspectiveCameraModeItem("Fly", CameraControlMode.Fly))
            .AddMenuItem(this.CreateCameraNumberBoxMenuItem(MovementSpeedText, this.movementSpeedItem));

    private MenuItemData CreatePerspectiveCameraModeItem(string text, CameraControlMode mode)
        => new()
        {
            Text = text,
            RadioGroupId = PerspectiveCameraModeGroup,
            IsChecked = this.SceneCamera is null && this.CameraType == CameraType.Perspective && this.CameraControlMode == mode,
            Command = new RelayCommand(() => _ = this.ApplyPerspectiveCameraModeAsync(mode)),
        };

    private async Task ApplyPerspectiveCameraModeAsync(CameraControlMode mode)
    {
        await this.ApplyCameraPresetAsync(CameraType.Perspective).ConfigureAwait(true);
        await this.ApplyCameraControlModeAsync(mode).ConfigureAwait(true);
    }

    private async Task ApplyOrthographicCameraPresetAsync(CameraType type)
    {
        if (this.CameraControlMode == CameraControlMode.Fly)
        {
            await this.ApplyCameraControlModeAsync(CameraControlMode.OrbitTurntable).ConfigureAwait(true);
        }

        await this.ApplyCameraPresetAsync(type).ConfigureAwait(true);
    }

    private async Task ApplyCameraPresetAsync(CameraType type)
    {
        if (this.SceneCamera is not null)
        {
            await this.ApplySceneCameraAsync(camera: null).ConfigureAwait(true);
        }

        this.CameraType = type;
        this.RebuildCameraMenu();

        if (!this.AssignedViewId.IsValid)
        {
            return;
        }

        var preset = type switch
        {
            CameraType.Perspective => CameraViewPreset.Perspective,
            CameraType.Top => CameraViewPreset.Top,
            CameraType.Bottom => CameraViewPreset.Bottom,
            CameraType.Left => CameraViewPreset.Left,
            CameraType.Right => CameraViewPreset.Right,
            CameraType.Front => CameraViewPreset.Front,
            CameraType.Back => CameraViewPreset.Back,
            _ => CameraViewPreset.Perspective,
        };

        try
        {
            var accepted = await this.EngineService.SetViewCameraPresetAsync(this.AssignedViewId, preset).ConfigureAwait(true);
            if (!accepted)
            {
                this.PublishRuntimeWarning(
                    RuntimeOperationKinds.ViewSetCameraPreset,
                    FailureDomain.RuntimeView,
                    DiagnosticCodes.ViewPrefix + "CAMERA_PRESET_REJECTED",
                    "Camera preset was not applied",
                    "The runtime rejected the camera preset for this viewport.");
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.PublishRuntimeFailure(
                RuntimeOperationKinds.ViewSetCameraPreset,
                FailureDomain.RuntimeView,
                DiagnosticCodes.ViewPrefix + "CAMERA_PRESET_FAILED",
                "Camera preset failed",
                "The runtime could not apply the camera preset for this viewport.",
                ex);
        }
    }

    private async Task ApplySceneCameraAsync(SceneCameraChoice? camera)
    {
        if (this.IsPilotingSceneCamera)
        {
            // The runtime ends the pilot when the viewed camera changes.
            await this.FlushPilotPoseAsync().ConfigureAwait(true);
            this.IsPilotingSceneCamera = false;
        }

        this.SceneCamera = camera;
        this.RebuildCameraMenu();

        if (this.AssignedViewId.IsValid)
        {
            _ = await this.SendSceneCameraAsync(camera).ConfigureAwait(true);
        }
    }

    private async Task<bool> SendSceneCameraAsync(SceneCameraChoice? camera)
    {
        try
        {
            var accepted = await this.EngineService.SetViewSceneCameraAsync(this.AssignedViewId, camera?.NodeId).ConfigureAwait(true);
            if (!accepted)
            {
                this.PublishRuntimeWarning(
                    RuntimeOperationKinds.ViewSetSceneCamera,
                    FailureDomain.RuntimeView,
                    DiagnosticCodes.ViewPrefix + "SCENE_CAMERA_REJECTED",
                    "Scene camera was not applied",
                    "The runtime rejected the scene camera for this viewport.");
            }

            return accepted;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.PublishRuntimeFailure(
                RuntimeOperationKinds.ViewSetSceneCamera,
                FailureDomain.RuntimeView,
                DiagnosticCodes.ViewPrefix + "SCENE_CAMERA_FAILED",
                "Scene camera failed",
                "The runtime could not render this viewport through the scene camera.",
                ex);
            return false;
        }
    }

    private async Task SetPilotAsync(bool pilot)
    {
        if (pilot == this.IsPilotingSceneCamera
            || (pilot && (this.SceneCamera is not { } camera || this.IsLocked(camera.NodeId))))
        {
            return;
        }

        if (!pilot)
        {
            await this.FlushPilotPoseAsync().ConfigureAwait(true);
        }
        else if (this.CameraType != CameraType.Perspective)
        {
            // The runtime pilots through the perspective editor camera.
            this.CameraType = CameraType.Perspective;
        }

        this.IsPilotingSceneCamera = pilot;
        this.RebuildCameraMenu();
        if (this.AssignedViewId.IsValid)
        {
            _ = await this.SendScenePilotAsync(pilot).ConfigureAwait(true);
        }
    }

    private async Task<bool> SendScenePilotAsync(bool pilot)
    {
        try
        {
            var accepted = await this.EngineService.SetViewScenePilotAsync(this.AssignedViewId, pilot).ConfigureAwait(true);
            if (!accepted)
            {
                this.PublishRuntimeWarning(
                    RuntimeOperationKinds.ViewSetScenePilot,
                    FailureDomain.RuntimeView,
                    DiagnosticCodes.ViewPrefix + "SCENE_PILOT_REJECTED",
                    "Camera pilot was not applied",
                    "The runtime rejected piloting the scene camera for this viewport.");
            }

            return accepted;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.PublishRuntimeFailure(
                RuntimeOperationKinds.ViewSetScenePilot,
                FailureDomain.RuntimeView,
                DiagnosticCodes.ViewPrefix + "SCENE_PILOT_FAILED",
                "Camera pilot failed",
                "The runtime could not pilot the scene camera for this viewport.",
                ex);
            return false;
        }
    }

    private async Task CommitPilotAfterPauseAsync(CancellationToken cancellationToken)
    {
        try
        {
            await Task.Delay(this.PilotCommitDelay, cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return;
        }

        await this.FlushPilotPoseAsync().ConfigureAwait(true);
    }

    private void CancelPilotCommit()
    {
        this.pilotCommitDelay?.Cancel();
        this.pilotCommitDelay?.Dispose();
        this.pilotCommitDelay = null;
    }

    private async Task FlushPilotPoseAsync()
    {
        this.CancelPilotCommit();
        if (!this.pilotPoseDirty || this.SceneCamera is not { } camera)
        {
            return;
        }

        this.pilotPoseDirty = false;
        _ = await this.CommitCameraPoseAsync(camera.NodeId).ConfigureAwait(true);
    }

    private async Task<bool> CommitCameraPoseAsync(Guid nodeId)
    {
        if (this.CameraPoseCommitter is not { } commit || !this.AssignedViewId.IsValid)
        {
            return false;
        }

        try
        {
            var pose = await this.EngineService.GetViewCameraPoseAsync(this.AssignedViewId, nodeId).ConfigureAwait(true);
            if (pose is null)
            {
                this.PublishRuntimeWarning(
                    RuntimeOperationKinds.ViewGetCameraPose,
                    FailureDomain.RuntimeView,
                    DiagnosticCodes.ViewPrefix + "CAMERA_POSE_UNAVAILABLE",
                    "Camera was not moved",
                    "The runtime could not read the view pose for the camera node.");
                return false;
            }

            return await commit(nodeId, pose).ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.PublishRuntimeFailure(
                RuntimeOperationKinds.ViewGetCameraPose,
                FailureDomain.RuntimeView,
                DiagnosticCodes.ViewPrefix + "CAMERA_POSE_FAILED",
                "Camera move failed",
                "The runtime could not read the view pose for the camera node.",
                ex);
            return false;
        }
    }

    private IReadOnlyList<SceneCameraChoice> GetSceneCameras() => this.SceneCamerasProvider?.Invoke() ?? [];

    private bool IsLocked(Guid nodeId) => this.CameraLockProvider?.Invoke(nodeId) == true;

    private void RebuildCameraMenu()
    {
        this.cameraMenu = this.BuildCameraMenu();
        this.OnPropertyChanged(nameof(this.CameraMenu));
    }

    private async Task ApplyCameraControlModeAsync(CameraControlMode mode)
    {
        this.CameraControlMode = mode;
        this.RebuildCameraMenu();

        if (!this.AssignedViewId.IsValid)
        {
            return;
        }

        try
        {
            var accepted = await this.EngineService.SetViewCameraControlModeAsync(this.AssignedViewId, mode).ConfigureAwait(true);
            if (!accepted)
            {
                this.PublishRuntimeWarning(
                    RuntimeOperationKinds.ViewSetCameraControlMode,
                    FailureDomain.RuntimeView,
                    DiagnosticCodes.ViewPrefix + "CAMERA_CONTROL_MODE_REJECTED",
                    "Camera mode was not applied",
                    "The runtime rejected the camera control mode for this viewport.");
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.PublishRuntimeFailure(
                RuntimeOperationKinds.ViewSetCameraControlMode,
                FailureDomain.RuntimeView,
                DiagnosticCodes.ViewPrefix + "CAMERA_CONTROL_MODE_FAILED",
                "Camera mode failed",
                "The runtime could not apply the camera control mode for this viewport.",
                ex);
        }
    }

    private MenuItemData CreateCameraNumberBoxMenuItem(string text, ViewportCameraNumberBoxItemModel model)
        => new()
        {
            Text = text,
            InteractiveContent = model,
            InteractiveContentTemplate = this.cameraNumberBoxItemTemplate,
        };

    private void OnCameraNumberBoxValueChanged(string propertyName, float value)
    {
        this.OnPropertyChanged(propertyName);

        if (string.Equals(propertyName, nameof(this.MovementSpeed), StringComparison.Ordinal))
        {
            _ = this.ApplyCameraMovementSpeedAsync(value);
            return;
        }

        _ = this.ApplyCameraSettingsAsync();
    }

    private async Task ApplyCameraMovementSpeedAsync(float speedUnitsPerSecond)
    {
        if (!this.AssignedViewId.IsValid)
        {
            return;
        }

        try
        {
            var accepted = await this.EngineService.SetViewCameraMovementSpeedAsync(this.AssignedViewId, speedUnitsPerSecond).ConfigureAwait(true);
            if (!accepted)
            {
                this.PublishRuntimeWarning(
                    RuntimeOperationKinds.ViewSetCameraMovementSpeed,
                    FailureDomain.RuntimeView,
                    DiagnosticCodes.ViewPrefix + "CAMERA_MOVEMENT_SPEED_REJECTED",
                    "Camera movement speed was not applied",
                    "The runtime rejected the camera movement speed for this viewport.");
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.PublishRuntimeFailure(
                RuntimeOperationKinds.ViewSetCameraMovementSpeed,
                FailureDomain.RuntimeView,
                DiagnosticCodes.ViewPrefix + "CAMERA_MOVEMENT_SPEED_FAILED",
                "Camera movement speed failed",
                "The runtime could not apply the camera movement speed for this viewport.",
                ex);
        }
    }

    private async Task ApplyCameraSettingsAsync()
    {
        if (!this.AssignedViewId.IsValid)
        {
            return;
        }

        try
        {
            var accepted = await this.EngineService.SetViewCameraSettingsAsync(
                this.AssignedViewId,
                this.FieldOfViewDegrees,
                this.NearViewPlane,
                this.FarViewPlane).ConfigureAwait(true);
            if (!accepted)
            {
                this.PublishRuntimeWarning(
                    RuntimeOperationKinds.ViewSetCameraSettings,
                    FailureDomain.RuntimeView,
                    DiagnosticCodes.ViewPrefix + "CAMERA_SETTINGS_REJECTED",
                    "Camera settings were not applied",
                    "The runtime rejected the camera settings for this viewport.");
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.PublishRuntimeFailure(
                RuntimeOperationKinds.ViewSetCameraSettings,
                FailureDomain.RuntimeView,
                DiagnosticCodes.ViewPrefix + "CAMERA_SETTINGS_FAILED",
                "Camera settings failed",
                "The runtime could not apply the camera settings for this viewport.",
                ex);
        }
    }

    private IMenuSource BuildShadingMenu()
    {
        var builder = new MenuBuilder(this.LoggerFactory);

        foreach (var mode in new[] { ShadingMode.Wireframe, ShadingMode.Shaded, ShadingMode.Rendered })
        {
            _ = builder.AddRadioMenuItem(mode.ToString(), "ShadingMode", this.ShadingMode == mode, new RelayCommand(() => this.ShadingMode = mode));
        }

        return builder.Build();
    }

    private IMenuSource BuildLayoutMenu()
    {
        var builder = new MenuBuilder(this.LoggerFactory);
        _ = builder.AddMenuItem(CreateToggleMenuItem("Show FPS", () => this.ShowFps, v => this.ShowFps = v, "Ctrl+Shift+H"))
            .AddMenuItem(CreateToggleMenuItem("Show Stats", () => this.ShowStats, v => this.ShowStats = v, "Shift+L"))
            .AddSubmenu("Stats", submenu => submenu
                .AddMenuItem(CreateToggleMenuItem("Stat1", () => this.Stat1, v => this.Stat1 = v))
                .AddMenuItem(CreateToggleMenuItem("Stat2", () => this.Stat2, v => this.Stat2 = v))
                .AddMenuItem(CreateToggleMenuItem("Stat3", () => this.Stat3, v => this.Stat3 = v)))
            .AddMenuItem(CreateToggleMenuItem("Show Toolbar", () => this.ShowToolbar, v => this.ShowToolbar = v, "Ctrl+Shift+T"))
            .AddSeparator();

        // Layouts submenu with grouped panes and themed icons
        _ = builder.AddSubmenu("Layouts", layouts =>
        {
            _ = layouts.AddMenuItem("One Pane", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.OnePane)), this.ResolveIcon("OnePane"));
            _ = layouts.AddMenuItem("Four Quadrants", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourQuad)), this.ResolveIcon("FourQuad"));
            _ = layouts.AddSubmenu("Two Panes", two =>
            {
                _ = two.AddMenuItem("Main Left", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.TwoMainLeft)), this.ResolveIcon("TwoMainLeft"));
                _ = two.AddMenuItem("Main Right", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.TwoMainRight)), this.ResolveIcon("TwoMainRight"));
                _ = two.AddMenuItem("Main Top", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.TwoMainTop)), this.ResolveIcon("TwoMainTop"));
                _ = two.AddMenuItem("Main Bottom", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.TwoMainBottom)), this.ResolveIcon("TwoMainBottom"));
            });
            _ = layouts.AddSubmenu("Three Panes", three =>
            {
                _ = three.AddMenuItem("Main Left", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.ThreeMainLeft)), this.ResolveIcon("ThreeMainLeft"));
                _ = three.AddMenuItem("Main Right", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.ThreeMainRight)), this.ResolveIcon("ThreeMainRight"));
                _ = three.AddMenuItem("Main Top", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.ThreeMainTop)), this.ResolveIcon("ThreeMainTop"));
                _ = three.AddMenuItem("Main Bottom", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.ThreeMainBottom)), this.ResolveIcon("ThreeMainBottom"));
            });
            _ = layouts.AddSubmenu("Four Panes", four =>
            {
                _ = four.AddMenuItem("Main Left", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourMainLeft)), this.ResolveIcon("FourMainLeft"));
                _ = four.AddMenuItem("Main Right", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourMainRight)), this.ResolveIcon("FourMainRight"));
                _ = four.AddMenuItem("Main Top", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourMainTop)), this.ResolveIcon("FourMainTop"));
                _ = four.AddMenuItem("Main Bottom", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourMainBottom)), this.ResolveIcon("FourMainBottom"));
            });
        });

        return builder.Build();
    }

    private AffectedScope CreateAffectedScope()
        => new()
        {
            DocumentId = this.DocumentId,
        };

    private string CreateViewportTechnicalMessage(Exception? exception)
    {
        var viewId = this.AssignedViewId.IsValid ? this.AssignedViewId.ToString() : "Invalid";
        var message = string.Create(CultureInfo.InvariantCulture, $"DocumentId={this.DocumentId}; ViewportId={this.ViewportId}; ViewportIndex={this.ViewportIndex}; IsPrimary={this.IsPrimaryViewport}; ViewId={viewId}");
        return exception is null ? message : $"{message}; {exception.Message}";
    }
}
