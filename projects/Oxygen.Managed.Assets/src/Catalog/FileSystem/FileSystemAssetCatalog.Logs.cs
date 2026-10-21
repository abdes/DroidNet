// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Managed.Assets.Catalog.FileSystem;

/// <inheritdoc cref="FileSystemAssetCatalog"/>
/// <remarks>
/// Levels: Information for the changes a batch of file events reported and for each rescan; Warning when applying a
/// batch failed and the catalog rescanned instead; Error when a batch failed outright, so changes may be missing.
/// </remarks>
public sealed partial class FileSystemAssetCatalog
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{Events} file events under mount '{MountPoint}' reported {Added} added, {Removed} removed, {Updated} updated and {Relocated} renamed or moved files.")]
    private static partial void LogChangesReported(ILogger logger, int events, string mountPoint, int added, int removed, int updated, int relocated);

    private void LogChangesReported(int events, string mountPoint, int added, int removed, int updated, int relocated)
        => LogChangesReported(this.logger, events, mountPoint, added, removed, updated, relocated);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "A folder under mount '{MountPoint}' was created, deleted, renamed or moved; rescanning the mount to find the files it held.")]
    private static partial void LogFolderRescan(ILogger logger, string mountPoint);

    private void LogFolderRescan(string mountPoint)
        => LogFolderRescan(this.logger, mountPoint);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The file watcher of mount '{MountPoint}' lost events; rescanning the mount and reporting every known file as updated.")]
    private static partial void LogEventsLost(ILogger logger, string mountPoint);

    private void LogEventsLost(string mountPoint)
        => LogEventsLost(this.logger, mountPoint);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Applying file events under mount '{MountPoint}' failed; rescanning the mount and reporting every known file as updated.")]
    private static partial void LogApplyFailed(ILogger logger, Exception exception, string mountPoint);

    private void LogApplyFailed(Exception exception, string mountPoint)
        => LogApplyFailed(this.logger, exception, mountPoint);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "A batch of {Count} file events under mount '{MountPoint}' failed; changes it carried may not have been reported.")]
    private static partial void LogBatchFailed(ILogger logger, Exception exception, int count, string mountPoint);

    private void LogBatchFailed(Exception exception, int count, string mountPoint)
        => LogBatchFailed(this.logger, exception, count, mountPoint);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "'{RelativePath}' under mount '{MountPoint}' has a name the virtual path rules refuse; it is not listed.")]
    private static partial void LogNameRefused(ILogger logger, string relativePath, string mountPoint);

    private void LogNameRefused(string relativePath, string mountPoint)
        => LogNameRefused(this.logger, relativePath, mountPoint);
}
