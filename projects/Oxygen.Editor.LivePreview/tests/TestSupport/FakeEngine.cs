// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Tests.TestSupport;

/// <summary>
/// The embedded engine as LivePreview uses it: a test drives its state, which raises <see cref="StateChanged"/>, and
/// reads what LivePreview asked of it. Viewports are not LivePreview's, so their members are not supported.
/// </summary>
/// <param name="state">The engine's state.</param>
internal sealed class FakeEngine(EngineServiceState state = EngineServiceState.Running) : IEngineService
{
    private EngineServiceState state = state;

    /// <inheritdoc/>
    public event EventHandler<EngineStateChangedEventArgs>? StateChanged;

    /// <inheritdoc/>
    public event EventHandler<RuntimeContentChangedEventArgs>? ContentStatusChanged;

    /// <inheritdoc/>
    public event EventHandler<RuntimeGizmoEventArgs>? GizmoEvent
    {
        add { }
        remove { }
    }

    /// <summary>Gets or sets the world commands, which the engine always gives, as the engine service does.</summary>
    public FakeWorld World { get; set; } = new();

    /// <summary>Gets or sets a value indicating whether the engine gives its world commands at all.</summary>
    public bool HasWorld { get; set; } = true;

    /// <summary>Gets how many times the engine's state was read.</summary>
    public int StateReads { get; private set; }

    /// <summary>Gets how many times the world was asked for.</summary>
    public int WorldReads { get; private set; }

    /// <summary>Gets or sets the failure of the next start, if any.</summary>
    public Exception? StartFailure { get; set; }

    /// <summary>Gets how many times the engine was started.</summary>
    public int Starts { get; private set; }

    /// <summary>Gets every set of cooked roots the engine was given, in order.</summary>
    public List<IReadOnlyList<RuntimeCookedRoot>> MountedRoots { get; } = [];

    /// <summary>Gets how many times cooked content was suspended.</summary>
    public int Suspensions { get; private set; }

    /// <summary>Gets how many times cooked content was resumed.</summary>
    public int Resumptions { get; private set; }

    /// <summary>Gets the preview settings the engine took, in order: their names and values.</summary>
    public List<(string Setting, object Value)> Settings { get; } = [];

    /// <inheritdoc/>
    public EngineServiceState State
    {
        get
        {
            ++this.StateReads;
            return this.state;
        }
    }

    /// <inheritdoc/>
    public RuntimeContentSnapshot ContentStatus { get; set; } = new(Guid.Empty, 0, RuntimeContentState.Unmounted, []);

    /// <inheritdoc/>
    public int EngineLoggingVerbosity { get; set; }

    /// <inheritdoc/>
    public uint MaxTargetFps { get; set; } = 240;

    /// <inheritdoc/>
    public uint TargetFps
    {
        get;
        set
        {
            this.RequireLive();
            field = value;
            this.Settings.Add((nameof(this.TargetFps), value));
        }
    }

    /// <inheritdoc/>
    public int ActiveSurfaceCount => 0;

    /// <inheritdoc/>
    public IRuntimeWorldCommands WorldCommands
    {
        get
        {
            ++this.WorldReads;
            return this.HasWorld ? this.World : null!;
        }
    }

    /// <inheritdoc/>
    public IRuntimeInputCommands InputCommands => throw new NotSupportedException("LivePreview sends no input.");

    /// <summary>Moves the engine to a state and raises <see cref="StateChanged"/>, as the engine service does.</summary>
    /// <param name="next">The new state.</param>
    /// <param name="result">The result the engine service publishes with the change, as it does for a run that ends unexpectedly.</param>
    public void ChangeState(EngineServiceState next, OperationResult? result = null)
    {
        var previous = this.state;
        this.state = next;
        if (next == EngineServiceState.Running)
        {
            this.World.RunId = Guid.NewGuid();
        }

        this.StateChanged?.Invoke(this, new EngineStateChangedEventArgs(this.World.RunId, previous, next, result));
    }

    /// <summary>Sets the engine's state without raising <see cref="StateChanged"/>, as a test arranging it does.</summary>
    /// <param name="next">The state.</param>
    public void SetState(EngineServiceState next) => this.state = next;

    /// <summary>Raises <see cref="StateChanged"/> for the current state and run, as a repeated notification does.</summary>
    public void RaiseStateChanged()
        => this.StateChanged?.Invoke(this, new EngineStateChangedEventArgs(this.World.RunId, this.state, this.state));

    /// <summary>Raises <see cref="ContentStatusChanged"/> with the current content status.</summary>
    public void RaiseContentStatusChanged() => this.ContentStatusChanged?.Invoke(this, new RuntimeContentChangedEventArgs(this.ContentStatus));

    /// <inheritdoc/>
    public Task RefreshProjectCookedRootsAsync(IReadOnlyList<RuntimeCookedRoot> bindings, IDisposable? readLease = null, bool keepPaused = false)
    {
        this.MountedRoots.Add(bindings);
        this.ContentStatus = new(this.World.RunId, this.ContentStatus.Revision + 1, bindings.Count == 0 ? RuntimeContentState.Unmounted : RuntimeContentState.Mounted, [.. bindings]);
        readLease?.Dispose();
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public void SetVSyncEnabled(bool enabled)
    {
        this.RequireLive();
        this.Settings.Add((nameof(this.SetVSyncEnabled), enabled));
    }

    /// <inheritdoc/>
    public void SetAlwaysRenderPanes(bool alwaysRender)
    {
        this.RequireLive();
        this.Settings.Add((nameof(this.SetAlwaysRenderPanes), alwaysRender));
    }

    /// <inheritdoc/>
    public void SetGroundGrid(GroundGridSettings settings)
    {
        this.RequireLive();
        this.Settings.Add((nameof(this.SetGroundGrid), settings));
    }

    /// <inheritdoc/>
    public Task SuspendCookedContentAsync()
    {
        ++this.Suspensions;
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task ResumeCookedContentAsync()
    {
        ++this.Resumptions;
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public ValueTask<bool> InitializeAsync(CancellationToken cancellationToken = default)
    {
        if (this.state is EngineServiceState.NoEngine or EngineServiceState.Faulted)
        {
            this.ChangeState(EngineServiceState.Ready);
        }

        return ValueTask.FromResult(true);
    }

    /// <inheritdoc/>
    public ValueTask StartAsync()
    {
        ++this.Starts;
        if (this.StartFailure is { } failure)
        {
            this.StartFailure = null;
            this.ChangeState(EngineServiceState.Faulted);
            return ValueTask.FromException(failure);
        }

        this.ChangeState(EngineServiceState.Running);
        return ValueTask.CompletedTask;
    }

    /// <inheritdoc/>
    public ValueTask ShutdownAsync()
    {
        this.ChangeState(EngineServiceState.NoEngine);
        return ValueTask.CompletedTask;
    }

    /// <inheritdoc/>
    public ValueTask DisposeAsync() => ValueTask.CompletedTask;

    /// <inheritdoc/>
    public ValueTask<IViewportSurfaceLease> AttachViewportAsync(ViewportSurfaceRequest request, SwapChainPanel panel, CancellationToken cancellationToken = default) => throw Viewports();

    /// <inheritdoc/>
    public ValueTask ReleaseOwnerSurfacesAsync(SurfaceOwnerId owner) => throw Viewports();

    /// <inheritdoc/>
    public Task<RuntimeViewId> CreateViewAsync(RuntimeViewConfig config) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> DestroyViewAsync(RuntimeViewId viewId) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> ShowViewAsync(RuntimeViewId viewId) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> HideViewAsync(RuntimeViewId viewId) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> SetViewCameraPresetAsync(RuntimeViewId viewId, CameraViewPreset preset) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> SetViewCameraControlModeAsync(RuntimeViewId viewId, CameraControlMode mode) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> SetViewSceneCameraAsync(RuntimeViewId viewId, NodeId? cameraNodeId) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> SetViewScenePilotAsync(RuntimeViewId viewId, bool pilot) => throw Viewports();

    /// <inheritdoc/>
    public Task<RuntimeViewCameraPose?> GetViewCameraPoseAsync(RuntimeViewId viewId, NodeId nodeId) => throw Viewports();

    /// <inheritdoc/>
    public Task<RuntimeEditorCamera?> GetViewEditorCameraAsync(RuntimeViewId viewId) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> SetViewCameraMovementSpeedAsync(RuntimeViewId viewId, float speedUnitsPerSecond) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> SetViewCameraSettingsAsync(RuntimeViewId viewId, float fieldOfViewDegrees, float nearPlane, float farPlane) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> SetViewRenderOptionsAsync(RuntimeViewId viewId, ViewportRenderOptions options) => throw Viewports();

    /// <inheritdoc/>
    public Task<RuntimePickResult?> PickViewAsync(RuntimeViewId viewId, RuntimePickRect rect) => throw Viewports();

    /// <inheritdoc/>
    public Task<RuntimeFramingOutcome> FrameViewAsync(RuntimeViewId viewId, IReadOnlyList<NodeReference> nodes) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> SetSelectionOutlineAsync(IReadOnlyList<NodeReference> nodes, NodeReference? activeNode) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> SetTransformGizmoAsync(RuntimeTransformGizmo gizmo) => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> CancelTransformGizmoDragAsync() => throw Viewports();

    /// <inheritdoc/>
    public Task<bool> SetSceneHelpersAsync(RuntimeSceneHelpers helpers) => throw Viewports();

    /// <inheritdoc/>
    public RuntimeFrameStatistics? GetFrameStatistics() => null;

    private static NotSupportedException Viewports() => new("Viewports are the scene editor's, not LivePreview's.");

    private void RequireLive()
    {
        if (this.state is not (EngineServiceState.Ready or EngineServiceState.Running))
        {
            throw new InvalidOperationException("The engine takes settings only while it is ready or running.");
        }
    }
}
