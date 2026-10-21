// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.UI;

namespace Oxygen.Editor.Services;

/// <summary>Logging helpers for <see cref="EditorExit" />.</summary>
internal sealed partial class EditorExit
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The editor exits with its last window {Window}: any project session closes, then the engine shuts down.")]
    private static partial void LogExiting(ILogger logger, ulong window);

    private void LogExiting(WindowId window) => LogExiting(this.logger, window.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Window {Window} closes (last window: {IsLastWindow}, a project session's window: {IsSessionWindow}); the editor exits: {Exits}.")]
    private static partial void LogDecided(ILogger logger, ulong window, bool isLastWindow, bool isSessionWindow, bool exits);

    private void LogDecided(WindowId window, bool isLastWindow, bool isSessionWindow, bool exits)
        => LogDecided(this.logger, window.Value, isLastWindow, isSessionWindow, exits);
}
