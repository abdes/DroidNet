// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Play;

/// <summary>Logger message definitions for <see cref="PlayerConnection"/>.</summary>
internal sealed partial class PlayerConnection
{
    [LoggerMessage(Level = LogLevel.Information, Message = "The player began a session on '{Scene}', viewed through '{Camera}'.")]
    private partial void LogSessionBegan(string scene, string camera);

    [LoggerMessage(Level = LogLevel.Information, Message = "The player did not end a session: {Message}")]
    private partial void LogSessionEndRefused(string message);

    [LoggerMessage(Level = LogLevel.Information, Message = "The player connection ended; expected: {Expected}.")]
    private partial void LogPlayerGone(bool expected);
}
