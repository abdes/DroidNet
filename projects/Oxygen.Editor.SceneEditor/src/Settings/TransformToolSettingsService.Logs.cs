// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.SceneEditor.Settings;

/// <summary>Logging helpers for <see cref="TransformToolSettingsService"/>.</summary>
public sealed partial class TransformToolSettingsService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The transform tool preferences could not be read; the defaults apply.")]
    private static partial void LogPreferencesUnreadable(ILogger logger, Exception exception);

    private void LogPreferencesUnreadable(Exception exception)
        => LogPreferencesUnreadable(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Discarded transform tool preferences of version {Version}; the defaults apply.")]
    private static partial void LogPreferencesDiscarded(ILogger logger, int version);

    private void LogPreferencesDiscarded(int version)
        => LogPreferencesDiscarded(this.logger, version);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The transform tool preferences could not be saved.")]
    private static partial void LogPreferencesNotSaved(ILogger logger, Exception exception);

    private void LogPreferencesNotSaved(Exception exception)
        => LogPreferencesNotSaved(this.logger, exception);
}
