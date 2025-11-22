// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.WorldEditor.Engine;

/// <summary>
/// Logger message definitions for <see cref="EngineService"/>.
/// </summary>
public sealed partial class EngineService
{
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
    private static partial void LogResizeFailed(ILogger logger, Exception exception, uint width, uint height);

    private void LogResizeFailed(Exception exception, uint width, uint height)
        => LogResizeFailed(this.logger, exception, width, height);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Starting engine loop for panel {PanelHash}.")]
    private static partial void LogStartingEngineLoop(ILogger logger, int panelHash);

    private void LogStartingEngineLoop(int panelHash)
        => LogStartingEngineLoop(this.logger, panelHash);

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

    private void LogLeaseReleased(string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount, bool idleCandidate)
        => LogLeaseReleased(this.logger, leaseKey, documentId, totalActive, documentSurfaceCount, idleCandidate);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "StopEngineAsync requested (keepContextAlive={KeepContextAlive}) hasActiveContext={HasActiveContext} while state {State}.")]
    private static partial void LogStopEngineRequested(ILogger logger, bool keepContextAlive, bool hasActiveContext, EngineServiceState state);

    private void LogStopEngineRequested(bool keepContextAlive, bool hasActiveContext, EngineServiceState state)
        => LogStopEngineRequested(this.logger, keepContextAlive, hasActiveContext, state);

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
}
