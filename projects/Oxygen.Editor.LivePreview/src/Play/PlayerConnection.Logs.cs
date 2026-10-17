// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>
///     Logging helpers for <see cref="PlayerConnection"/>.
/// </summary>
internal sealed partial class PlayerConnection
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The player began a session on '{Scene}', viewed through '{Camera}'.")]
    private static partial void LogSessionBegan(ILogger logger, string scene, string camera);

    private void LogSessionBegan(string scene, string camera)
        => LogSessionBegan(this.logger, scene, camera);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The player did not end a session: {Message}")]
    private static partial void LogSessionEndRefused(ILogger logger, string message);

    private void LogSessionEndRefused(string message)
        => LogSessionEndRefused(this.logger, message);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The player connection ended; expected: {Expected}.")]
    private static partial void LogPlayerGone(ILogger logger, bool expected);

    private void LogPlayerGone(bool expected)
        => LogPlayerGone(this.logger, expected);
}
