// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
///     Logging methods for the <see cref="HomeViewModel"/> class.
/// </summary>
public partial class HomeViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to preload recently used templates during ViewModel activation.")]
    private static partial void LogPreloadingRecentTemplatesError(ILogger logger, Exception ex);

    private void LogPreloadingRecentTemplatesError(Exception ex)
        => LogPreloadingRecentTemplatesError(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to preload recently used projects during ViewModel activation.")]
    private static partial void LogPreloadingRecentProjectsError(ILogger logger, Exception ex);

    private void LogPreloadingRecentProjectsError(Exception ex)
        => LogPreloadingRecentProjectsError(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "New project from template: {Category}/{Template} with name `{ProjectName}` in location `{Location}`.")]
    private static partial void LogNewProjectFromTemplate(ILogger logger, string category, string template, string projectName, string location);

    [Conditional("DEBUG")]
    private void LogNewProjectFromTemplate(string category, string template, string projectName, string location)
        => LogNewProjectFromTemplate(this.logger, category, template, projectName, location);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Opening project with name `{ProjectName}` in location `{Location}`.")]
    private static partial void LogOpenProject(ILogger logger, string projectName, string location);

    [Conditional("DEBUG")]
    private void LogOpenProject(string projectName, string location)
        => LogOpenProject(this.logger, projectName, location);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Updating recent projects; current count is {CurrentCount}...")]
    private static partial void LogUpdatingRecentProjects(ILogger logger, int currentCount);

    [Conditional("DEBUG")]
    private void LogUpdatingRecentProjects(int currentCount)
        => LogUpdatingRecentProjects(this.logger, currentCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Got recent project item: Id={Id}, Name={Name}, LastUsedOn={LastUsedOn}.")]
    private static partial void LogGotProjectInfo(ILogger logger, Guid id, string name, DateTime? lastUsedOn);

    [Conditional("DEBUG")]
    private void LogGotProjectInfo(Guid id, string name, DateTime? lastUsedOn)
        => LogGotProjectInfo(this.logger, id, name, lastUsedOn);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Purge recent project item (not in DB results): Id={Id}, Name={Name}.")]
    private static partial void LogPurgeRecentProjectItem(ILogger logger, Guid id, string name);

    [Conditional("DEBUG")]
    private void LogPurgeRecentProjectItem(Guid id, string name)
        => LogPurgeRecentProjectItem(this.logger, id, name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Completed loading of recent projects; final count is {FinalCount}.")]
    private static partial void LogCompletedLoadingRecentProjects(ILogger logger, int finalCount);

    [Conditional("DEBUG")]
    private void LogCompletedLoadingRecentProjects(int finalCount)
        => LogCompletedLoadingRecentProjects(this.logger, finalCount);
}
