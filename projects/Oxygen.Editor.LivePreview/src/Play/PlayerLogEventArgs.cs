// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>A line of the player's output, or the clear that starts a new session.</summary>
/// <param name="timestamp">When the editor received the line.</param>
/// <param name="level">The line's severity.</param>
/// <param name="channel">The player's log channel, if any.</param>
/// <param name="message">The line.</param>
/// <param name="clear">Whether the earlier output ends here: a new session started.</param>
public sealed class PlayerLogEventArgs(DateTimeOffset timestamp, LogLevel level, string? channel, string message, bool clear = false) : EventArgs
{
    /// <summary>Gets when the editor received the line.</summary>
    public DateTimeOffset Timestamp { get; } = timestamp;

    /// <summary>Gets the line's severity.</summary>
    public LogLevel Level { get; } = level;

    /// <summary>Gets the player's log channel, if any.</summary>
    public string? Channel { get; } = channel;

    /// <summary>Gets the line; empty for a clear.</summary>
    public string Message { get; } = message;

    /// <summary>Gets a value indicating whether the earlier output ends here because a new session started.</summary>
    public bool Clear { get; } = clear;

    /// <summary>Creates the clear that starts a new session's output.</summary>
    /// <returns>The clear.</returns>
    public static PlayerLogEventArgs SessionStarted() => new(DateTimeOffset.Now, LogLevel.None, channel: null, string.Empty, clear: true);
}
