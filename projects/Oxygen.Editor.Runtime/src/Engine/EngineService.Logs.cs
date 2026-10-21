// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Logging helpers for <see cref="EngineService"/>.</summary>
public sealed partial class EngineService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Native artifact compatibility failed: {Message}")]
    private static partial void LogCompatibilityFailed(ILogger logger, string message, Exception? exception);

    private void LogCompatibilityFailed(string message, Exception? exception) => LogCompatibilityFailed(this.logger, message, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to resize composition surface to {Width}x{Height}.")]
    private static partial void LogResizeFailed(ILogger logger, uint width, uint height, Exception? exception);

    private void LogResizeFailed(uint width, uint height, Exception? exception = null)
        => LogResizeFailed(this.logger, width, height, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Starting engine loop.")]
    private static partial void LogStartingEngineLoop(ILogger logger);

    private void LogStartingEngineLoop()
        => LogStartingEngineLoop(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Headless engine context created.")]
    private static partial void LogContextReady(ILogger logger);

    private void LogContextReady()
        => LogContextReady(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Engine shutdown requested in state {State}.")]
    private static partial void LogShutdownRequested(ILogger logger, EngineServiceState state);

    private void LogShutdownRequested()
        => LogShutdownRequested(this.logger, this.state);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Engine cleanup failed during {Stage}.")]
    private static partial void LogCleanupFailure(ILogger logger, Exception exception, string stage);

    private void LogCleanupFailure(Exception exception, string stage)
        => LogCleanupFailure(this.logger, exception, stage);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Engine run {RunId} terminated unexpectedly: {Reason}")]
    private static partial void LogLoopTerminated(ILogger logger, Guid runId, string reason, Exception? exception);

    private void LogLoopTerminated(Guid runId, string reason, Exception? exception)
        => LogLoopTerminated(this.logger, runId, reason, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "A runtime lifecycle notification consumer failed.")]
    private static partial void LogStateNotificationFailed(ILogger logger, Exception exception);

    private void LogStateNotificationFailed(Exception exception)
        => LogStateNotificationFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Engine target FPS set to {Fps}.")]
    private static partial void LogTargetFpsSet(ILogger logger, uint fps);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Engine target FPS set to {Requested} (clamped from {Clamped}).")]
    private static partial void LogTargetFpsClamped(ILogger logger, uint requested, uint clamped);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogTargetFpsSet(uint fps, uint clamped)
    {
        if (fps != clamped)
        {
            LogTargetFpsClamped(this.logger, fps, clamped);
        }
        else
        {
            LogTargetFpsSet(this.logger, fps);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Engine vsync set to {Enabled}.")]
    private static partial void LogVSyncSet(ILogger logger, bool enabled);

    private void LogVSyncSet(bool enabled)
        => LogVSyncSet(this.logger, enabled);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Viewport panes always render: {AlwaysRender}.")]
    private static partial void LogAlwaysRenderPanesSet(ILogger logger, bool alwaysRender);

    private void LogAlwaysRenderPanesSet(bool alwaysRender)
        => LogAlwaysRenderPanesSet(this.logger, alwaysRender);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Ground grid set: {Settings}.")]
    private static partial void LogGroundGridSet(ILogger logger, GroundGridSettings settings);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogGroundGridSet(GroundGridSettings settings) => LogGroundGridSet(this.logger, settings);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Engine logging verbosity set to {Verbosity}.")]
    private static partial void LogLoggingVerbositySet(ILogger logger, int verbosity);

    private void LogLoggingVerbositySet(int verbosity)
        => LogLoggingVerbositySet(this.logger, verbosity);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to set engine logging verbosity to {Verbosity}.")]
    private static partial void LogSetLoggingVerbosityFailed(ILogger logger, int verbosity, Exception? exception);

    private void LogSetLoggingVerbosityFailed(int verbosity, Exception? exception = null)
        => LogSetLoggingVerbosityFailed(this.logger, verbosity, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The changed logging verbosity {Verbosity} was saved but not applied to the running engine; it applies at the next start.")]
    private static partial void LogLiveVerbosityNotApplied(ILogger logger, int verbosity, Exception exception);

    private void LogLiveVerbosityNotApplied(int verbosity, Exception exception)
        => LogLiveVerbosityNotApplied(this.logger, verbosity, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting view ('{Name}'/'{Purpose}') from the engine: Extent={Width}x{Height}, {TargetInfo}")]
    private static partial void LogCreateView(ILogger logger, string name, string purpose, uint? width, uint? height, string targetInfo);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogCreateView(RuntimeViewConfig config)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var targetInfo = config.CompositingTarget != null ? $"composing to: {config.CompositingTarget}" : "without target";
        LogCreateView(this.logger, config.Name, config.Purpose, config.Width, config.Height, targetInfo);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting view with id '{ViewId}' to be destroyed.")]
    private static partial void LogDestroyView(ILogger logger, ulong viewId);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogDestroyView(RuntimeViewId viewId)
        => LogDestroyView(this.logger, viewId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "View '{ViewId}' was released with the engine (state {State}).")]
    private static partial void LogViewReleasedWithEngine(ILogger logger, ulong viewId, EngineServiceState state);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogViewReleasedWithEngine(RuntimeViewId viewId, EngineServiceState state)
        => LogViewReleasedWithEngine(this.logger, viewId.Value, state);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting view with id '{ViewId}' to be hidden.")]
    private static partial void LogHideView(ILogger logger, ulong viewId);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogHideView(RuntimeViewId viewId)
        => LogHideView(this.logger, viewId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting view with id '{ViewId}' to be shown.")]
    private static partial void LogShowView(ILogger logger, ulong viewId);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogShowView(RuntimeViewId viewId)
        => LogShowView(this.logger, viewId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting view with id '{ViewId}' to set camera preset to '{Preset}'.")]
    private static partial void LogSetViewCameraPreset(ILogger logger, ulong viewId, CameraViewPreset preset);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSetViewCameraPreset(RuntimeViewId viewId, CameraViewPreset preset)
        => LogSetViewCameraPreset(this.logger, viewId.Value, preset);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting view with id '{ViewId}' to render through scene camera node '{CameraNodeId}' (none: editor camera).")]
    private static partial void LogSetViewSceneCamera(ILogger logger, ulong viewId, NodeId? cameraNodeId);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSetViewSceneCamera(RuntimeViewId viewId, NodeId? cameraNodeId)
        => LogSetViewSceneCamera(this.logger, viewId.Value, cameraNodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting view with id '{ViewId}' to set scene camera piloting to '{Pilot}'.")]
    private static partial void LogSetViewScenePilot(ILogger logger, ulong viewId, bool pilot);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSetViewScenePilot(RuntimeViewId viewId, bool pilot)
        => LogSetViewScenePilot(this.logger, viewId.Value, pilot);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting view with id '{ViewId}' to set camera control mode to '{Mode}'.")]
    private static partial void LogSetViewCameraControlMode(ILogger logger, ulong viewId, CameraControlMode mode);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSetViewCameraControlMode(RuntimeViewId viewId, CameraControlMode mode)
        => LogSetViewCameraControlMode(this.logger, viewId.Value, mode);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting view with id '{ViewId}' to set camera movement speed to '{SpeedUnitsPerSecond}'.")]
    private static partial void LogSetViewCameraMovementSpeed(ILogger logger, ulong viewId, float speedUnitsPerSecond);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSetViewCameraMovementSpeed(RuntimeViewId viewId, float speedUnitsPerSecond)
        => LogSetViewCameraMovementSpeed(this.logger, viewId.Value, speedUnitsPerSecond);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting view with id '{ViewId}' to render with {Options}.")]
    private static partial void LogSetViewRenderOptions(ILogger logger, ulong viewId, ViewportRenderOptions options);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSetViewRenderOptions(RuntimeViewId viewId, ViewportRenderOptions options)
        => LogSetViewRenderOptions(this.logger, viewId.Value, options);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting view with id '{ViewId}' to set camera settings to fov '{FieldOfViewDegrees}', near '{NearPlane}', far '{FarPlane}'.")]
    private static partial void LogSetViewCameraSettings(ILogger logger, ulong viewId, float fieldOfViewDegrees, float nearPlane, float farPlane);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSetViewCameraSettings(RuntimeViewId viewId, float fieldOfViewDegrees, float nearPlane, float farPlane)
        => LogSetViewCameraSettings(this.logger, viewId.Value, fieldOfViewDegrees, nearPlane, farPlane);
}
