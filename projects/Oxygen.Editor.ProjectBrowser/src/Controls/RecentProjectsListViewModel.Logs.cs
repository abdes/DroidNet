// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
///     Logging methods for the <see cref="RecentProjectsListViewModel"/> class.
/// </summary>
internal partial class RecentProjectsListViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Initialized: ItemsView.Source.Hash={SourceHash}")]
    private static partial void LogInitialized(ILogger logger, int? sourceHash);

    [Conditional("DEBUG")]
    private void LogInitialized(int? sourceHash)
        => LogInitialized(this.logger, sourceHash);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to save column widths: {ErrorMessage}")]
    private static partial void LogFailedToSave(ILogger logger, string errorMessage);

    private void LogFailedToSave(string errorMessage)
        => LogFailedToSave(this.logger, errorMessage);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to load column widths: {ErrorMessage}")]
    private static partial void LogFailedToLoad(ILogger logger, string errorMessage);

    private void LogFailedToLoad(string errorMessage)
        => LogFailedToLoad(this.logger, errorMessage);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Activating project: {ProjectName}")]
    private static partial void LogActivatingProject(ILogger logger, string projectName);

    [Conditional("DEBUG")]
    private void LogActivatingProject(string projectName)
        => LogActivatingProject(this.logger, projectName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Toggling sort by name: comparer={Comparer}, direction={Direction}, itemCount={ItemCount}")]
    private static partial void LogTogglingSortByName(ILogger logger, string comparer, string direction, int itemCount);

    [Conditional("DEBUG")]
    private void LogTogglingSortByName(string comparer, string direction, int itemCount)
        => LogTogglingSortByName(this.logger, comparer, direction, itemCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Toggling sort by last used: comparer={Comparer}, direction={Direction}, itemCount={ItemCount}")]
    private static partial void LogTogglingSortByLastUsedOn(ILogger logger, string comparer, string direction, int itemCount);

    [Conditional("DEBUG")]
    private void LogTogglingSortByLastUsedOn(string comparer, string direction, int itemCount)
        => LogTogglingSortByLastUsedOn(this.logger, comparer, direction, itemCount);
}
