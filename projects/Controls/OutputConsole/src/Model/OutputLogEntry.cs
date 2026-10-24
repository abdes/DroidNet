// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Serilog.Events;

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     A lightweight, UI-friendly representation of a log entry.
///     Decoupled from Serilog's <see cref="LogEvent" /> to keep the control reusable.
/// </summary>
/// <remarks>
///     Producers set the init-only members. <see cref="Sequence" /> and <see cref="Subsystem" /> are set
///     by <see cref="OutputLogBuffer.Append" />, once, when the entry enters a buffer.
/// </remarks>
public sealed class OutputLogEntry
{
    /// <summary>The name of the log property that holds the entry's <see cref="Source" />, as Serilog names it.</summary>
    public const string SourcePropertyName = "SourceContext";

    /// <summary>The name of the log property that holds the entry's <see cref="Channel" />.</summary>
    public const string ChannelPropertyName = "Channel";

    private string? exceptionText;

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
    ///     Gets the optional full name of the entry's producer (for example, a Serilog <c>SourceContext</c>).
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
    ///     Gets the short subsystem name (for example <c>Engine</c>), resolved from <see cref="Source" /> by the
    ///     <see cref="SubsystemMap" /> of the buffer the entry was appended to. <see langword="null" /> when the map
    ///     has no match, and for entries never appended to a buffer.
    /// </summary>
    public string? Subsystem { get; internal set; }

    /// <summary>
    ///     Gets the entry's position in the buffer it was appended to: starting at 1 and strictly increasing for
    ///     that buffer's lifetime. <c>0</c> for entries never appended to a buffer.
    /// </summary>
    public long Sequence { get; internal set; }

    /// <summary>
    ///     Gets the rendered <see cref="Exception" /> (type, message and stack trace), computed on first use and
    ///     cached; <see langword="null" /> when the entry has no exception.
    /// </summary>
    public string? ExceptionText
    {
        get
        {
            if (this.Exception is null)
            {
                return null;
            }

            return this.exceptionText ??= this.Exception.ToString();
        }
    }

    /// <summary>
    ///     Returns the entry message.
    /// </summary>
    /// <inheritdoc />
    public override string ToString() => this.Message;
}
