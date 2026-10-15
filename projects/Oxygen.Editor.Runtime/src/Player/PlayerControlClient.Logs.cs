// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Runtime.Player;

#pragma warning disable SA1204 // Each log method keeps its static and instance forms together

/// <summary>Logger message definitions for <see cref="PlayerControlClient"/>.</summary>
public sealed partial class PlayerControlClient
{
    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Player request {Id}: {Method}.")]
    private static partial void LogRequest(ILogger logger, string method, long id);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogRequest(string method, long id) => LogRequest(this.logger, method, id);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Player request {Id} failed with code {Code}: {Message}")]
    private partial void LogRequestFailed(long id, int code, string message);

    [LoggerMessage(Level = LogLevel.Error, Message = "A message from the player could not be handled.")]
    private partial void LogMessageFailed(Exception exception);

    [LoggerMessage(Level = LogLevel.Information, Message = "The player connection closed.")]
    private partial void LogDisconnected();
}
