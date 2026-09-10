// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// Logger message definitions for <see cref="EngineService"/>.
/// </summary>
public sealed partial class EngineService
{
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
        Level = LogLevel.Debug,
        Message = "Engine target FPS set to {Fps}.")]
    private static partial void LogTargetFpsSet(ILogger logger, uint fps);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Engine target FPS set to {Requested} (clamped from {Clamped}).")]
    private static partial void LogTargetFpsClamped(ILogger logger, uint requested, uint clamped);

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
        Message = "Requesting view ('{Name}'/'{Purpose}') from the engine: Extent={Width}x{Height}, {TargetInfo}")]
    private static partial void LogCreateView(ILogger logger, string name, string purpose, uint width, uint height, string targetInfo);

    private void LogCreateView(ViewConfigManaged config)
    {
        var hasTarget = config.CompositingTarget != null ? $"composing to: {config.CompositingTarget}" : "without target";
        LogCreateView(this.logger, config.Name, config.Purpose, config.Width, config.Height, hasTarget);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Requesting view with id '{ViewId}' to be destroyed.")]
    private static partial void LogDestroyView(ILogger logger, ulong viewId);

    private void LogDestroyView(ViewIdManaged viewId)
        => LogDestroyView(this.logger, viewId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Requesting view with id '{ViewId}' to be hidden.")]
    private static partial void LogHideView(ILogger logger, ulong viewId);

    private void LogHideView(ViewIdManaged viewId)
        => LogHideView(this.logger, viewId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Requesting view with id '{ViewId}' to be shown.")]
    private static partial void LogShowView(ILogger logger, ulong viewId);

    private void LogShowView(ViewIdManaged viewId)
        => LogShowView(this.logger, viewId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Requesting view with id '{ViewId}' to set camera preset to '{Preset}'.")]
    private static partial void LogSetViewCameraPreset(ILogger logger, ulong viewId, CameraViewPresetManaged preset);

    private void LogSetViewCameraPreset(ViewIdManaged viewId, CameraViewPresetManaged preset)
        => LogSetViewCameraPreset(this.logger, viewId.Value, preset);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Requesting view with id '{ViewId}' to set camera control mode to '{Mode}'.")]
    private static partial void LogSetViewCameraControlMode(ILogger logger, ulong viewId, CameraControlModeManaged mode);

    private void LogSetViewCameraControlMode(ViewIdManaged viewId, CameraControlModeManaged mode)
        => LogSetViewCameraControlMode(this.logger, viewId.Value, mode);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Requesting view with id '{ViewId}' to set camera movement speed to '{SpeedUnitsPerSecond}'.")]
    private static partial void LogSetViewCameraMovementSpeed(ILogger logger, ulong viewId, float speedUnitsPerSecond);

    private void LogSetViewCameraMovementSpeed(ViewIdManaged viewId, float speedUnitsPerSecond)
        => LogSetViewCameraMovementSpeed(this.logger, viewId.Value, speedUnitsPerSecond);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Requesting view with id '{ViewId}' to set camera settings to fov '{FieldOfViewDegrees}', near '{NearPlane}', far '{FarPlane}'.")]
    private static partial void LogSetViewCameraSettings(ILogger logger, ulong viewId, float fieldOfViewDegrees, float nearPlane, float farPlane);

    private void LogSetViewCameraSettings(ViewIdManaged viewId, float fieldOfViewDegrees, float nearPlane, float farPlane)
        => LogSetViewCameraSettings(this.logger, viewId.Value, fieldOfViewDegrees, nearPlane, farPlane);
}
