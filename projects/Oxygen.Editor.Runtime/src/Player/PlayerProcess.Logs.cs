// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Runtime.Player;

/// <summary>Logger message definitions for <see cref="PlayerProcess"/>.</summary>
public sealed partial class PlayerProcess
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Started the player (process {ProcessId}): '{Executable}' --launch '{LaunchFile}'.")]
    private partial void LogStarted(int processId, string executable, string launchFile);

    [LoggerMessage(Level = LogLevel.Information, Message = "Connected to the player (process {ProcessId}) on port {Port} as its owner.")]
    private partial void LogConnected(int processId, int port);

    [LoggerMessage(Level = LogLevel.Information, Message = "The player (process {ProcessId}) ended.")]
    private partial void LogQuit(int processId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The player (process {ProcessId}) did not end when asked; it was killed.")]
    private partial void LogKilled(int processId);
}
