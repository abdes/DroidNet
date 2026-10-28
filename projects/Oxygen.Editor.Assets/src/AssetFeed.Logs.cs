// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Assets;

/// <summary>Logging helpers for <see cref="AssetFeed"/>.</summary>
public sealed partial class AssetFeed
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Unable to refresh asset status for the Content Explorer.")]
    private static partial void LogRefreshFailed(ILogger logger, Exception exception);

    private void LogRefreshFailed(Exception exception) => LogRefreshFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Listed {Rows} assets from {Records} catalog records in {Total} ms: catalog refresh {Refresh} ms, catalog read {Read} ms, rows {Reduce} ms, cook status {Status} ms.")]
    private static partial void LogRefreshTimes(ILogger logger, int rows, int records, long total, long refresh, long read, long reduce, long status);

    private void LogRefreshTimes(RefreshTimes times, int records, int rows)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var total = (long)Stopwatch.GetElapsedTime(times.Started, times.Finished).TotalMilliseconds;
        var refresh = (long)Stopwatch.GetElapsedTime(times.Started, times.CatalogRefreshed).TotalMilliseconds;
        var read = (long)Stopwatch.GetElapsedTime(times.CatalogRefreshed, times.SnapshotRead).TotalMilliseconds;
        var reduce = (long)Stopwatch.GetElapsedTime(times.SnapshotRead, times.RowsBuilt).TotalMilliseconds;
        var status = (long)Stopwatch.GetElapsedTime(times.RowsBuilt, times.Finished).TotalMilliseconds;
        LogRefreshTimes(this.logger, rows, records, total, refresh, read, reduce, status);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The project or its catalog changed while its assets were listed; listing them again.")]
    private static partial void LogRefreshSuperseded(ILogger logger);

    private void LogRefreshSuperseded() => LogRefreshSuperseded(this.logger);

    // The timestamps of one asset listing, at the end of each of its steps.
    private readonly record struct RefreshTimes(long Started, long CatalogRefreshed, long SnapshotRead, long RowsBuilt, long Finished);
}
