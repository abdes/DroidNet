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
    [LoggerMessage(Level = LogLevel.Information, Message = "{Events} file events under mount '{MountPoint}' reported {Added} added, {Removed} removed, {Updated} updated and {Relocated} renamed or moved files.")]
    private partial void LogChangesReported(int events, string mountPoint, int added, int removed, int updated, int relocated);

    [LoggerMessage(Level = LogLevel.Information, Message = "A folder under mount '{MountPoint}' was created, deleted, renamed or moved; rescanning the mount to find the files it held.")]
    private partial void LogFolderRescan(string mountPoint);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The file watcher of mount '{MountPoint}' lost events; rescanning the mount and reporting every known file as updated.")]
    private partial void LogEventsLost(string mountPoint);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Applying file events under mount '{MountPoint}' failed; rescanning the mount and reporting every known file as updated.")]
    private partial void LogApplyFailed(Exception exception, string mountPoint);

    [LoggerMessage(Level = LogLevel.Error, Message = "A batch of {Count} file events under mount '{MountPoint}' failed; changes it carried may not have been reported.")]
    private partial void LogBatchFailed(Exception exception, int count, string mountPoint);
}
