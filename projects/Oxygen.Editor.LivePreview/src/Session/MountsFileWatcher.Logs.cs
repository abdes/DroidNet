// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;

namespace Oxygen.Editor.LivePreview.Session;

/// <summary>Logging helpers for <see cref="MountsFileWatcher"/>.</summary>
/// <remarks>
/// Levels: Debug for each file event; Information for a mount table applied, found unchanged or overtaken by another
/// mount change; Warning for a change the mount table refuses.
/// </remarks>
public sealed partial class MountsFileWatcher
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Mounts file '{File}' {Change}.")]
    private static partial void LogFileChanged(ILogger logger, string file, WatcherChangeTypes change);

    private void LogFileChanged(string file, WatcherChangeTypes change)
    {
        if (this.logger.IsEnabled(LogLevel.Debug))
        {
            LogFileChanged(this.logger, file, change);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Mounts files of project '{ProjectRoot}' changed; the mount table is unchanged.")]
    private static partial void LogUnchanged(ILogger logger, string projectRoot);

    private void LogUnchanged(ProjectContext project)
    {
        if (this.logger.IsEnabled(LogLevel.Information))
        {
            LogUnchanged(this.logger, project.ProjectRoot.FullPath);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Mounts files of project '{ProjectRoot}' changed; applying the new mount table: {Bound} of {Count} mounts bound, {Findings} findings.")]
    private static partial void LogApplying(ILogger logger, string projectRoot, int bound, int count, int findings);

    private void LogApplying(ProjectContext project, ProjectMountTable table)
    {
        if (this.logger.IsEnabled(LogLevel.Information))
        {
            LogApplying(this.logger, project.ProjectRoot.FullPath, table.Mounts.Count(static mount => mount.IsBound), table.Mounts.Count, table.Findings.Count);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "A change of the mounts files of project '{ProjectRoot}' was overtaken by another mount change; checking again.")]
    private static partial void LogSuperseded(ILogger logger, string projectRoot);

    private void LogSuperseded()
    {
        if (this.logger.IsEnabled(LogLevel.Information))
        {
            LogSuperseded(this.logger, this.session.Context.ProjectRoot.FullPath);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "A change of the mounts files of project '{ProjectRoot}' was not applied; the project keeps its mounts.")]
    private static partial void LogRefused(ILogger logger, Exception failure, string projectRoot);

    private void LogRefused(Exception failure)
    {
        if (this.logger.IsEnabled(LogLevel.Warning))
        {
            LogRefused(this.logger, failure, this.session.Context.ProjectRoot.FullPath);
        }
    }
}
