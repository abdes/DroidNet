// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// EngineService: view-management partial implementation.
/// Keeps view lifecycle operations separated from the core engine startup/lease logic.
/// </summary>
public sealed partial class EngineService
{
    /// <inheritdoc/>
    public async Task<ViewIdManaged> CreateViewAsync(ViewConfigManaged config)
    {
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogCreateView(config);
            var viewId = await this.AwaitRuntimeOperationAsync(runner.TryCreateViewAsync(this.EngineContext, config)).ConfigureAwait(true);
            if (viewId.IsValid && config.CompositingTarget is { } viewportId)
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
    public async Task<bool> DestroyViewAsync(ViewIdManaged viewId)
    {
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.commandDispatcher.UnregisterView(viewId.Value);
            this.LogDestroyView(viewId);
            return await this.AwaitRuntimeOperationAsync(runner.TryDestroyViewAsync(this.EngineContext, viewId)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> ShowViewAsync(ViewIdManaged viewId)
    {
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogShowView(viewId);
            return await this.AwaitRuntimeOperationAsync(runner.TryShowViewAsync(this.EngineContext, viewId)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> HideViewAsync(ViewIdManaged viewId)
    {
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogHideView(viewId);
            return await this.AwaitRuntimeOperationAsync(runner.TryHideViewAsync(this.EngineContext, viewId)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> SetViewCameraPresetAsync(ViewIdManaged viewId, CameraViewPresetManaged preset)
    {
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogSetViewCameraPreset(viewId, preset);
            return await this.AwaitRuntimeOperationAsync(runner.TrySetViewCameraPresetAsync(this.EngineContext, viewId, preset)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> SetViewCameraControlModeAsync(ViewIdManaged viewId, CameraControlModeManaged mode)
    {
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogSetViewCameraControlMode(viewId, mode);
            return await this.AwaitRuntimeOperationAsync(runner.TrySetViewCameraControlModeAsync(this.EngineContext, viewId, mode)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> SetViewCameraMovementSpeedAsync(ViewIdManaged viewId, float speedUnitsPerSecond)
    {
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogSetViewCameraMovementSpeed(viewId, speedUnitsPerSecond);
            return await this.AwaitRuntimeOperationAsync(runner.TrySetViewCameraMovementSpeedAsync(this.EngineContext, viewId, speedUnitsPerSecond)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }

    /// <inheritdoc/>
    public async Task<bool> SetViewCameraSettingsAsync(ViewIdManaged viewId, float fieldOfViewDegrees, float nearPlane, float farPlane)
    {
        await this.lifecycleGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var runner = this.EnsureIsRunning();
            this.LogSetViewCameraSettings(viewId, fieldOfViewDegrees, nearPlane, farPlane);
            return await this.AwaitRuntimeOperationAsync(runner.TrySetViewCameraSettingsAsync(this.EngineContext, viewId, fieldOfViewDegrees, nearPlane, farPlane)).ConfigureAwait(true);
        }
        finally
        {
            _ = this.lifecycleGate.Release();
        }
    }
}
