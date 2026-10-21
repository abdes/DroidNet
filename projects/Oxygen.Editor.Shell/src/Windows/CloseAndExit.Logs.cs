// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Shell.Windows;

/// <summary>Logging helpers for <see cref="CloseAndExit" />.</summary>
public sealed partial class CloseAndExit
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Window {Window} is closing, with {OpenWindows} windows open (last window: {IsLastWindow}).")]
    private static partial void LogClosing(ILogger logger, ulong window, int openWindows, bool isLastWindow);

    private void LogClosing(WindowClose close, int openWindows)
        => LogClosing(this.logger, close.Window.Value, openWindows, close.IsLastWindow);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Close of window {Window} (last window: {IsLastWindow}): {Participant} at level {Level} prepared {Part}.")]
    private static partial void LogPrepared(ILogger logger, ulong window, bool isLastWindow, string participant, WindowCloseLevel level, string part);

    private void LogPrepared(WindowClose close, IWindowCloseParticipant participant, WindowCloseStep step)
        => LogPrepared(
            this.logger,
            close.Window.Value,
            close.IsLastWindow,
            participant.GetType().Name,
            participant.Level,
            ReferenceEquals(step, WindowCloseStep.None) ? "no part" : "its part");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Window {Window} stays open: a participant of level {Level} refused to close it.")]
    private static partial void LogKeptOpen(ILogger logger, ulong window, WindowCloseLevel level);

    private void LogKeptOpen(WindowClose close, WindowCloseLevel level) => LogKeptOpen(this.logger, close.Window.Value, level);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Window {Window} stays open: another handler of its close refused it.")]
    private static partial void LogNotApproved(ILogger logger, ulong window);

    private void LogNotApproved(WindowClose close) => LogNotApproved(this.logger, close.Window.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Window {Window} stays open: a participant could not commit its part of the close.")]
    private static partial void LogCommitRefused(ILogger logger, ulong window);

    private void LogCommitRefused(WindowClose close) => LogCommitRefused(this.logger, close.Window.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Close of window {Window} committed; its participants finalize next.")]
    private static partial void LogCommitted(ILogger logger, ulong window);

    private void LogCommitted(WindowClose close) => LogCommitted(this.logger, close.Window.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Close of window {Window} finalized; the window is destroyed next.")]
    private static partial void LogFinalized(ILogger logger, ulong window);

    private void LogFinalized(WindowClose close) => LogFinalized(this.logger, close.Window.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Close of window {Window}: the participant of level {Level} failed to finalize.")]
    private static partial void LogFinalizeFailed(ILogger logger, Exception exception, ulong window, WindowCloseLevel level);

    private void LogFinalizeFailed(Exception exception, WindowClose close, WindowCloseLevel level)
        => LogFinalizeFailed(this.logger, exception, close.Window.Value, level);
}
