// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Source-generated diagnostics for workspace activation and cooked-root mounting.</summary>
public partial class WorkspaceViewModel
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "Cannot refresh cooked roots: No active project context.")]
    private partial void LogRefreshWithoutProject();

    [LoggerMessage(Level = LogLevel.Information, Message = "Refreshing cooked roots. Project location: {ProjectLocation}, cooked root: {CookedBaseRoot}")]
    private partial void LogRefreshingCookedRoots(string? projectLocation, string? cookedBaseRoot);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Cooked root directory does not exist: {CookedBaseRoot}. Assets will not be available in the engine.")]
    private partial void LogCookedRootMissing(string? cookedBaseRoot);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to mount cooked root {CookedBaseRoot}.")]
    private partial void LogBaseRootMountFailed(Exception exception, string? cookedBaseRoot);

    [LoggerMessage(Level = LogLevel.Information, Message = "Mounted {Count} cooked roots: {Mounted}")]
    private partial void LogMountedRootsCore(int count, string? mounted);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to enumerate cooked mount point directories under {CookedBaseRoot}.")]
    private partial void LogMountEnumerationFailed(Exception exception, string? cookedBaseRoot);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to mount cooked root {CookedMountRoot}.")]
    private partial void LogMountPointFailed(Exception exception, string? cookedMountRoot);

    [LoggerMessage(Level = LogLevel.Warning, Message = "No cooked index files found under {CookedBaseRoot} (expected .cooked/<MountPoint>/{IndexFileName}). Assets will not be available in the engine.")]
    private partial void LogCookedIndicesMissing(string? cookedBaseRoot, string? indexFileName);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Cannot mount validated cooked roots: No active project context.")]
    private partial void LogValidatedMountWithoutProject();

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to mount validated cooked root {CookedRoot}.")]
    private partial void LogValidatedMountFailed(Exception exception, string? cookedRoot);

    [LoggerMessage(Level = LogLevel.Information, Message = "Mounted {Count} validated cooked roots: {CookedRoots}")]
    private partial void LogValidatedRootsCore(int count, string? cookedRoots);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Cannot open initial scene {SceneName}: document manager is not available.")]
    private partial void LogInitialSceneManagerUnavailable(string? sceneName);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Initial scene {SceneName} was selected for project {ProjectName}, but the document did not open.")]
    private partial void LogInitialSceneNotOpened(string? sceneName, string? projectName);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to restore the last opened scene for project {ProjectName}.")]
    private partial void LogSceneRestorationFailed(Exception exception, string? projectName);

    [LoggerMessage(Level = LogLevel.Information, Message = "Starting embedded engine for workspace activation.")]
    private partial void LogEngineStarting();

    [LoggerMessage(Level = LogLevel.Information, Message = "Starting initialized embedded engine for workspace activation.")]
    private partial void LogInitializedEngineStarting();

    [LoggerMessage(Level = LogLevel.Warning, Message = "Cannot refresh cooked roots while engine is in state {EngineState}.")]
    private partial void LogEngineStateNotReady(EngineServiceState engineState);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to start embedded engine for workspace activation.")]
    private partial void LogEngineStartFailed(Exception exception);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Skipping incompatible cooked index {IndexPath}. Re-cook the project to regenerate this mount point.")]
    private partial void LogCookedIndexRejected(Exception exception, string? indexPath);

    private void LogMountedRoots(List<string> roots, bool validated = false)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var joined = string.Join("; ", roots);
        if (validated)
        {
            this.LogValidatedRootsCore(roots.Count, joined);
        }
        else
        {
            this.LogMountedRootsCore(roots.Count, joined);
        }
    }
}
