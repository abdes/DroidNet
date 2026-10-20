// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// Logging helpers for <see cref="ProjectScriptTooling"/>.
/// </summary>
public sealed partial class ProjectScriptTooling
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{Action} script tooling file `{Path}`")]
    private static partial void LogFileWritten(ILogger logger, string action, string path);

    private void LogFileWritten(string path, bool existed)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var action = existed ? "Refreshed" : "Created";
        LogFileWritten(this.logger, action, path);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Added {Keys} to VS Code settings `{Path}`")]
    private static partial void LogSettingsAdded(ILogger logger, string path, string keys);

    private void LogSettingsAdded(string path, IReadOnlyList<string> added)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var keys = string.Join(", ", added);
        LogSettingsAdded(this.logger, path, keys);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Left VS Code settings `{Path}` unchanged: it has comments or is not plain JSON")]
    private static partial void LogSettingsLeftUnchanged(ILogger logger, string path);

    private void LogSettingsLeftUnchanged(string path) => LogSettingsLeftUnchanged(this.logger, path);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Could not update the script tooling files of `{ProjectRoot}`")]
    private static partial void LogUpdateFailed(ILogger logger, string projectRoot, Exception exception);

    private void LogUpdateFailed(string projectRoot, Exception exception) => LogUpdateFailed(this.logger, projectRoot, exception);
}
