// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Aura.Windowing;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

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
        Message = "Theme mode setting changed")]
    static partial void LogThemeModeSettingChanged(ILogger logger);

    [Conditional("DEBUG")]
    private void LogThemeModeSettingChanged() => LogThemeModeSettingChanged(this.logger);

    [LoggerMessage(
        EventId = 3,
        Level = LogLevel.Debug,
        Message = "Applying theme to all open windows (theme: {Theme})")]
    static partial void LogApplyingThemeToAllWindows(ILogger logger, string theme);

    private void LogApplyingThemeToAllWindows(ElementTheme theme) => LogApplyingThemeToAllWindows(this.logger, theme.ToString());

    [LoggerMessage(
        EventId = 4,
        Level = LogLevel.Debug,
        Message = "Applying theme to window (windowId: {WindowId}, theme: {Theme})")]
    static partial void LogApplyingThemeToWindow(ILogger logger, ulong windowId, string theme);

    private void LogApplyingThemeToWindow(Window window, ElementTheme theme)
        => LogApplyingThemeToWindow(this.logger, window.AppWindow.Id.Value, theme.ToString());

    [LoggerMessage(
        EventId = 5,
        Level = LogLevel.Debug,
        Message = "System theme changed, updating application (current theme: {CurrentTheme}, new theme: {NewTheme})")]
    static partial void LogSystemThemeChanged(ILogger logger, string currentTheme, string newTheme);

    private void LogSystemThemeChanged(ElementTheme theme)
        => LogSystemThemeChanged(
            this.logger,
            this.appearanceSettings.Settings.AppThemeMode.ToString(),
            theme.ToString());
}
