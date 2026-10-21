// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.Sessions;

/// <summary>Logging helpers for <see cref="ProjectSessionHost" />.</summary>
public sealed partial class ProjectSessionHost
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Project session opened for '{Project}' at {Path}.")]
    private static partial void LogOpened(ILogger logger, string project, string path);

    private void LogOpened(ProjectContext project)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var path = project.ProjectRoot.FullPath;
        LogOpened(this.logger, project.Name, path);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Project session for '{Project}' at {Path} failed to open.")]
    private static partial void LogOpenFailed(ILogger logger, Exception failure, string project, string path);

    private void LogOpenFailed(ProjectContext project, Exception failure)
    {
        if (!this.logger.IsEnabled(LogLevel.Error))
        {
            return;
        }

        var path = project.ProjectRoot.FullPath;
        LogOpenFailed(this.logger, failure, project.Name, path);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Project session for '{Project}' follows its new content mounts: {LocalFolders} local folders, {Libraries} libraries.")]
    private static partial void LogContextChanged(ILogger logger, string project, int localFolders, int libraries);

    private void LogContextChanged(ProjectContext next)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var localFolders = next.LocalFolderMounts.Count;
        var libraries = next.PakFileMounts.Count;
        LogContextChanged(this.logger, next.Name, localFolders, libraries);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Project session for '{Project}' is closing: its work in flight is cancelled and its services are disposed.")]
    private static partial void LogClosing(ILogger logger, string project);

    private void LogClosing(ProjectContext project) => LogClosing(this.logger, project.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Window {Window} of the closing project session finishes its own close.")]
    private static partial void LogWindowNotClosed(ILogger logger, ulong window);

    private void LogWindowNotClosed(Microsoft.UI.WindowId window) => LogWindowNotClosed(this.logger, window.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Project session for '{Project}' closed.")]
    private static partial void LogClosed(ILogger logger, string project);

    private void LogClosed(ProjectContext project) => LogClosed(this.logger, project.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Project session for '{Project}' failed to dispose its services.")]
    private static partial void LogDisposeFailed(ILogger logger, Exception failure, string project);

    private void LogDisposeFailed(ProjectContext project, Exception failure) => LogDisposeFailed(this.logger, failure, project.Name);
}
