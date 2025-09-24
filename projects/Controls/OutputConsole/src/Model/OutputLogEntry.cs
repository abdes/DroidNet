// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Serilog.Events;

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     A lightweight, UI-friendly representation of a log entry.
///     Decoupled from Serilog's <see cref="LogEvent" /> to keep the control reusable.
/// </summary>
public sealed class OutputLogEntry
{
    /// <summary>
    ///     Gets the timestamp of the log entry.
    /// </summary>
    public DateTimeOffset Timestamp { get; init; }

    /// <summary>
    ///     Gets the severity level of the log entry.
    /// </summary>
    public LogEventLevel Level { get; init; }

    /// <summary>
    ///     Gets the human-readable message associated with the entry. Never <see langword="null" />; defaults to an empty
    ///     string.
    /// </summary>
    public string Message { get; init; } = string.Empty;

    /// <summary>
    ///     Gets the optional source identifier for the entry (for example, a class or component name).
    ///     May be <see langword="null" /> when unspecified.
    /// </summary>
    public string? Source { get; init; }

    /// <summary>
    ///     Gets the optional channel name for the entry. May be <see langword="null" /> when unspecified.
    /// </summary>
    public string? Channel { get; init; }

    /// <summary>
    ///     Gets the optional exception associated with the log entry. May be <see langword="null" />.
    /// </summary>
    public Exception? Exception { get; init; }

    /// <summary>
    ///     Gets the read-only dictionary of additional properties captured with the log event.
    ///     Values may be <see langword="null" />.
    /// </summary>
    public IReadOnlyDictionary<string, object?> Properties { get; init; } =
        new Dictionary<string, object?>(StringComparer.Ordinal);

    /// <summary>
    ///     Gets or sets the optional coalescing count when many identical consecutive messages appear.
    ///     Defaults to <c>1</c>.
    /// </summary>
    public int RepeatCount { get; set; } = 1;

    /// <summary>
    ///     Returns the entry message.
    /// </summary>
    /// <inheritdoc />
    public override string ToString() => this.Message;
}
