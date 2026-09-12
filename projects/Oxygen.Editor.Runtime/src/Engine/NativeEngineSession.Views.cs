// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Converts managed view requests at the native session boundary.</summary>
internal sealed partial class NativeEngineSession
{
    /// <inheritdoc />
    public override async Task<RuntimeViewId> CreateViewAsync(RuntimeViewConfig config)
    {
        var result = await this.Runner.TryCreateViewAsync(this.context, NativeSessionConversions.ToNative(config)).ConfigureAwait(false);
        return new(result.Value);
    }

    /// <inheritdoc />
    public override Task<bool> DestroyViewAsync(RuntimeViewId viewId) => this.Runner.TryDestroyViewAsync(this.context, new(viewId.Value));

    /// <inheritdoc />
    public override Task<bool> ShowViewAsync(RuntimeViewId viewId) => this.Runner.TryShowViewAsync(this.context, new(viewId.Value));

    /// <inheritdoc />
    public override Task<bool> HideViewAsync(RuntimeViewId viewId) => this.Runner.TryHideViewAsync(this.context, new(viewId.Value));

    /// <inheritdoc />
    public override Task<bool> SetViewCameraPresetAsync(RuntimeViewId viewId, CameraViewPreset preset)
        => this.Runner.TrySetViewCameraPresetAsync(this.context, new(viewId.Value), NativeSessionConversions.ToNative<CameraViewPresetManaged>(preset));

    /// <inheritdoc />
    public override Task<bool> SetViewCameraControlModeAsync(RuntimeViewId viewId, CameraControlMode mode)
        => this.Runner.TrySetViewCameraControlModeAsync(this.context, new(viewId.Value), NativeSessionConversions.ToNative<CameraControlModeManaged>(mode));

    /// <inheritdoc />
    public override Task<bool> SetViewCameraMovementSpeedAsync(RuntimeViewId viewId, float speedUnitsPerSecond)
        => this.Runner.TrySetViewCameraMovementSpeedAsync(this.context, new(viewId.Value), speedUnitsPerSecond);

    /// <inheritdoc />
    public override Task<bool> SetViewCameraSettingsAsync(RuntimeViewId viewId, float fieldOfViewDegrees, float nearPlane, float farPlane)
        => this.Runner.TrySetViewCameraSettingsAsync(this.context, new(viewId.Value), fieldOfViewDegrees, nearPlane, farPlane);
}
