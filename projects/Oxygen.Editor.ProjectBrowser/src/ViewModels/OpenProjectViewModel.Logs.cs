// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
///     Logging methods for the <see cref="OpenProjectViewModel"/> class.
/// </summary>
public partial class OpenProjectViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to preload known locations during ViewModel activation.")]
    private static partial void LogLoadingKnownLocationsError(ILogger logger, Exception ex);

    private void LogLoadingKnownLocationsError(Exception ex)
        => LogLoadingKnownLocationsError(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Loading known locations.")]
    private static partial void LogLoadingKnownLocations(ILogger logger);

    private void LogLoadingKnownLocations()
        => LogLoadingKnownLocations(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to open project file at '{Location}'.")]
    private static partial void LogFailedToOpenProjectFileNoException(ILogger logger, string location);

    private void LogFailedToOpenProjectFile(string location)
        => LogFailedToOpenProjectFileNoException(this.logger, location);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to save column widths.")]
    private static partial void LogFailedToSaveColumnWidths(ILogger logger, Exception ex);

    private void LogFailedToSaveColumnWidths(Exception ex)
        => LogFailedToSaveColumnWidths(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Saved column widths.")]
    private static partial void LogSavedColumnWidths(ILogger logger);

    private void LogSavedColumnWidths()
        => LogSavedColumnWidths(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Loaded column widths: '{Widths}'.")]
    private static partial void LogLoadedColumnWidths(ILogger logger, string widths);

    private void LogLoadedColumnWidths(string widths)
        => LogLoadedColumnWidths(this.logger, widths);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to load column widths.")]
    private static partial void LogFailedToLoadColumnWidths(ILogger logger, Exception ex);

    private void LogFailedToLoadColumnWidths(Exception ex)
        => LogFailedToLoadColumnWidths(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Toggled file list sort: comparer={Comparer}, direction={Direction}.")]
    private static partial void LogToggledSort(ILogger logger, string comparer, string direction);

    [Conditional("DEBUG")]
    private void LogToggledSort()
    {
        var desc = this.byNameSortDescription ?? this.byDateSortDescription;
        if (desc is null)
        {
            return;
        }

        LogToggledSort(this.logger, desc.Comparer.GetType().Name, desc.Direction.ToString());
    }
}
