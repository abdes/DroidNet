// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.WorldEditor.Engine;

/// <summary>
/// Logger message definitions for <see cref="EngineService"/>.
/// </summary>
public sealed partial class EngineService
{
    [LoggerMessage(EventId = 1, Level = LogLevel.Information, Message = "Engine runner initialized on dispatcher thread {ThreadId}.")]
    private static partial void LogRunnerInitialized(ILogger logger, int threadId);

    private void LogRunnerInitialized() => LogRunnerInitialized(this.logger, Environment.CurrentManagedThreadId);

    [LoggerMessage(EventId = 2, Level = LogLevel.Error, Message = "Failed to initialize engine service.")]
    private static partial void LogInitializationFailed(ILogger logger, Exception exception);

    private void LogInitializationFailed(Exception exception) => LogInitializationFailed(this.logger, exception);

    [LoggerMessage(EventId = 3, Level = LogLevel.Warning, Message = "Failed to resize composition surface to {Width}x{Height}.")]
    private static partial void LogResizeFailed(ILogger logger, Exception exception, uint width, uint height);

    private void LogResizeFailed(Exception exception, uint width, uint height)
        => LogResizeFailed(this.logger, exception, width, height);

    [LoggerMessage(EventId = 4, Level = LogLevel.Information, Message = "Starting engine loop for viewport panel {PanelHash}.")]
    private static partial void LogStartingEngineLoop(ILogger logger, int panelHash);

    private void LogStartingEngineLoop(int panelHash) => LogStartingEngineLoop(this.logger, panelHash);

    [LoggerMessage(EventId = 5, Level = LogLevel.Information, Message = "Stopping engine loop.")]
    private static partial void LogStoppingEngineLoop(ILogger logger);

    private void LogStoppingEngineLoop() => LogStoppingEngineLoop(this.logger);

    [LoggerMessage(EventId = 6, Level = LogLevel.Warning, Message = "Engine loop stop threw an exception.")]
    private static partial void LogEngineStopFault(ILogger logger, Exception exception);

    private void LogEngineStopFault(Exception exception) => LogEngineStopFault(this.logger, exception);

    [LoggerMessage(EventId = 7, Level = LogLevel.Information, Message = "Headless engine context created.")]
    private static partial void LogDormantContextReady(ILogger logger);

    private void LogDormantContextReady() => LogDormantContextReady(this.logger);

    [LoggerMessage(EventId = 8, Level = LogLevel.Debug, Message = "Lease {LeaseKey} reused for document {DocumentId}. Active={TotalActive} DocumentActive={DocumentSurfaceCount}.")]
    private static partial void LogLeaseReused(ILogger logger, string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount);

    private void LogLeaseReused(string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount)
        => LogLeaseReused(this.logger, leaseKey, documentId, totalActive, documentSurfaceCount);

    [LoggerMessage(EventId = 9, Level = LogLevel.Information, Message = "Lease {LeaseKey} created for document {DocumentId}. Active={TotalActive} DocumentActive={DocumentSurfaceCount}.")]
    private static partial void LogLeaseCreated(ILogger logger, string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount);

    private void LogLeaseCreated(string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount)
        => LogLeaseCreated(this.logger, leaseKey, documentId, totalActive, documentSurfaceCount);

    [LoggerMessage(EventId = 10, Level = LogLevel.Information, Message = "Lease {LeaseKey} attached to panel {PanelHash} while state {State}.")]
    private static partial void LogLeaseAttached(ILogger logger, string leaseKey, int panelHash, EngineServiceState state);

    private void LogLeaseAttached(string leaseKey, int panelHash, EngineServiceState state)
        => LogLeaseAttached(this.logger, leaseKey, panelHash, state);

    [LoggerMessage(EventId = 11, Level = LogLevel.Information, Message = "Lease {LeaseKey} released for document {DocumentId}. Active={TotalActive} DocumentActive={DocumentSurfaceCount} WasPrimary={WasPrimaryLease} StopEngine={StopEngine}.")]
    private static partial void LogLeaseReleased(ILogger logger, string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount, bool wasPrimaryLease, bool stopEngine);

    private void LogLeaseReleased(string leaseKey, Guid documentId, int totalActive, int documentSurfaceCount, bool wasPrimaryLease, bool stopEngine)
        => LogLeaseReleased(this.logger, leaseKey, documentId, totalActive, documentSurfaceCount, wasPrimaryLease, stopEngine);

    [LoggerMessage(EventId = 12, Level = LogLevel.Debug, Message = "Lease {LeaseKey} designated as primary viewport.")]
    private static partial void LogPrimaryLeaseSelected(ILogger logger, string leaseKey);

    private void LogPrimaryLeaseSelected(string leaseKey) => LogPrimaryLeaseSelected(this.logger, leaseKey);

    [LoggerMessage(EventId = 13, Level = LogLevel.Information, Message = "StopEngineAsync requested (transitionToDormant={TransitionToDormant}) hasActiveContext={HasActiveContext} while state {State}.")]
    private static partial void LogStopEngineRequested(ILogger logger, bool transitionToDormant, bool hasActiveContext, EngineServiceState state);

    private void LogStopEngineRequested(bool transitionToDormant, bool hasActiveContext, EngineServiceState state)
        => LogStopEngineRequested(this.logger, transitionToDormant, hasActiveContext, state);

    [LoggerMessage(EventId = 14, Level = LogLevel.Information, Message = "StopEngineAsync completed (transitionToDormant={TransitionToDormant}) -> state {State}.")]
    private static partial void LogStopEngineCompleted(ILogger logger, bool transitionToDormant, EngineServiceState state);

    private void LogStopEngineCompleted(bool transitionToDormant, EngineServiceState state)
        => LogStopEngineCompleted(this.logger, transitionToDormant, state);

    [LoggerMessage(EventId = 15, Level = LogLevel.Debug, Message = "StopEngineAsync skipped: {Reason}.")]
    private static partial void LogStopEngineSkipped(ILogger logger, string reason);

    private void LogStopEngineSkipped(string reason) => LogStopEngineSkipped(this.logger, reason);

    [LoggerMessage(EventId = 16, Level = LogLevel.Debug, Message = "Dormant context destroyed.")]
    private static partial void LogDormantContextDestroyed(ILogger logger);

    private void LogDormantContextDestroyed() => LogDormantContextDestroyed(this.logger);

    [LoggerMessage(EventId = 17, Level = LogLevel.Debug, Message = "Dormant context reused.")]
    private static partial void LogDormantContextReused(ILogger logger);

    private void LogDormantContextReused() => LogDormantContextReused(this.logger);
}
