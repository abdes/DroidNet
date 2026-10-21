// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Runtime.Player;

/// <summary>Logging helpers for <see cref="PlayerControlClient"/>.</summary>
public sealed partial class PlayerControlClient
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Player request {Id}: {Method}.")]
    private static partial void LogRequest(ILogger logger, string method, long id);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogRequest(string method, long id) => LogRequest(this.logger, method, id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Player request {Id} failed with code {Code}: {Message}")]
    private static partial void LogRequestFailed(ILogger logger, long id, int code, string message);

    private void LogRequestFailed(long id, int code, string message)
        => LogRequestFailed(this.logger, id, code, message);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "A message from the player could not be handled.")]
    private static partial void LogMessageFailed(ILogger logger, Exception exception);

    private void LogMessageFailed(Exception exception)
        => LogMessageFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The player connection closed.")]
    private static partial void LogDisconnected(ILogger logger);

    private void LogDisconnected()
        => LogDisconnected(this.logger);
}
