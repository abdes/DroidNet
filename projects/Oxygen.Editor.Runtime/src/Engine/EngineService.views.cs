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
        var runner = this.EnsureIsRunning();
        this.LogCreateView(config);
        return await runner.TryCreateViewAsync(this.engineContext, config).ConfigureAwait(true);
    }

    /// <inheritdoc/>
    public async Task<bool> DestroyViewAsync(ViewIdManaged viewId)
    {
        var runner = this.EnsureIsRunning();
        this.LogDestroyView(viewId);
        return await runner.TryDestroyViewAsync(this.engineContext, viewId).ConfigureAwait(true);
    }

    /// <inheritdoc/>
    public async Task<bool> ShowViewAsync(ViewIdManaged viewId)
    {
        var runner = this.EnsureIsRunning();
        this.LogShowView(viewId);
        return await runner.TryShowViewAsync(this.engineContext, viewId).ConfigureAwait(true);
    }

    /// <inheritdoc/>
    public async Task<bool> HideViewAsync(ViewIdManaged viewId)
    {
        var runner = this.EnsureIsRunning();
        this.LogHideView(viewId);
        return await runner.TryHideViewAsync(this.engineContext, viewId).ConfigureAwait(true);
    }

    /// <inheritdoc/>
    public async Task<bool> SetViewCameraPresetAsync(ViewIdManaged viewId, CameraViewPresetManaged preset)
    {
        var runner = this.EnsureIsRunning();
        this.LogSetViewCameraPreset(viewId, preset);
        return await runner.TrySetViewCameraPresetAsync(this.engineContext, viewId, preset).ConfigureAwait(true);
    }
}
