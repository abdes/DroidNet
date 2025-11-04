// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Theming;

/// <summary>
///     Provides an implementation of the <see cref="IAppThemeModeService"/> interface to manage and apply theme modes to application windows.
/// </summary>
public partial class AppThemeModeService
{
    [LoggerMessage(
        EventId = 1,
        Level = LogLevel.Error,
        Message = "Failed to change theme mode of the app")]
    static partial void LogFailedToChangeThemeMode(ILogger logger, Exception exception);

    private void LogFailedToChangeThemeMode(Exception exception) => LogFailedToChangeThemeMode(this.logger, exception);

    [LoggerMessage(
        EventId = 2,
        Level = LogLevel.Debug,
        Message = "AppThemeModeService: theme mode setting changed")]
    static partial void LogThemeModeSettingChanged(ILogger logger);

    [Conditional("DEBUG")]
    private void LogThemeModeSettingChanged() => LogThemeModeSettingChanged(this.logger);
}
