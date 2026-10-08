// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// EngineService: view-management partial implementation.
/// Keeps view lifecycle operations separated from the core engine startup/lease logic.
/// </summary>
public sealed partial class EngineService
{
    /// <inheritdoc/>
    public async Task<RuntimeViewId> CreateViewAsync(RuntimeViewConfig config)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogCreateView(config);
            var viewId = await this.AwaitRuntimeOperationAsync(runner.CreateViewAsync(config)).ConfigureAwait(true);
            // A camera preview inset receives no input: its host stays the viewport's input target.
            if (viewId.IsValid && config.InsetHost is null && config.CompositingTarget is { } viewportId)
            {
                var lease = this.activeLeases.Values.FirstOrDefault(value => value.Key.ViewportId == viewportId);
                if (lease is not null)
                {
                    this.commandDispatcher.RegisterView(viewId.Value, lease.Key.DocumentId, viewportId);
                }
            }

            return viewId;
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> DestroyViewAsync(RuntimeViewId viewId)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            this.commandDispatcher.UnregisterView(viewId.Value);
            if (this.disposalRequested || this.State != EngineServiceState.Running)
            {
                // Views exist only in a running engine; a stopped engine released them all.
                this.LogViewReleasedWithEngine(viewId, this.State);
                return true;
            }

            this.LogDestroyView(viewId);
            return await this.AwaitRuntimeOperationAsync(this.session!.DestroyViewAsync(viewId)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> ShowViewAsync(RuntimeViewId viewId)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogShowView(viewId);
            return await this.AwaitRuntimeOperationAsync(runner.ShowViewAsync(viewId)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> HideViewAsync(RuntimeViewId viewId)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogHideView(viewId);
            return await this.AwaitRuntimeOperationAsync(runner.HideViewAsync(viewId)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> SetViewCameraPresetAsync(RuntimeViewId viewId, CameraViewPreset preset)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogSetViewCameraPreset(viewId, preset);
            return await this.AwaitRuntimeOperationAsync(runner.SetViewCameraPresetAsync(viewId, preset)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> SetViewSceneCameraAsync(RuntimeViewId viewId, Guid? cameraNodeId)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogSetViewSceneCamera(viewId, cameraNodeId);
            return await this.AwaitRuntimeOperationAsync(runner.SetViewSceneCameraAsync(viewId, cameraNodeId)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> SetViewScenePilotAsync(RuntimeViewId viewId, bool pilot)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogSetViewScenePilot(viewId, pilot);
            return await this.AwaitRuntimeOperationAsync(runner.SetViewScenePilotAsync(viewId, pilot)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<RuntimeViewCameraPose?> GetViewCameraPoseAsync(RuntimeViewId viewId, Guid nodeId)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            return await this.AwaitRuntimeOperationAsync(runner.GetViewCameraPoseAsync(viewId, nodeId)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<RuntimeEditorCamera?> GetViewEditorCameraAsync(RuntimeViewId viewId)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            return await this.AwaitRuntimeOperationAsync(runner.GetViewEditorCameraAsync(viewId)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> SetViewCameraControlModeAsync(RuntimeViewId viewId, CameraControlMode mode)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogSetViewCameraControlMode(viewId, mode);
            return await this.AwaitRuntimeOperationAsync(runner.SetViewCameraControlModeAsync(viewId, mode)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> SetViewCameraMovementSpeedAsync(RuntimeViewId viewId, float speedUnitsPerSecond)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogSetViewCameraMovementSpeed(viewId, speedUnitsPerSecond);
            return await this.AwaitRuntimeOperationAsync(runner.SetViewCameraMovementSpeedAsync(viewId, speedUnitsPerSecond)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> SetViewCameraSettingsAsync(RuntimeViewId viewId, float fieldOfViewDegrees, float nearPlane, float farPlane)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogSetViewCameraSettings(viewId, fieldOfViewDegrees, nearPlane, farPlane);
            return await this.AwaitRuntimeOperationAsync(runner.SetViewCameraSettingsAsync(viewId, fieldOfViewDegrees, nearPlane, farPlane)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> SetViewRenderOptionsAsync(RuntimeViewId viewId, ViewportRenderOptions options)
    {
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogSetViewRenderOptions(viewId, options);
            return await this.AwaitRuntimeOperationAsync(runner.SetViewRenderOptionsAsync(viewId, options)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<RuntimePickResult?> PickViewAsync(RuntimeViewId viewId, RuntimePickRect rect)
    {
        Task<RuntimePickResult?> pick;
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            pick = runner.PickViewAsync(viewId, rect);
        }
        finally
        {
            // The result lands frames later: other view requests need not wait for it.
            _ = this.lifecycleGate.Release();
        }

        return await this.AwaitRuntimeOperationAsync(pick).ConfigureAwait(true);
    }

    /// <inheritdoc/>
    public async Task<RuntimeFramingOutcome> FrameViewAsync(RuntimeViewId viewId, IReadOnlyList<Guid> nodeIds)
    {
        ArgumentNullException.ThrowIfNull(nodeIds);
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            return await this.AwaitRuntimeOperationAsync(runner.FrameViewAsync(viewId, nodeIds)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> SetSelectionOutlineAsync(IReadOnlyList<Guid> nodeIds, Guid? activeNodeId)
    {
        ArgumentNullException.ThrowIfNull(nodeIds);
        await this.lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            return this.State == EngineServiceState.Running && this.session is { } running
                && running.SetSelectionOutline(nodeIds, activeNodeId);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public RuntimeFrameStatistics? GetFrameStatistics()
        => this.State == EngineServiceState.Running && this.session is { } running ? running.GetFrameStatistics() : null;
}
