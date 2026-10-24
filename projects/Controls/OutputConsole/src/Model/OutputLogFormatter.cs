// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text;
using Serilog.Events;

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     Renders <see cref="OutputLogEntry" /> instances as plain text: the copy format, and the record format of the
///     details pane.
/// </summary>
internal static class OutputLogFormatter
{
    private const string TimestampFormat = "yyyy-MM-dd HH:mm:ss.fff";
    private const string Indent = "  ";

    /// <summary>
    ///     Gets the three-letter tag of a severity level.
    /// </summary>
    /// <param name="level">The severity level.</param>
    /// <returns>One of <c>VRB</c>, <c>DBG</c>, <c>INF</c>, <c>WRN</c>, <c>ERR</c>, <c>FTL</c>.</returns>
    public static string LevelTag(LogEventLevel level) => level switch
    {
        LogEventLevel.Verbose => "VRB",
        LogEventLevel.Debug => "DBG",
        LogEventLevel.Information => "INF",
        LogEventLevel.Warning => "WRN",
        LogEventLevel.Error => "ERR",
        LogEventLevel.Fatal => "FTL",
        _ => level.ToString(),
    };

    /// <summary>
    ///     Appends the entry's line, followed by its exception text (if any) indented by two spaces.
    /// </summary>
    /// <param name="builder">The destination.</param>
    /// <param name="entry">The entry to render.</param>
    public static void AppendLine(StringBuilder builder, OutputLogEntry entry)
    {
        AppendHeader(builder, entry);
        AppendException(builder, entry);
    }

    /// <summary>
    ///     Renders the entries, in order, with <see cref="AppendLine" />.
    /// </summary>
    /// <param name="entries">The entries to render.</param>
    /// <returns>The rendered text.</returns>
    public static string Format(IEnumerable<OutputLogEntry> entries)
    {
        var builder = new StringBuilder();
        foreach (var entry in entries)
        {
            AppendLine(builder, entry);
        }

        return builder.ToString();
    }

    /// <summary>
    ///     Appends the details record of an entry: the line, <c>Source:</c>, <c>Channel:</c>, the properties sorted by
    ///     key, then the exception.
    /// </summary>
    /// <param name="builder">The destination.</param>
    /// <param name="entry">The entry to render.</param>
    public static void AppendDetails(StringBuilder builder, OutputLogEntry entry)
    {
        AppendHeader(builder, entry);

        if (entry.Source is not null)
        {
            _ = builder.Append("Source: ").Append(entry.Source).Append(Environment.NewLine);
        }

        if (entry.Channel is not null)
        {
            _ = builder.Append("Channel: ").Append(entry.Channel).Append(Environment.NewLine);
        }

        AppendProperties(builder, entry);
        AppendException(builder, entry);
    }

    /// <summary>
    ///     Formats a property value for display, using the invariant culture.
    /// </summary>
    /// <param name="value">The value; may be <see langword="null" />.</param>
    /// <returns>The display text.</returns>
    public static string FormatValue(object? value)
        => value switch
        {
            null => "null",
            string text => $"\"{text}\"",
            bool flag => flag ? "true" : "false",
            IFormattable formattable => formattable.ToString(format: null, CultureInfo.InvariantCulture),
            IReadOnlyDictionary<string, object?> readOnlyMap => FormatMap(readOnlyMap),
            IDictionary<string, object?> map => FormatMap(map),
            System.Collections.IEnumerable sequence => FormatSequence(sequence),
            _ => Convert.ToString(value, CultureInfo.InvariantCulture) ?? string.Empty,
        };

    private static void AppendHeader(StringBuilder builder, OutputLogEntry entry)
    {
        // The console's row prefix, with the date: [2026-10-24 18:13:56.503 INF (Router)] message.
        _ = builder
            .Append('[')
            .Append(entry.Timestamp.ToString(TimestampFormat, CultureInfo.InvariantCulture))
            .Append(' ')
            .Append(LevelTag(entry.Level));

        if (entry.Subsystem is not null)
        {
            _ = builder.Append(" (").Append(entry.Subsystem).Append(')');
        }

        _ = builder.Append("] ").Append(entry.Message).Append(Environment.NewLine);
    }

    private static void AppendException(StringBuilder builder, OutputLogEntry entry)
    {
        var text = entry.ExceptionText;
        if (text is null)
        {
            return;
        }

        foreach (var line in text.Split(["\r\n", "\n"], StringSplitOptions.None))
        {
            _ = builder.Append(Indent).Append(line).Append(Environment.NewLine);
        }
    }

    private static void AppendProperties(StringBuilder builder, OutputLogEntry entry)
    {
        var keys = entry.Properties.Keys
            .Where(static key => !string.Equals(key, OutputLogEntry.SourcePropertyName, StringComparison.Ordinal)
                && !string.Equals(key, OutputLogEntry.ChannelPropertyName, StringComparison.Ordinal))
            .Order(StringComparer.Ordinal)
            .ToList();

        if (keys.Count == 0)
        {
            return;
        }

        _ = builder.Append("Properties:").Append(Environment.NewLine);
        foreach (var key in keys)
        {
            _ = builder
                .Append(Indent)
                .Append(key)
                .Append(" = ")
                .Append(FormatValue(entry.Properties[key]))
                .Append(Environment.NewLine);
        }
    }

    private static string FormatMap(IEnumerable<KeyValuePair<string, object?>> map)
    {
        var parts = map.Select(static pair => $"{pair.Key}: {FormatValue(pair.Value)}").ToList();
        return parts.Count == 0 ? "{ }" : "{ " + string.Join(", ", parts) + " }";
    }

    private static string FormatSequence(System.Collections.IEnumerable sequence)
        => "[" + string.Join(", ", sequence.Cast<object?>().Select(static item => FormatValue(item))) + "]";
}
