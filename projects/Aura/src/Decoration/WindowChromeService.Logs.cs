// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI;

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Logging helpers for <see cref="WindowChromeService"/>.
/// </summary>
public partial class WindowChromeService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Service initialized and subscribed to window events")]
    private static partial void LogServiceInitialized(ILogger logger);

    private void LogServiceInitialized()
        => LogServiceInitialized(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Applying chrome to window {WindowId} (ChromeEnabled: {ChromeEnabled})")]
    private static partial void LogApplyingChrome(ILogger logger, ulong windowId, bool chromeEnabled);

    [Conditional("DEBUG")]
    private void LogApplyingChrome(WindowId windowId, bool chromeEnabled)
        => LogApplyingChrome(this.logger, windowId.Value, chromeEnabled);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Chrome applied successfully to window {WindowId}")]
    private static partial void LogChromeApplied(ILogger logger, ulong windowId);

    private void LogChromeApplied(WindowId windowId)
        => LogChromeApplied(this.logger, windowId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to apply chrome to window {WindowId}")]
    private static partial void LogChromeApplicationFailed(ILogger logger, Exception ex, ulong windowId);

    private void LogChromeApplicationFailed(Exception ex, WindowId windowId)
        => LogChromeApplicationFailed(this.logger, ex, windowId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Applying chrome to all matching windows")]
    private static partial void LogApplyingChromeToWindows(ILogger logger);

    [Conditional("DEBUG")]
    private void LogApplyingChromeToWindows()
        => LogApplyingChromeToWindows(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Applying button visibility to window {WindowId} (Minimize: {ShowMinimize}, Maximize: {ShowMaximize})")]
    private static partial void LogApplyingButtons(ILogger logger, ulong windowId, bool showMinimize, bool showMaximize);

    [Conditional("DEBUG")]
    private void LogApplyingButtons(WindowId windowId, bool showMinimize, bool showMaximize)
        => LogApplyingButtons(this.logger, windowId.Value, showMinimize, showMaximize);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Applied tool window chrome - removed system title bar for window {WindowId}")]
    private static partial void LogToolWindowChromeApplied(ILogger logger, ulong windowId);

    [Conditional("DEBUG")]
    private void LogToolWindowChromeApplied(WindowId windowId)
        => LogToolWindowChromeApplied(this.logger, windowId.Value);
}
