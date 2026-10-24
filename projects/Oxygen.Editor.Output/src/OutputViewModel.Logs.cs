// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Output;

/// <summary>
///     Logging helpers for <see cref="OutputViewModel"/>.
/// </summary>
public partial class OutputViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The Output panel preferences could not be read; the defaults apply.")]
    private static partial void LogPreferencesUnreadable(ILogger logger, Exception exception);

    private void LogPreferencesUnreadable(Exception exception)
        => LogPreferencesUnreadable(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Discarded Output panel preferences of version {Version}; the defaults apply.")]
    private static partial void LogPreferencesDiscarded(ILogger logger, int version);

    private void LogPreferencesDiscarded(int version)
        => LogPreferencesDiscarded(this.logger, version);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The Output panel preferences could not be saved.")]
    private static partial void LogPreferencesNotSaved(ILogger logger, Exception exception);

    private void LogPreferencesNotSaved(Exception exception)
        => LogPreferencesNotSaved(this.logger, exception);
}
