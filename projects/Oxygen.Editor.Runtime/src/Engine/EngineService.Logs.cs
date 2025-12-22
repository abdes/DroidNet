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
        Level = LogLevel.Information,
        Message = "Engine runner is already initialized (State: {State}).")]
    private static partial void LogAlreadyInitialized(ILogger logger, EngineServiceState state);

    private void LogAlreadyInitialized()
        => LogAlreadyInitialized(this.logger, this.State);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Engine runner initialized on dispatcher thread {ThreadId}.")]
    private static partial void LogRunnerInitialized(ILogger logger, int threadId);

    private void LogRunnerInitialized()
        => LogRunnerInitialized(this.logger, Environment.CurrentManagedThreadId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to initialize engine service.")]
    private static partial void LogInitializationFailed(ILogger logger, Exception exception);

    private void LogInitializationFailed(Exception exception)
        => LogInitializationFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to resize composition surface to {Width}x{Height}.")]
    private static partial void LogResizeFailed(ILogger logger, uint width, uint height, Exception? exception);

    private void LogResizeFailed(uint width, uint height, Exception? exception = null)
        => LogResizeFailed(this.logger, width, height, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to compute initial surface pixels: {ExceptionMessage}")]
    private static partial void LogComputeInitialPixelsFailed(ILogger logger, string exceptionMessage);

    private void LogComputeInitialPixelsFailed(Exception exception)
        => LogComputeInitialPixelsFailed(this.logger, exception.Message);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Invalid panel measurements for initial surface pixels: ActualWidth={ActualWidth} ActualHeight={ActualHeight}")]
    private static partial void LogComputeInitialPixelsInvalidMeasurements(ILogger logger, double actualWidth, double actualHeight);

    private void LogComputeInitialPixelsInvalidMeasurements(double actualWidth, double actualHeight)
        => LogComputeInitialPixelsInvalidMeasurements(this.logger, actualWidth, actualHeight);

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
        Message = "Stopping engine loop.")]
    private static partial void LogStoppingEngineLoop(ILogger logger);

    private void LogStoppingEngineLoop()
        => LogStoppingEngineLoop(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Engine loop stop threw an exception.")]
    private static partial void LogEngineStopFault(ILogger logger, Exception exception);

    private void LogEngineStopFault(Exception exception)
        => LogEngineStopFault(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Headless engine context created.")]
    private static partial void LogContextReady(ILogger logger);

    private void LogContextReady()
        => LogContextReady(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Lease {LeaseKey} reused for document {DocumentId}. Active={TotalActive} DocumentActive={DocumentSurfaceCount}.")]
    private static partial void LogLeaseReused(ILogger logger, string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount);

    private void LogLeaseReused(string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount)
        => LogLeaseReused(this.logger, leaseKey, documentId, totalActive, documentSurfaceCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Lease {LeaseKey} created for document {DocumentId}. Active={TotalActive} DocumentActive={DocumentSurfaceCount}.")]
    private static partial void LogLeaseCreated(ILogger logger, string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount);

    private void LogLeaseCreated(string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount)
        => LogLeaseCreated(this.logger, leaseKey, documentId, totalActive, documentSurfaceCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Lease {LeaseKey} attached to panel {PanelHash} while state {State}.")]
    private static partial void LogLeaseAttached(ILogger logger, string leaseKey, int panelHash, EngineServiceState state);

    private void LogLeaseAttached(string leaseKey, int panelHash, EngineServiceState state)
        => LogLeaseAttached(this.logger, leaseKey, panelHash, state);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Lease {LeaseKey} released for document {DocumentId}. Active={TotalActive} DocumentActive={DocumentSurfaceCount} IdleCandidate={IdleCandidate}.")]
    private static partial void LogLeaseReleased(ILogger logger, string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount, bool idleCandidate);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Failed to release lease {LeaseKey} for document {DocumentId}. Active={TotalActive} DocumentActive={DocumentSurfaceCount}.")]
    private static partial void LogLeaseReleaseFailed(ILogger logger, string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to release lease {LeaseKey} for document {DocumentId}, ({DocumentSurfaceCount} surfaces, {TotalActive} active).")]
    private static partial void LogLeaseReleaseFailed(ILogger logger, Exception exception, string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount);

    private void LogLeaseReleaseFailed(Exception exception, string leaseKey, Guid documentId)
        => LogLeaseReleaseFailed(this.logger, exception, leaseKey, documentId, this.activeLeases.Count, this.documentSurfaceCounts.Count);

    private void LogLeaseReleased(string leaseKey, Guid documentId, bool result)
    {
        if (result)
        {
            LogLeaseReleased(this.logger, leaseKey, documentId, this.activeLeases.Count, this.documentSurfaceCounts.Count, this.activeLeases.IsEmpty);
        }
        else
        {
            LogLeaseReleaseFailed(this.logger, leaseKey, documentId, this.activeLeases.Count, this.documentSurfaceCounts.Count);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Engine shutdown requested in state {State}.")]
    private static partial void LogShutdownRequested(ILogger logger, EngineServiceState state);

    private void LogShutdownRequested()
        => LogShutdownRequested(this.logger, this.state);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "StopEngineAsync completed (keepContextAlive={KeepContextAlive}) -> state {State}.")]
    private static partial void LogStopEngineCompleted(ILogger logger, bool keepContextAlive, EngineServiceState state);

    private void LogStopEngineCompleted(bool keepContextAlive, EngineServiceState state)
        => LogStopEngineCompleted(this.logger, keepContextAlive, state);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "StopEngineAsync skipped: {Reason}.")]
    private static partial void LogStopEngineSkipped(ILogger logger, string reason);

    private void LogStopEngineSkipped(string reason)
        => LogStopEngineSkipped(this.logger, reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Engine context destroyed.")]
    private static partial void LogContextDestroyed(ILogger logger);

    private void LogContextDestroyed()
        => LogContextDestroyed(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Engine context reused.")]
    private static partial void LogContextReused(ILogger logger);

    private void LogContextReused()
        => LogContextReused(this.logger);

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
        Level = LogLevel.Error,
        Message = "Failed to set engine target FPS to {Fps}.")]
    private static partial void LogSetTargetFpsFailed(ILogger logger, uint fps, Exception exception);

    private void LogSetTargetFpsFailed(uint fps, Exception exception)
        => LogSetTargetFpsFailed(this.logger, fps, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to retrieve engine target FPS.")]
    private static partial void LogGetTargetFpsFailed(ILogger logger, Exception exception);

    private void LogGetTargetFpsFailed(Exception exception)
        => LogGetTargetFpsFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Engine logging verbosity retrieved: {Verbosity}.")]
    private static partial void LogLoggingVerbosityRetrieved(ILogger logger, int verbosity);

    private void LogLoggingVerbosityRetrieved(int verbosity)
        => LogLoggingVerbosityRetrieved(this.logger, verbosity);

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
}
