// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>A line of the player's output, or the mark that a new session starts.</summary>
/// <param name="timestamp">When the editor received the line.</param>
/// <param name="level">The line's severity.</param>
/// <param name="channel">The player's log channel, if any.</param>
/// <param name="message">The line.</param>
/// <param name="sessionStart">Whether a new session starts here.</param>
public sealed class PlayerLogEventArgs(DateTimeOffset timestamp, LogLevel level, string? channel, string message, bool sessionStart = false) : EventArgs
{
    /// <summary>Gets when the editor received the line.</summary>
    public DateTimeOffset Timestamp { get; } = timestamp;

    /// <summary>Gets the line's severity.</summary>
    public LogLevel Level { get; } = level;

    /// <summary>Gets the player's log channel, if any.</summary>
    public string? Channel { get; } = channel;

    /// <summary>Gets the line; empty for a session start.</summary>
    public string Message { get; } = message;

    /// <summary>Gets a value indicating whether a new session starts here.</summary>
    public bool IsSessionStart { get; } = sessionStart;

    /// <summary>Creates the mark that a new session starts.</summary>
    /// <returns>The session-start mark.</returns>
    public static PlayerLogEventArgs SessionStarted() => new(DateTimeOffset.Now, LogLevel.None, channel: null, string.Empty, sessionStart: true);
}
