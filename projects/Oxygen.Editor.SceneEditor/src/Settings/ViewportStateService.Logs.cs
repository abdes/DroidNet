// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneEditor.Settings;

/// <summary>Logging helpers for <see cref="ViewportStateService"/>.</summary>
public sealed partial class ViewportStateService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Restored viewport state for {SceneCount} scene(s) of project {ProjectRoot}.")]
    private static partial void LogRestored(ILogger logger, int sceneCount, FileSystemPath projectRoot);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogRestored(int sceneCount)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var root = this.Project.ProjectRoot;
        LogRestored(this.logger, sceneCount, root);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Discarded saved viewport state ({Code}) of project {ProjectRoot}.")]
    private static partial void LogDiscarded(ILogger logger, Exception exception, string code, FileSystemPath projectRoot);

    private void LogDiscarded(Exception exception, string code)
    {
        if (!this.logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        var root = this.Project.ProjectRoot;
        LogDiscarded(this.logger, exception, code, root);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Viewport state was not saved for project {ProjectRoot}.")]
    private static partial void LogWriteFailed(ILogger logger, Exception exception, FileSystemPath projectRoot);

    private void LogWriteFailed(Exception exception)
    {
        if (!this.logger.IsEnabled(LogLevel.Error))
        {
            return;
        }

        var root = this.Project.ProjectRoot;
        LogWriteFailed(this.logger, exception, root);
    }
}
