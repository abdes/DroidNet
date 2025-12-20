// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.ContentBrowser;

/// <summary>
///     Logging methods for <see cref="ContentBrowserViewModel"/>.
/// </summary>
public partial class ContentBrowserViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Pushed URL: `{Url}`")]
    private static partial void LogHistoryPush(ILogger logger, string url);

    private void LogHistoryPush(string url)
        => LogHistoryPush(this.logger, url);

    [LoggerMessage(
        Level = LogLevel.Information,
        Message = "Navigated back from index {FromIndex} to {ToIndex} (URL: {Url})")]
    private static partial void LogNavigationBack(ILogger logger, int fromIndex, int toIndex, string url);

    private void LogNavigationBack(int fromIndex, int toIndex, string url)
        => LogNavigationBack(this.logger, fromIndex, toIndex, url);

    [LoggerMessage(
        Level = LogLevel.Information,
        Message = "Navigated forward from index {FromIndex} to {ToIndex} (URL: {Url})")]
    private static partial void LogNavigationForward(ILogger logger, int fromIndex, int toIndex, string url);

    private void LogNavigationForward(int fromIndex, int toIndex, string url)
        => LogNavigationForward(this.logger, fromIndex, toIndex, url);

    [LoggerMessage(
        Level = LogLevel.Information,
        Message = "Navigated Up to: {Url} (from '{Current}')")]
    private static partial void LogNavigationUp(ILogger logger, string url, string current);

    private void LogNavigationUp(string url, string current)
        => LogNavigationUp(this.logger, url, current);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "ContentBrowserState changed, updating history only: {Url}")]
    private static partial void LogContentBrowserStateChanged(ILogger logger, string url);

    private void LogContentBrowserStateChanged(string url)
        => LogContentBrowserStateChanged(this.logger, url);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "NavigationEnd event: URL={Url}, IsFromHistory={IsFromHistory}")]
    private static partial void LogNavigationEnd(ILogger logger, string url, bool isFromHistory);

    private void LogNavigationEnd(string url, bool isFromHistory)
        => LogNavigationEnd(this.logger, url, isFromHistory);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Duplicate of current entry, skipping add to history")]
    private static partial void LogHistoryDuplicateSkipped(ILogger logger);

    private void LogHistoryDuplicateSkipped()
        => LogHistoryDuplicateSkipped(this.logger);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Removing {Count} forward history items from index {Index}")]
    private static partial void LogHistoryForwardCleared(ILogger logger, int count, int index);

    private void LogHistoryForwardCleared(int count, int index)
        => LogHistoryForwardCleared(this.logger, count, index);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "History state: Count={Count}, CurrentIndex={CurrentIndex}")]
    private static partial void LogHistoryState(ILogger logger, int count, int currentIndex);

    private void LogHistoryState(int count, int currentIndex)
        => LogHistoryState(this.logger, count, currentIndex);

    [LoggerMessage(
        Level = LogLevel.Trace,
        Message = "Full history: {History}")]
    private static partial void LogHistoryFull(ILogger logger, string history);

    private void LogHistoryFull(List<string> history, int currentIndex)
    {
        if (this.logger.IsEnabled(LogLevel.Trace))
        {
            var histStr = string.Join(", ", history.Select((u, i) => i == currentIndex ? $"*{u}*" : u));
            LogHistoryFull(this.logger, histStr);
        }
    }

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "CanGoUp updated: {CanGoUp}, current='{Current}', parent='{Parent}'")]
    private static partial void LogCanGoUpUpdated(ILogger logger, bool canGoUp, string? current, string? parent);

    private void LogCanGoUpUpdated(bool canGoUp, string? current, string? parent)
        => LogCanGoUpUpdated(this.logger, canGoUp, current, parent);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Refresh requested for selected folders")]
    private static partial void LogRefreshRequested(ILogger logger);

    private void LogRefreshRequested()
        => LogRefreshRequested(this.logger);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Failed to retrieve project name for breadcrumbs")]
    private static partial void LogProjectNameRetrievalFailed(ILogger logger, Exception ex);

    private void LogProjectNameRetrievalFailed(Exception ex)
        => LogProjectNameRetrievalFailed(this.logger, ex);
}
