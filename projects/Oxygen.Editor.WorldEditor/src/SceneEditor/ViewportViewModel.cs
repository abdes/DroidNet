// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// ViewModel for one viewport pane: its editor camera, the scene camera it looks through, and the
/// view camera flyout that controls both.
/// </summary>
public partial class ViewportViewModel : ObservableObject, IDisposable
{
    private const string DegreeUnit = "°";
    private const string LockedCameraText = "Unlock the camera to move it.";
    private const float DefaultFieldOfViewDegrees = 90.0f;
    private const float DefaultNearViewPlane = 0.1f;
    private const float DefaultFarViewPlane = 1000.0f;

    private static readonly CameraControlMode[] PerspectiveModeValues =
    [
        CameraControlMode.OrbitTurntable,
        CameraControlMode.OrbitTrackball,
        CameraControlMode.Fly,
    ];

    // Display order of the orthographic grid: three rows of opposite pairs read left to right.
    private static readonly CameraType[] OrthographicViewValues =
    [
        CameraType.Top,
        CameraType.Bottom,
        CameraType.Front,
        CameraType.Back,
        CameraType.Left,
        CameraType.Right,
    ];

    private readonly ILogger logger;
    private readonly IOperationResultPublisher operationResults;
    private readonly IStatusReducer statusReducer;
    private readonly List<SceneCameraChoice> sceneCameraChoices = [];
    private CancellationTokenSource? pilotCommitDelay;
    private CancellationTokenSource? navigationSettleDelay;
    private bool pilotPoseDirty;
    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="ViewportViewModel"/> class.
    /// </summary>
    /// <param name="documentId">The owning document identifier.</param>
    /// <param name="engineService">The shared engine service.</param>
    /// <param name="operationResults">The host-level operation result publisher.</param>
    /// <param name="statusReducer">The shared operation status reducer.</param>
    /// <param name="loggerFactory">
    ///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    ///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    public ViewportViewModel(
        Guid documentId,
        IEngineService engineService,
        IOperationResultPublisher operationResults,
        IStatusReducer statusReducer,
        ILoggerFactory? loggerFactory = null)
    {
        this.LoggerFactory = loggerFactory;
        this.logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger("Oxygen.Editor.LevelEditor.ViewportViewModel");

        this.DocumentId = documentId;
        this.EngineService = engineService;
        this.operationResults = operationResults;
        this.statusReducer = statusReducer;
        this.MovementSpeedField = this.CreateCameraNumberField(
            value: 1.0f,
            minimum: 1.0f,
            maximum: float.PositiveInfinity,
            unit: "m/s",
            propertyName: nameof(this.MovementSpeed));
        this.FieldOfViewField = this.CreateCameraNumberField(
            value: DefaultFieldOfViewDegrees,
            minimum: 0.0f,
            maximum: 180.0f,
            unit: DegreeUnit,
            propertyName: nameof(this.FieldOfViewDegrees));
        this.NearViewPlaneField = this.CreateCameraNumberField(
            value: DefaultNearViewPlane,
            minimum: 0.0f,
            maximum: float.PositiveInfinity,
            unit: "m",
            propertyName: nameof(this.NearViewPlane),
            mask: "~.###");
        this.FarViewPlaneField = this.CreateCameraNumberField(
            value: DefaultFarViewPlane,
            minimum: 0.0f,
            maximum: float.PositiveInfinity,
            unit: "m",
            propertyName: nameof(this.FarViewPlane),
            mask: "~.###");

        this.PerspectiveModes =
        [
            new ViewportOption("Turntable", "Orbit with a level horizon", () => this.ApplyPerspectiveCameraModeAsync(CameraControlMode.OrbitTurntable)),
            new ViewportOption("Trackball", "Orbit freely, including roll", () => this.ApplyPerspectiveCameraModeAsync(CameraControlMode.OrbitTrackball)),
            new ViewportOption("Fly", "Move through the scene", () => this.ApplyPerspectiveCameraModeAsync(CameraControlMode.Fly)),
        ];
        this.OrthographicViews = [.. OrthographicViewValues.Select(type => new ViewportOption(type.ToString(), description: null, () => this.ApplyOrthographicCameraPresetAsync(type)))];
        this.ViewModeGroups = BuildViewModeGroups(this.ApplyViewModeAsync);
        this.LayoutGroups = this.BuildLayoutGroups();
        this.ToggleMaximizeCommand = new RelayCommand(() => this.IsMaximized = !this.IsMaximized);
        this.UpdateCameraOptions();
        this.UpdateViewModeOptions();
        this.UpdateLayoutOptions();
        this.LogInitialized();
    }

    /// <summary>
    /// Raised when state the pane keeps across sessions changed: its preset, control mode, viewed
    /// camera or presentation, or its editor camera once navigation has paused.
    /// </summary>
    public event EventHandler? StateChanged;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CameraMenuLabel))]
    [NotifyPropertyChangedFor(nameof(IsPerspectiveView))]
    [NotifyPropertyChangedFor(nameof(GestureHint))]
    public partial CameraType CameraType { get; set; } = CameraType.Perspective;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CameraMenuLabel))]
    [NotifyPropertyChangedFor(nameof(CameraControlModeLabel))]
    [NotifyPropertyChangedFor(nameof(GestureHint))]
    public partial CameraControlMode CameraControlMode { get; set; } = CameraControlMode.OrbitTurntable;

    /// <summary>
    /// Gets or sets the authored scene camera this viewport renders through, or <see langword="null"/>
    /// when it renders through its editor camera.
    /// </summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CameraMenuLabel))]
    [NotifyPropertyChangedFor(nameof(IsPerspectiveView))]
    [NotifyPropertyChangedFor(nameof(CanPilot))]
    [NotifyPropertyChangedFor(nameof(PilotHint))]
    [NotifyPropertyChangedFor(nameof(GestureHint))]
    public partial SceneCameraChoice? SceneCamera { get; set; }

    /// <summary>
    /// Gets or sets a value indicating whether navigation moves the scene camera this viewport looks through.
    /// </summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CameraMenuLabel))]
    [NotifyPropertyChangedFor(nameof(CanPilot))]
    [NotifyPropertyChangedFor(nameof(PilotHint))]
    [NotifyPropertyChangedFor(nameof(PilotLabel))]
    [NotifyPropertyChangedFor(nameof(GestureHint))]
    public partial bool IsPilotingSceneCamera { get; set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(MaximizeGlyph))]
    [NotifyPropertyChangedFor(nameof(MaximizeToolTip))]
    public partial bool IsMaximized { get; set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsGestureHintVisible))]
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

    /// <summary>Gets the perspective navigation modes offered by the view camera flyout.</summary>
    public IReadOnlyList<ViewportOption> PerspectiveModes { get; }

    /// <summary>Gets the orthographic directions offered by the view camera flyout.</summary>
    public IReadOnlyList<ViewportOption> OrthographicViews { get; }

    /// <summary>Gets the scene cameras the pane can look through, refreshed when the flyout opens.</summary>
    public ObservableCollection<ViewportOption> SceneCameraOptions { get; } = [];

    /// <summary>Gets a value indicating whether the scene has a camera to look through.</summary>
    public bool HasSceneCameras => this.SceneCameraOptions.Count > 0;

    /// <summary>Gets the fly movement speed field.</summary>
    public ViewportCameraNumberBoxItemModel MovementSpeedField { get; }

    /// <summary>Gets the vertical field of view field, in degrees.</summary>
    public ViewportCameraNumberBoxItemModel FieldOfViewField { get; }

    /// <summary>Gets the near clipping distance field, in metres.</summary>
    public ViewportCameraNumberBoxItemModel NearViewPlaneField { get; }

    /// <summary>Gets the far clipping distance field, in metres.</summary>
    public ViewportCameraNumberBoxItemModel FarViewPlaneField { get; }

    /// <summary>
    /// Gets the display label for the view camera button: the navigation mode, orthographic
    /// direction or viewed scene camera.
    /// </summary>
    public string CameraMenuLabel => this.SceneCamera is { } camera
        ? (this.IsPilotingSceneCamera ? $"Piloting {camera.Name}" : camera.Name)
        : (this.CameraType == CameraType.Perspective ? this.CameraControlModeLabel : this.CameraType.ToString());

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

    /// <summary>Gets a value indicating whether the pane shows the scene through its perspective editor camera.</summary>
    public bool IsPerspectiveView => this.SceneCamera is null && this.CameraType == CameraType.Perspective;

    /// <summary>Gets a value indicating whether the viewed scene camera can be piloted, or piloting stopped.</summary>
    public bool CanPilot => this.SceneCamera is { } camera && (this.IsPilotingSceneCamera || !this.IsLocked(camera.NodeId));

    /// <summary>Gets the label of the pilot action.</summary>
    public string PilotLabel => this.IsPilotingSceneCamera ? "Stop piloting" : "Pilot camera";

    /// <summary>Gets why piloting is unavailable, or what piloting does.</summary>
    public string PilotHint => this.SceneCamera is not { } camera
        ? "Look through a scene camera to pilot it."
        : !this.IsPilotingSceneCamera && this.IsLocked(camera.NodeId)
            ? LockedCameraText
            : "Navigation moves the camera; each pause is one undoable edit.";

    /// <summary>Gets the label of the Align to View action, naming the selected camera.</summary>
    public string AlignCameraLabel => this.SelectedCameraProvider?.Invoke() is { } selected
        ? $"Align '{selected.Name}' to view"
        : "Align selected camera to view";

    /// <summary>Gets why Align to View is unavailable, or <see langword="null"/> when it is available.</summary>
    public string? AlignCameraDisabledReason => this.SelectedCameraProvider?.Invoke() switch
    {
        null => "Select a camera node to move it to this view.",
        _ when this.SceneCamera is not null => "Return to the editor camera to align a camera to it.",
        { } selected when this.IsLocked(selected.NodeId) => LockedCameraText,
        _ => null,
    };

    /// <summary>Gets a value indicating whether Align to View is available.</summary>
    public bool CanAlignCamera => this.AlignCameraDisabledReason is null;

    /// <summary>Gets the clipping range summary shown on the Clipping disclosure.</summary>
    public string ClippingSummary
        => string.Create(CultureInfo.CurrentCulture, $"{this.NearViewPlane:0.###}–{this.FarViewPlane:0.###} m");

    /// <summary>
    /// Gets or sets the authored scene cameras listed in the view camera flyout.
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
    /// Gets or sets the hook that runs before this pane starts piloting a camera, so the panes
    /// piloting the same camera stop first.
    /// </summary>
    public Func<ViewportViewModel, Guid, Task>? PilotStarting { get; set; }

    /// <summary>
    /// Gets or sets the editor fly movement speed.
    /// </summary>
    public float MovementSpeed
    {
        get => this.MovementSpeedField.NumberValue;
        set => this.MovementSpeedField.NumberValue = value;
    }

    /// <summary>Gets or sets the fly movement speed as edited by the flyout slider.</summary>
    public double FlySpeed
    {
        get => this.MovementSpeed;
        set => this.MovementSpeed = (float)Math.Max(value, this.MovementSpeedField.Minimum);
    }

    /// <summary>
    /// Gets or sets the editor camera field of view in degrees.
    /// </summary>
    public float FieldOfViewDegrees
    {
        get => this.FieldOfViewField.NumberValue;
        set => this.FieldOfViewField.NumberValue = value;
    }

    /// <summary>
    /// Gets or sets the editor camera near view plane in meters.
    /// </summary>
    public float NearViewPlane
    {
        get => this.NearViewPlaneField.NumberValue;
        set => this.NearViewPlaneField.NumberValue = value;
    }

    /// <summary>
    /// Gets or sets the editor camera far view plane in meters.
    /// </summary>
    public float FarViewPlane
    {
        get => this.FarViewPlaneField.NumberValue;
        set => this.FarViewPlaneField.NumberValue = value;
    }

    /// <summary>
    /// Gets or sets the command to toggle maximize state.
    /// </summary>
    public ICommand? ToggleMaximizeCommand { get; set; }

    /// <summary>
    /// Gets the neutral clear color of the engine view, shared by every pane.
    /// </summary>
    public RuntimeColor ClearColor { get; } = new(0.1f, 0.12f, 0.15f, 1.0f);

    /// <summary>
    /// Gets the glyph for the maximize/restore button.
    /// </summary>
    public string MaximizeGlyph => this.IsMaximized ? "" : "";

    /// <summary>Gets the tooltip of the maximize/restore button.</summary>
    public string MaximizeToolTip => this.IsMaximized ? "Restore viewport layout" : "Maximize viewport";

    /// <summary>
    /// Gets the logger factory.
    /// </summary>
    public ILoggerFactory? LoggerFactory { get; }

    /// <summary>
    /// Gets or sets how long navigation must pause before a pilot gesture is committed.
    /// </summary>
    internal TimeSpan PilotCommitDelay { get; set; } = TimeSpan.FromMilliseconds(300);

    /// <summary>
    /// Gets or sets how long navigation must pause before the editor camera counts as moved.
    /// </summary>
    internal TimeSpan NavigationSettleDelay { get; set; } = TimeSpan.FromMilliseconds(500);

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
    /// Re-reads the scene cameras for the view camera flyout. A viewed scene camera that no longer
    /// exists is dropped in favor of the editor camera.
    /// </summary>
    internal void RefreshSceneCameras()
    {
        var cameras = this.GetSceneCameras();
        if (this.SceneCamera is { } current)
        {
            var match = cameras.FirstOrDefault(camera => camera.NodeId == current.NodeId);
            if (match is null)
            {
                _ = this.ApplySceneCameraAsync(camera: null);
            }
            else
            {
                this.SceneCamera = match;
            }
        }

        this.sceneCameraChoices.Clear();
        this.sceneCameraChoices.AddRange(cameras);
        this.SceneCameraOptions.Clear();
        foreach (var camera in cameras)
        {
            this.SceneCameraOptions.Add(new ViewportOption(camera.Name, description: null, () => this.ApplySceneCameraAsync(camera)));
        }

        this.OnPropertyChanged(nameof(this.HasSceneCameras));
        this.OnPropertyChanged(nameof(this.AlignCameraLabel));
        this.OnPropertyChanged(nameof(this.AlignCameraDisabledReason));
        this.OnPropertyChanged(nameof(this.CanAlignCamera));
        this.OnPropertyChanged(nameof(this.PilotHint));
        this.OnPropertyChanged(nameof(this.CanPilot));
        this.UpdateCameraOptions();
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

    /// <summary>
    /// Reports navigation input in this viewport. While piloting, a gesture is committed as one
    /// undoable camera edit once no button or key is held and input has paused.
    /// </summary>
    /// <param name="inputHeld">Whether a mouse button or key is still held.</param>
    internal void NotifyNavigationInput(bool inputHeld)
    {
        this.MarkNavigated();
        this.ScheduleNavigationSettled(inputHeld);
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
                this.CancelPilotCommit();
                this.CancelNavigationSettled();
                this.CancelNotice();
            }

            this.isDisposed = true;
        }
    }

    private ViewportCameraNumberBoxItemModel CreateCameraNumberField(
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

    partial void OnCameraTypeChanged(CameraType value)
    {
        this.UpdateCameraOptions();
        this.ResetGestureHint();
        this.RaiseStateChanged();
    }

    partial void OnCameraControlModeChanged(CameraControlMode value)
    {
        this.UpdateCameraOptions();
        this.ResetGestureHint();
        this.RaiseStateChanged();
    }

    partial void OnSceneCameraChanged(SceneCameraChoice? value)
    {
        this.UpdateCameraOptions();
        this.ResetGestureHint();
        this.OnPropertyChanged(nameof(this.AlignCameraDisabledReason));
        this.OnPropertyChanged(nameof(this.CanAlignCamera));
        this.RaiseStateChanged();
    }

    /// <summary>Marks the option of each flyout list that describes the pane's current camera.</summary>
    private void UpdateCameraOptions()
    {
        var editorCamera = this.SceneCamera is null;
        for (var i = 0; i < PerspectiveModeValues.Length; i++)
        {
            this.PerspectiveModes[i].IsSelected = editorCamera && this.CameraType == CameraType.Perspective
                && this.CameraControlMode == PerspectiveModeValues[i];
        }

        for (var i = 0; i < OrthographicViewValues.Length; i++)
        {
            this.OrthographicViews[i].IsSelected = editorCamera && this.CameraType == OrthographicViewValues[i];
        }

        for (var i = 0; i < this.SceneCameraOptions.Count && i < this.sceneCameraChoices.Count; i++)
        {
            this.SceneCameraOptions[i].IsSelected = this.SceneCamera?.NodeId == this.sceneCameraChoices[i].NodeId;
        }
    }

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

        if (this.AssignedViewId.IsValid)
        {
            _ = await this.SendSceneCameraAsync(camera).ConfigureAwait(true);
        }

        // The inset hides while the pane itself looks through the previewed camera.
        await this.RunViewWorkAsync(this.ReconcileInsetCoreAsync).ConfigureAwait(true);
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

    [RelayCommand]
    private Task TogglePilot() => this.SetPilotAsync(!this.IsPilotingSceneCamera);

    /// <summary>Moves the selected camera to this viewport's editor camera, as one undoable edit.</summary>
    /// <returns>A task that completes when the request was handled.</returns>
    [RelayCommand]
    private async Task AlignSelectedCameraToView()
    {
        if (this.SelectedCameraProvider?.Invoke() is { } camera)
        {
            _ = await this.AlignCameraToViewAsync(camera).ConfigureAwait(true);
        }
    }

    /// <summary>Restores the lens defaults; navigation mode, speed and pose are unchanged.</summary>
    [RelayCommand]
    private void ResetLens()
    {
        this.FieldOfViewDegrees = DefaultFieldOfViewDegrees;
        this.NearViewPlane = DefaultNearViewPlane;
        this.FarViewPlane = DefaultFarViewPlane;
    }

    private async Task SetPilotAsync(bool pilot)
    {
        var camera = this.SceneCamera;
        if (pilot == this.IsPilotingSceneCamera || (pilot && (camera is null || this.IsLocked(camera.NodeId))))
        {
            return;
        }

        if (!pilot)
        {
            await this.FlushPilotPoseAsync().ConfigureAwait(true);
        }
        else
        {
            // One pane at a time pilots a camera: the others stop, committing their last gesture.
            if (this.PilotStarting is { } starting)
            {
                await starting(this, camera!.NodeId).ConfigureAwait(true);
            }

            if (this.CameraType != CameraType.Perspective)
            {
                // The runtime pilots through the perspective editor camera.
                this.CameraType = CameraType.Perspective;
            }
        }

        this.IsPilotingSceneCamera = pilot;
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

    private void ScheduleNavigationSettled(bool inputHeld)
    {
        this.CancelNavigationSettled();
        if (inputHeld)
        {
            return;
        }

        this.navigationSettleDelay = new CancellationTokenSource();
        _ = this.RaiseStateChangedAfterPauseAsync(this.navigationSettleDelay.Token);
    }

    private async Task RaiseStateChangedAfterPauseAsync(CancellationToken cancellationToken)
    {
        try
        {
            await Task.Delay(this.NavigationSettleDelay, cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return;
        }

        this.RaiseStateChanged();
    }

    private void CancelNavigationSettled()
    {
        this.navigationSettleDelay?.Cancel();
        this.navigationSettleDelay?.Dispose();
        this.navigationSettleDelay = null;
    }

    private void RaiseStateChanged() => this.StateChanged?.Invoke(this, EventArgs.Empty);

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

    private async Task ApplyCameraControlModeAsync(CameraControlMode mode)
    {
        this.CameraControlMode = mode;

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

    private void OnCameraNumberBoxValueChanged(string propertyName, float value)
    {
        this.OnPropertyChanged(propertyName);

        if (string.Equals(propertyName, nameof(this.MovementSpeed), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.FlySpeed));
            _ = this.ApplyCameraMovementSpeedAsync(value);
            return;
        }

        this.OnPropertyChanged(nameof(this.ClippingSummary));
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
