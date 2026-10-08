// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Diagnostics;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.WorldEditor.SceneEditor;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// The pane's heads-up display: what it renders (view mode, grid), what it overlays (camera
/// preview, statistics), the layout picker and the navigation hint.
/// </summary>
public partial class ViewportViewModel
{
    private const string OrbitGestureHint = "Alt+drag orbit · Alt+middle-drag pan · Alt+right-drag dolly · wheel zoom";
    private const string FlyGestureHint = "Right-drag look · WASD move · Q/E down/up · Shift faster";
    private const string OrthographicGestureHint = "Alt+middle-drag pan · wheel zoom";

    private static readonly (ViewportViewMode Mode, string Label, string? Description)[] ViewModeValues =
    [
        (ViewportViewMode.Lit, "Lit", "The fully lit scene"),
        (ViewportViewMode.Unlit, "Unlit", "Base color without lighting"),
        (ViewportViewMode.Wireframe, "Wireframe", "Geometry edges only"),
        (ViewportViewMode.LitWireframe, "Lit + wireframe", "Edges over the lit scene"),
        (ViewportViewMode.DirectLighting, "Direct", "Light from scene lights only"),
        (ViewportViewMode.IndirectLighting, "Indirect", "Light from the sky only"),
        (ViewportViewMode.WorldNormals, "World normals", null),
        (ViewportViewMode.Roughness, "Roughness", null),
        (ViewportViewMode.Metalness, "Metalness", null),
        (ViewportViewMode.LinearDepth, "Scene depth", null),
        (ViewportViewMode.ShadowMask, "Shadow mask", null),
    ];

    private bool hasNavigated;

    /// <summary>Gets or sets what the pane renders: the lit scene or one of its diagnostic views.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ViewModeLabel))]
    public partial ViewportViewMode ViewMode { get; set; } = ViewportViewMode.Lit;

    /// <summary>Gets or sets a value indicating whether the pane draws the ground grid.</summary>
    [ObservableProperty]
    public partial bool ShowGrid { get; set; } = true;

    /// <summary>Gets or sets a value indicating whether the focused pane previews the selected camera in an inset.</summary>
    [ObservableProperty]
    public partial bool ShowCameraPreview { get; set; } = true;

    /// <summary>Gets or sets a value indicating whether the pane shows the frame statistics readout.</summary>
    [ObservableProperty]
    public partial bool ShowStatistics { get; set; }

    /// <summary>Gets the frame statistics readout, refreshed by <see cref="RefreshStatistics"/>.</summary>
    [ObservableProperty]
    public partial string StatisticsText { get; private set; } = string.Empty;

    /// <summary>Gets or sets the scene's current pane arrangement, marked in the layout picker.</summary>
    [ObservableProperty]
    public partial SceneViewLayout CurrentLayout { get; set; } = SceneViewLayout.OnePane;

    /// <summary>Gets the view modes offered by the view mode flyout, in titled groups.</summary>
    public IReadOnlyList<ViewportOptionGroup> ViewModeGroups { get; }

    /// <summary>Gets the pane arrangements offered by the layout picker, in titled groups.</summary>
    public IReadOnlyList<ViewportLayoutGroup> LayoutGroups { get; }

    /// <summary>Gets the label of the view mode button.</summary>
    public string ViewModeLabel
        => ViewModeValues[Math.Max(0, Array.FindIndex(ViewModeValues, value => value.Mode == this.ViewMode))].Label;

    /// <summary>Gets the navigation gestures of the pane's current camera.</summary>
    public string GestureHint => this.SceneCamera is { } camera && !this.IsPilotingSceneCamera
        ? $"Looking through {camera.Name} · pilot it to move it"
        : this.SceneCamera is null && this.CameraType != CameraType.Perspective
            ? OrthographicGestureHint
            : this.CameraControlMode == CameraControlMode.Fly ? FlyGestureHint : OrbitGestureHint;

    /// <summary>
    /// Gets a value indicating whether the navigation hint shows: on the focused pane, until the
    /// user navigates with the current camera.
    /// </summary>
    public bool IsGestureHintVisible => this.IsFocused && !this.hasNavigated;

    /// <summary>Gets or sets the switch to another pane arrangement, requested from the layout picker.</summary>
    public Action<SceneViewLayout>? OnLayoutRequested { get; set; }

    /// <summary>Gets or sets the source of the scene's node count shown by the statistics readout.</summary>
    public Func<int>? NodeCountProvider { get; set; }

    /// <summary>Re-reads the engine frame statistics into <see cref="StatisticsText"/>.</summary>
    internal void RefreshStatistics()
    {
        var parts = new List<string>(3);
        if (this.EngineService.GetFrameStatistics() is { } frame)
        {
            parts.Add(string.Create(CultureInfo.CurrentCulture, $"{frame.FramesPerSecond:0} fps"));
            parts.Add(string.Create(CultureInfo.CurrentCulture, $"{frame.FrameTimeMilliseconds:0.0} ms"));
        }

        if (this.NodeCountProvider?.Invoke() is { } nodes)
        {
            parts.Add(string.Create(CultureInfo.CurrentCulture, $"{nodes:N0} {(nodes == 1 ? "node" : "nodes")}"));
        }

        this.StatisticsText = parts.Count == 0 ? "No frame statistics" : string.Join(" · ", parts);
    }

    /// <summary>Applies the pane's view mode and grid to the native view, when it has one.</summary>
    /// <returns>A task that completes when the options have been submitted.</returns>
    internal async Task ApplyCurrentRenderOptionsAsync()
    {
        if (!this.AssignedViewId.IsValid)
        {
            return;
        }

        try
        {
            var accepted = await this.EngineService.SetViewRenderOptionsAsync(this.AssignedViewId, this.ViewMode, this.ShowGrid).ConfigureAwait(true);
            if (!accepted)
            {
                this.PublishRuntimeWarning(
                    RuntimeOperationKinds.ViewSetRenderOptions,
                    FailureDomain.RuntimeView,
                    DiagnosticCodes.ViewPrefix + "RENDER_OPTIONS_REJECTED",
                    "View mode was not applied",
                    "The runtime rejected the view mode or grid for this viewport.");
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.PublishRuntimeFailure(
                RuntimeOperationKinds.ViewSetRenderOptions,
                FailureDomain.RuntimeView,
                DiagnosticCodes.ViewPrefix + "RENDER_OPTIONS_FAILED",
                "View mode failed",
                "The runtime could not apply the view mode or grid for this viewport.",
                ex);
        }
    }

    private static ViewportOptionGroup[] BuildViewModeGroups(Func<ViewportViewMode, Task> choose)
    {
        ViewportOption Option(int index)
        {
            var (mode, label, description) = ViewModeValues[index];
            return new ViewportOption(label, description, () => choose(mode));
        }

        return
        [
            new(Title: null, [Option(0), Option(1), Option(2), Option(3)]),
            new("Lighting", [Option(4), Option(5)]),
            new("Buffer visualization", [Option(6), Option(7), Option(8), Option(9), Option(10)]),
        ];
    }

    private ViewportLayoutGroup[] BuildLayoutGroups()
    {
        ViewportLayoutOption Option(SceneViewLayout layout, string label)
            => new(layout, label, requested => this.OnLayoutRequested?.Invoke(requested));

        return
        [
            new("Single and quad", [Option(SceneViewLayout.OnePane, "One pane"), Option(SceneViewLayout.FourQuad, "Four quadrants")]),
            new(
                "Two panes",
                [
                    Option(SceneViewLayout.TwoMainLeft, "Main left"),
                    Option(SceneViewLayout.TwoMainRight, "Main right"),
                    Option(SceneViewLayout.TwoMainTop, "Main top"),
                    Option(SceneViewLayout.TwoMainBottom, "Main bottom"),
                ]),
            new(
                "Three panes",
                [
                    Option(SceneViewLayout.ThreeMainLeft, "Main left"),
                    Option(SceneViewLayout.ThreeMainRight, "Main right"),
                    Option(SceneViewLayout.ThreeMainTop, "Main top"),
                    Option(SceneViewLayout.ThreeMainBottom, "Main bottom"),
                ]),
            new(
                "Four panes",
                [
                    Option(SceneViewLayout.FourMainLeft, "Main left"),
                    Option(SceneViewLayout.FourMainRight, "Main right"),
                    Option(SceneViewLayout.FourMainTop, "Main top"),
                    Option(SceneViewLayout.FourMainBottom, "Main bottom"),
                ]),
        ];
    }

    private Task ApplyViewModeAsync(ViewportViewMode mode)
    {
        this.ViewMode = mode;
        return Task.CompletedTask;
    }

    /// <summary>Hides the navigation hint once the user navigates with the current camera.</summary>
    private void MarkNavigated()
    {
        if (this.hasNavigated)
        {
            return;
        }

        this.hasNavigated = true;
        this.OnPropertyChanged(nameof(this.IsGestureHintVisible));
    }

    /// <summary>Shows the navigation hint again: the pane's camera, and so its gestures, changed.</summary>
    private void ResetGestureHint()
    {
        if (!this.hasNavigated)
        {
            return;
        }

        this.hasNavigated = false;
        this.OnPropertyChanged(nameof(this.IsGestureHintVisible));
    }

    private void UpdateViewModeOptions()
    {
        var index = 0;
        foreach (var option in this.ViewModeGroups.SelectMany(group => group.Options))
        {
            option.IsSelected = ViewModeValues[index++].Mode == this.ViewMode;
        }
    }

    private void UpdateLayoutOptions()
    {
        foreach (var option in this.LayoutGroups.SelectMany(group => group.Options))
        {
            option.IsSelected = option.Layout == this.CurrentLayout;
        }
    }

    partial void OnViewModeChanged(ViewportViewMode value)
    {
        this.UpdateViewModeOptions();
        _ = this.ApplyCurrentRenderOptionsAsync();
        this.RaiseStateChanged();
    }

    partial void OnShowGridChanged(bool value)
    {
        _ = this.ApplyCurrentRenderOptionsAsync();
        this.RaiseStateChanged();
    }

    partial void OnShowCameraPreviewChanged(bool value)
    {
        _ = this.RunViewWorkAsync(this.ReconcileInsetCoreAsync);
        this.RaiseStateChanged();
    }

    partial void OnShowStatisticsChanged(bool value)
    {
        if (value)
        {
            this.RefreshStatistics();
        }

        this.RaiseStateChanged();
    }

    partial void OnCurrentLayoutChanged(SceneViewLayout value) => this.UpdateLayoutOptions();
}
