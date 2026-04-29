// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
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
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Interop;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// ViewModel for the Viewport control, managing camera, shading, and menus.
/// </summary>
public partial class ViewportViewModel : ObservableObject, IDisposable
{
    private const string DegreeUnit = "\u00b0";
    private const string PerspectiveCameraModeGroup = "PerspectiveCameraMode";
    private const string OrthographicCameraGroup = "OrthographicCamera";
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
    /// <param name="appearanceSettings">
    ///     The <see cref="ISettingsService{IAppearanceSettings}" /> used to provide appearance and theme settings.
    ///     This service supplies the current theme and notifies the view model of changes.
    /// </param>
    /// <param name="loggerFactory">
    ///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    ///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    /// <param name="operationResults">The host-level operation result publisher.</param>
    /// <param name="statusReducer">The shared operation status reducer.</param>
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
    public partial CameraControlModeManaged CameraControlMode { get; set; } = CameraControlModeManaged.OrbitTurntable;

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
    public ViewIdManaged AssignedViewId { get; set; } = ViewIdManaged.Invalid;

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
    public string CameraMenuLabel => this.CameraType == CameraType.Perspective ? this.CameraControlModeLabel : this.CameraType.ToString();

    /// <summary>
    /// Gets the display label for the editor camera control mode.
    /// </summary>
    public string CameraControlModeLabel => this.CameraControlMode switch
    {
        CameraControlModeManaged.OrbitTurntable => "Turntable",
        CameraControlModeManaged.OrbitTrackball => "Trackball",
        CameraControlModeManaged.Fly => "Fly",
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

    // Background clear color used when creating a matching engine view. Exposed
    // from the view-model so each viewport can choose its own diagnostic tint.
    [ObservableProperty]
    public partial ColorManaged ClearColor { get; set; } = new ColorManaged(0.1f, 0.12f, 0.15f, 1.0f);

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
        catch (Exception ex)
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
                this.CameraType == type,
                new RelayCommand(() => _ = this.ApplyOrthographicCameraPresetAsync(type)));
        }

        _ = builder
            .AddSeparator("View")
            .AddMenuItem(this.CreateCameraNumberBoxMenuItem(FieldOfViewText, this.fieldOfViewItem))
            .AddMenuItem(this.CreateCameraNumberBoxMenuItem(NearViewPlaneText, this.nearViewPlaneItem))
            .AddMenuItem(this.CreateCameraNumberBoxMenuItem(FarViewPlaneText, this.farViewPlaneItem));

        return builder.Build();
    }

    private void AddPerspectiveCameraItems(MenuBuilder builder)
    {
        _ = builder
            .AddSeparator("Perspective")
            .AddMenuItem(this.CreatePerspectiveCameraModeItem("Turntable", CameraControlModeManaged.OrbitTurntable))
            .AddMenuItem(this.CreatePerspectiveCameraModeItem("Trackball", CameraControlModeManaged.OrbitTrackball))
            .AddMenuItem(this.CreatePerspectiveCameraModeItem("Fly", CameraControlModeManaged.Fly))
            .AddMenuItem(this.CreateCameraNumberBoxMenuItem(MovementSpeedText, this.movementSpeedItem));
    }

    private MenuItemData CreatePerspectiveCameraModeItem(string text, CameraControlModeManaged mode)
        => new()
        {
            Text = text,
            RadioGroupId = PerspectiveCameraModeGroup,
            IsChecked = this.CameraType == CameraType.Perspective && this.CameraControlMode == mode,
            Command = new RelayCommand(() => _ = this.ApplyPerspectiveCameraModeAsync(mode)),
        };

    private async Task ApplyPerspectiveCameraModeAsync(CameraControlModeManaged mode)
    {
        await this.ApplyCameraPresetAsync(CameraType.Perspective).ConfigureAwait(true);
        await this.ApplyCameraControlModeAsync(mode).ConfigureAwait(true);
    }

    private async Task ApplyOrthographicCameraPresetAsync(CameraType type)
    {
        if (this.CameraControlMode == CameraControlModeManaged.Fly)
        {
            await this.ApplyCameraControlModeAsync(CameraControlModeManaged.OrbitTurntable).ConfigureAwait(true);
        }

        await this.ApplyCameraPresetAsync(type).ConfigureAwait(true);
    }

    private async Task ApplyCameraPresetAsync(CameraType type)
    {
        this.CameraType = type;
        this.cameraMenu = this.BuildCameraMenu();
        this.OnPropertyChanged(nameof(this.CameraMenu));

        if (!this.AssignedViewId.IsValid)
        {
            return;
        }

        var preset = type switch
        {
            CameraType.Perspective => CameraViewPresetManaged.Perspective,
            CameraType.Top => CameraViewPresetManaged.Top,
            CameraType.Bottom => CameraViewPresetManaged.Bottom,
            CameraType.Left => CameraViewPresetManaged.Left,
            CameraType.Right => CameraViewPresetManaged.Right,
            CameraType.Front => CameraViewPresetManaged.Front,
            CameraType.Back => CameraViewPresetManaged.Back,
            _ => CameraViewPresetManaged.Perspective,
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

    private async Task ApplyCameraControlModeAsync(CameraControlModeManaged mode)
    {
        this.CameraControlMode = mode;
        this.cameraMenu = this.BuildCameraMenu();
        this.OnPropertyChanged(nameof(this.CameraMenu));

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
        var message = $"DocumentId={this.DocumentId}; ViewportId={this.ViewportId}; ViewportIndex={this.ViewportIndex}; IsPrimary={this.IsPrimaryViewport}; ViewId={viewId}";
        return exception is null ? message : $"{message}; {exception.Message}";
    }
}
