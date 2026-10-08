// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DryIoc;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.SceneEditor;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// The scene's viewport panes: layout, focus, maximize, the camera preview inset and the state kept
/// across sessions.
/// </summary>
/// <remarks>
/// Maximize is presentation only: the layout and pane indexes stay unchanged, and the hidden panes
/// release their views but keep their state. A layout change creates and releases only the panes
/// that change.
/// </remarks>
public partial class SceneEditorViewModel
{
    private SceneCameraChoice? selectedCamera;
    private long viewportRestoreGeneration;

    /// <summary>
    /// Gets the pane shown alone while maximized, or <see langword="null"/> when the layout shows
    /// every pane.
    /// </summary>
    [ObservableProperty]
    public partial ViewportViewModel? MaximizedViewport { get; private set; }

    private ViewportStateService? ViewportStates
        => this.container.Resolve<ViewportStateService>(IfUnresolved.ReturnDefault);

    private ProjectContext? ActiveProject
        => this.container.Resolve<IProjectContextService>(IfUnresolved.ReturnDefault)?.ActiveProject;

    /// <summary>
    /// Marks the given viewport as focused and clears focus from all other viewports.
    /// </summary>
    /// <param name="viewport">The viewport to focus.</param>
    public void SetFocusedViewport(ViewportViewModel viewport)
    {
        ArgumentNullException.ThrowIfNull(viewport);
        if (this.FocusedViewportId == viewport.ViewportId)
        {
            return;
        }

        this.FocusedViewportId = viewport.ViewportId;
        this.ApplyFocusedViewportFlags();
        this.SaveViewportState();
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

    partial void OnCurrentLayoutChanged(SceneViewLayout value)
    {
        if (this.sceneReady)
        {
            this.SaveViewportState();
        }
    }

    /// <summary>
    /// Creates the panes once the scene is in the engine. A scene opening without panes restores the
    /// layout, focused pane and pane cameras kept for it; the panes of a reloaded scene keep theirs.
    /// </summary>
    private async Task RestoreViewportsAsync()
    {
        var generation = ++this.viewportRestoreGeneration;
        this.selectedCamera = this.GetSelectedCamera();
        var stored = this.Viewports.Count == 0 && this.ViewportStates is { } states && this.ActiveProject is { } project
            ? await states.RestoreAsync(project, this.Metadata.DocumentId).ConfigureAwait(true)
            : null;
        if (generation != this.viewportRestoreGeneration || this.isDisposed)
        {
            return;
        }

        if (stored is not null)
        {
            // Deferred by the layout hook: the panes are created below with their kept state.
            this.CurrentLayout = stored.Layout;
        }

        this.sceneReady = true;
        this.UpdateLayout(this.CurrentLayout, stored?.Panes);
        if (stored is not null && stored.FocusedPane < this.Viewports.Count)
        {
            this.FocusedViewportId = this.Viewports[stored.FocusedPane].ViewportId;
            this.ApplyFocusedViewportFlags();
        }
    }

    private void UpdateLayout(SceneViewLayout targetLayout, IReadOnlyList<ViewportPaneState>? restoredPanes = null)
    {
        var metadata = this.Metadata ?? throw new InvalidOperationException("Scene metadata is not initialized.");
        metadata.Layout = targetLayout;
        this.MaximizedViewport = null;

        var requiredCount = SceneLayoutHelpers.GetPlacements(targetLayout).Count;
        while (this.Viewports.Count < requiredCount)
        {
            var index = this.Viewports.Count;
            var viewport = this.CreateViewport(metadata.DocumentId);
            if (restoredPanes is not null && index < restoredPanes.Count)
            {
                this.RestorePane(viewport, index, restoredPanes[index]);
            }

            viewport.StateChanged += this.Viewport_StateChanged;
            this.LogCreatingViewport(index, viewport);
            this.Viewports.Add(viewport);
        }

        // Panes past the new layout go for good; the surviving panes keep their state.
        while (this.Viewports.Count > requiredCount)
        {
            var removed = this.Viewports[^1];
            this.Viewports.RemoveAt(this.Viewports.Count - 1);
            this.ReleaseViewport(removed);
        }

        for (var i = 0; i < this.Viewports.Count; i++)
        {
            var viewport = this.Viewports[i];
            viewport.IsMaximized = false;
            viewport.CurrentLayout = targetLayout;

            // The first viewport is considered the main camera
            viewport.UpdateLayoutMetadata(i, i == 0);
        }

        this.EnsureFocusedViewportIsValid();
    }

    private ViewportViewModel CreateViewport(Guid documentId)
    {
        var viewport = new ViewportViewModel(
            documentId,
            this.engineService,
            this.operationResults,
            this.statusReducer,
            this.loggerFactory);
        viewport.ToggleMaximizeCommand = new RelayCommand(() => this.ToggleMaximize(viewport));
        viewport.OnLayoutRequested = requestedLayout => this.ChangeLayoutCommand.Execute(requestedLayout);
        viewport.PilotStarting = this.StopOtherPilotsAsync;
        viewport.NodeCountProvider = () => this.scene?.AllNodes.Count() ?? 0;
        this.AttachCameraServices(viewport);
        this.AttachSelectionServices(viewport);
        return viewport;
    }

    private void RestorePane(ViewportViewModel viewport, int index, ViewportPaneState pane)
    {
        SceneCameraChoice? camera = null;
        if (pane.SceneCameraId is { } cameraId)
        {
            camera = this.GetSceneCameras().FirstOrDefault(choice => choice.NodeId == cameraId);
            if (camera is null)
            {
                this.LogRestoredCameraMissing(index, cameraId);
            }
        }

        viewport.RestoreState(pane, camera);
    }

    private void ReleaseViewport(ViewportViewModel viewport)
    {
        viewport.StateChanged -= this.Viewport_StateChanged;
        viewport.Dispose();
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

        this.UpdateInsets();
    }

    /// <summary>The focused pane previews the selected camera; the other panes show no inset.</summary>
    private void UpdateInsets()
    {
        foreach (var viewport in this.Viewports)
        {
            _ = viewport.SetInsetCameraAsync(viewport.IsFocused ? this.selectedCamera : null);
        }
    }

    private void ToggleMaximize(ViewportViewModel viewport)
    {
        this.MaximizedViewport = ReferenceEquals(this.MaximizedViewport, viewport) ? null : viewport;
        foreach (var pane in this.Viewports)
        {
            pane.IsMaximized = ReferenceEquals(pane, this.MaximizedViewport);
        }

        this.SetFocusedViewport(viewport);
    }

    private async Task StopOtherPilotsAsync(ViewportViewModel pilot, Guid cameraId)
    {
        var others = this.Viewports
            .Where(viewport => !ReferenceEquals(viewport, pilot)
                && viewport.IsPilotingSceneCamera && viewport.SceneCamera?.NodeId == cameraId)
            .ToList();
        foreach (var other in others)
        {
            await other.StopPilotingAsync().ConfigureAwait(true);
        }
    }

    private void Viewport_StateChanged(object? sender, EventArgs e)
    {
        if (sender is ViewportViewModel viewport)
        {
            _ = this.SaveViewportStateAsync(viewport);
        }
    }

    private async Task SaveViewportStateAsync(ViewportViewModel changed)
    {
        await changed.RefreshEditorCameraAsync().ConfigureAwait(true);
        this.SaveViewportState();
    }

    private async Task SaveViewportStateForCloseAsync()
    {
        foreach (var viewport in this.Viewports.ToList())
        {
            await viewport.RefreshEditorCameraAsync().ConfigureAwait(true);
        }

        this.SaveViewportState();
        if (this.ViewportStates is { } states)
        {
            await states.FlushAsync().ConfigureAwait(true);
        }
    }

    private void SaveViewportState()
    {
        if (!this.sceneReady || this.isDisposed || this.Viewports.Count == 0
            || this.ViewportStates is not { } states || this.ActiveProject is not { } project)
        {
            return;
        }

        var focused = this.Viewports.ToList().FindIndex(viewport => viewport.ViewportId == this.FocusedViewportId);
        states.Update(
            project,
            this.Metadata.DocumentId,
            new SceneViewportState(this.CurrentLayout, Math.Max(focused, 0), [.. this.Viewports.Select(static viewport => viewport.CaptureState())]));
    }

    [RelayCommand]
    private void ChangeLayout(SceneViewLayout layout) => this.CurrentLayout = layout;

    /// <summary>Moves the selected camera to the focused pane's editor camera (Ctrl+Shift+F).</summary>
    [RelayCommand]
    private async Task AlignSelectedCameraToView()
    {
        if (this.GetActiveViewport() is { } viewport)
        {
            await viewport.AlignSelectedCameraToViewCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        }
    }
}
