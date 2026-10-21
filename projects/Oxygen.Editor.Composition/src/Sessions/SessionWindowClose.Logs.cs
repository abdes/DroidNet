// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.UI;

namespace Oxygen.Editor.Sessions;

/// <summary>Logging helpers for <see cref="SessionWindowClose" />.</summary>
internal sealed partial class SessionWindowClose
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Window {Window} is the project session's last window: the Start window opens, then the session closes.")]
    private static partial void LogReturningToStart(ILogger logger, ulong window);

    private void LogReturningToStart(WindowId window) => LogReturningToStart(this.logger, window.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The Start window did not open: window {Window} and its project session stay open.")]
    private static partial void LogStartWindowFailed(ILogger logger, ulong window);

    private void LogStartWindowFailed(WindowId window) => LogStartWindowFailed(this.logger, window.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Window {Window} is not a project session's last workspace window (a session is open: {HasSession}, it is closing: {IsClosing}); the session takes no part in its close.")]
    private static partial void LogNotLastWindow(ILogger logger, ulong window, bool hasSession, bool isClosing);

    private void LogNotLastWindow(WindowId window, bool hasSession, bool isClosing)
        => LogNotLastWindow(this.logger, window.Value, hasSession, isClosing);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The Start window opened before window {Window} closes; Start windows open: [{StartWindows}].")]
    private static partial void LogStartWindowOpened(ILogger logger, ulong window, string startWindows);

    private void LogStartWindowOpened(WindowId window, string startWindows) => LogStartWindowOpened(this.logger, window.Value, startWindows);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Window {Window} closed the project session's last workspace window: the session closes.")]
    private static partial void LogClosingSession(ILogger logger, ulong window);

    private void LogClosingSession(WindowId window) => LogClosingSession(this.logger, window.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The project session closed with window {Window}; {OpenWindows} windows remain open.")]
    private static partial void LogSessionClosed(ILogger logger, ulong window, int openWindows);

    private void LogSessionClosed(WindowId window, int openWindows) => LogSessionClosed(this.logger, window.Value, openWindows);
}
