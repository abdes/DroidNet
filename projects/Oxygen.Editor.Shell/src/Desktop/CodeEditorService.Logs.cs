// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Shell.Desktop;

/// <summary>
/// Logging helpers for <see cref="CodeEditorService"/>.
/// </summary>
public sealed partial class CodeEditorService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Discarded a code editor preference with unknown version {Version}")]
    private static partial void LogPreferenceDiscarded(ILogger logger, int version);

    private void LogPreferenceDiscarded(int version) => LogPreferenceDiscarded(this.logger, version);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Could not read the code editor preference")]
    private static partial void LogPreferenceUnreadable(ILogger logger, Exception failure);

    private void LogPreferenceUnreadable(Exception failure) => LogPreferenceUnreadable(this.logger, failure);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Could not save the code editor preference")]
    private static partial void LogPreferenceNotSaved(ILogger logger, Exception failure);

    private void LogPreferenceNotSaved(Exception failure) => LogPreferenceNotSaved(this.logger, failure);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Opened `{Path}` in the code editor: {Program}")]
    private static partial void LogEditorStarted(ILogger logger, string path, string program);

    private void LogEditorStarted(string path, ProcessStartInfo start)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var program = start.FileName;
        LogEditorStarted(this.logger, path, program);
    }
}
