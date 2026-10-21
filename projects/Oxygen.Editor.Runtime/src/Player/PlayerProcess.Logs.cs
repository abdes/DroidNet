// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Runtime.Player;

/// <summary>Logging helpers for <see cref="PlayerProcess"/>.</summary>
public sealed partial class PlayerProcess
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Started the player (process {ProcessId}): '{Executable}' --launch '{LaunchFile}'.")]
    private static partial void LogStarted(ILogger logger, int processId, string executable, string launchFile);

    private void LogStarted(int processId, string executable, string launchFile)
        => LogStarted(this.logger, processId, executable, launchFile);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Connected to the player (process {ProcessId}) on port {Port} as its owner.")]
    private static partial void LogConnected(ILogger logger, int processId, int port);

    private void LogConnected(int processId, int port)
        => LogConnected(this.logger, processId, port);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The player (process {ProcessId}) ended.")]
    private static partial void LogQuit(ILogger logger, int processId);

    private void LogQuit(int processId)
        => LogQuit(this.logger, processId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The player (process {ProcessId}) did not end when asked; it was killed.")]
    private static partial void LogKilled(ILogger logger, int processId);

    private void LogKilled(int processId)
        => LogKilled(this.logger, processId);
}
