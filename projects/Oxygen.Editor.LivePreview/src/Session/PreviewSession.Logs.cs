// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.LivePreview.Session;

/// <summary>
///     Logging helpers for <see cref="PreviewSession"/>.
/// </summary>
public sealed partial class PreviewSession
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Starting embedded engine for the project preview.")]
    private static partial void LogEngineStarting(ILogger logger);

    private void LogEngineStarting()
        => LogEngineStarting(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Starting initialized embedded engine for the project preview.")]
    private static partial void LogInitializedEngineStarting(ILogger logger);

    private void LogInitializedEngineStarting()
        => LogInitializedEngineStarting(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Cannot start the project preview while the engine is in state {EngineState}.")]
    private static partial void LogEngineStateNotReady(ILogger logger, EngineServiceState engineState);

    private void LogEngineStateNotReady(EngineServiceState engineState)
        => LogEngineStateNotReady(this.logger, engineState);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to start embedded engine for the project preview.")]
    private static partial void LogEngineStartFailed(ILogger logger, Exception exception);

    private void LogEngineStartFailed(Exception exception)
        => LogEngineStartFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cooked-content refresh failed while {Stage} for project {ProjectRoot}. Preview state: {PreviewState}.")]
    private static partial void LogCookedRootRefreshFailed(ILogger logger, Exception exception, string stage, string projectRoot, string previewState);

    private void LogCookedRootRefreshFailed(Exception exception, string stage, string projectRoot, string previewState)
        => LogCookedRootRefreshFailed(this.logger, exception, stage, projectRoot, previewState);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "A cooked library changed before project {ProjectRoot} could mount it; republishing from the current libraries.")]
    private static partial void LogCookedLibraryChanged(ILogger logger, Exception exception, string projectRoot);

    private void LogCookedLibraryChanged(Exception exception, string projectRoot)
        => LogCookedLibraryChanged(this.logger, exception, projectRoot);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to suspend the preview after cooked-content refresh failed for project {ProjectRoot}.")]
    private static partial void LogCookedContentSuspensionFailed(ILogger logger, Exception exception, string projectRoot);

    private void LogCookedContentSuspensionFailed(Exception exception, string projectRoot)
        => LogCookedContentSuspensionFailed(this.logger, exception, projectRoot);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Mounted {Count} cooked roots.")]
    private static partial void LogMountedRoots(ILogger logger, int count);

    private void LogMountedRoots(int count)
        => LogMountedRoots(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to release the project's cooked roots.")]
    private static partial void LogCookedRootReleaseFailed(ILogger logger, Exception exception);

    private void LogCookedRootReleaseFailed(Exception exception)
        => LogCookedRootReleaseFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Changed the content mounts of project {ProjectRoot}: {LocalFolderCount} local folders, {OrderCount} cooked sources in order.")]
    private static partial void LogMountsChanged(ILogger logger, string projectRoot, int localFolderCount, int orderCount);

    private void LogMountsChanged(string projectRoot, int localFolderCount, int orderCount)
        => LogMountsChanged(this.logger, projectRoot, localFolderCount, orderCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "A content mount change of project {ProjectRoot} was saved, but another project became active before it could be shown.")]
    private static partial void LogMountsChangeSuperseded(ILogger logger, string projectRoot);

    private void LogMountsChangeSuperseded(string projectRoot)
        => LogMountsChangeSuperseded(this.logger, projectRoot);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Engine run {RunId} that previews project {ProjectRoot} faulted ({Code}, engine state {EngineState}); the preview is unavailable until it is retried.")]
    private static partial void LogEngineRunFaulted(ILogger logger, Guid runId, string projectRoot, string code, EngineServiceState engineState);

    private void LogEngineRunFaulted(Guid runId, EngineServiceState engineState, string code)
    {
        if (!this.logger.IsEnabled(LogLevel.Error))
        {
            return;
        }

        var projectRoot = session.Context.ProjectRoot.FullPath;
        LogEngineRunFaulted(this.logger, runId, projectRoot, code, engineState);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Engine run {RunId} that previews project {ProjectRoot} was shut down (engine state {EngineState}); the preview is unavailable until it is retried.")]
    private static partial void LogEngineRunStopped(ILogger logger, Guid runId, string projectRoot, EngineServiceState engineState);

    private void LogEngineRunStopped(Guid runId, EngineServiceState engineState)
    {
        if (!this.logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        var projectRoot = session.Context.ProjectRoot.FullPath;
        LogEngineRunStopped(this.logger, runId, projectRoot, engineState);
    }
}
