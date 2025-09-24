// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using DroidNet.Controls.OutputConsole.Model;
using Serilog.Core;
using Serilog.Events;

namespace DroidNet.Controls.OutputConsole.Sinks;

/// <summary>
///     Serilog sink that translates <see cref="LogEvent" /> instances to <see cref="OutputLogEntry" />
///     and appends them to an <see cref="OutputLogBuffer" />.
/// </summary>
/// <param name="buffer">
///     The <see cref="OutputLogBuffer" /> that will receive translated log entries. Cannot be
///     <see langword="null" />.
/// </param>
public sealed class BufferedSink(OutputLogBuffer buffer) : ILogEventSink
{
    private readonly OutputLogBuffer buffer = buffer ?? throw new ArgumentNullException(nameof(buffer));

    /// <summary>
    ///     Receives a Serilog <see cref="LogEvent" />, converts it to an <see cref="OutputLogEntry" />
    ///     and appends it to the configured <see cref="OutputLogBuffer" />.
    /// </summary>
    /// <param name="logEvent">The log event to emit. The parameter is required and must not be <see langword="null" />.</param>
    /// <remarks>
    ///     Message rendering uses <see cref="CultureInfo.CurrentCulture" /> when formatting.
    ///     Properties from the <see cref="LogEvent" /> are simplified to primitive CLR values
    ///     where possible and copied into the entry's <see cref="OutputLogEntry.Properties" />.
    /// </remarks>
    public void Emit(LogEvent logEvent)
    {
        var entry = new OutputLogEntry
        {
            Timestamp = logEvent.Timestamp,
            Level = logEvent.Level,
            Message = logEvent.RenderMessage(CultureInfo.CurrentCulture),
            Source = logEvent.Properties.TryGetValue("SourceContext", out var sc) ? sc.ToString().Trim('"') : null,
            Channel = logEvent.Properties.TryGetValue("Channel", out var ch) ? ch.ToString().Trim('"') : null,
            Exception = logEvent.Exception,
            Properties = ConvertProps(logEvent.Properties),
        };
        this.buffer.Append(entry);
    }

    private static ReadOnlyDictionary<string, object?> ConvertProps(
        IReadOnlyDictionary<string, LogEventPropertyValue> props)
    {
        var dict = new Dictionary<string, object?>(props.Count, StringComparer.Ordinal);

        foreach (var kvp in props)
        {
            dict[kvp.Key] = Simplify(kvp.Value);
        }

        return new ReadOnlyDictionary<string, object?>(dict);
    }

    private static object? Simplify(LogEventPropertyValue v) => v switch
    {
        ScalarValue s => s.Value,
        SequenceValue seq => seq.Elements is { Count: 0 } ? [] : MapSeq(seq.Elements),
        StructureValue str => MapStruct(str.Properties),
        DictionaryValue dv => MapDict(dv.Elements),
        _ => v.ToString(),
    };

    private static object?[] MapSeq(IReadOnlyList<LogEventPropertyValue> items)
    {
        var arr = new object?[items.Count];
        for (var i = 0; i < items.Count; i++)
        {
            arr[i] = Simplify(items[i]);
        }

        return arr;
    }

    private static Dictionary<string, object?> MapStruct(IReadOnlyList<LogEventProperty> props)
    {
        var d = new Dictionary<string, object?>(StringComparer.Ordinal);
        foreach (var p in props)
        {
            d[p.Name] = Simplify(p.Value);
        }

        return d;
    }

    private static Dictionary<string, object?> MapDict(IReadOnlyDictionary<ScalarValue, LogEventPropertyValue> props)
    {
        var d = new Dictionary<string, object?>(StringComparer.Ordinal);
        foreach (var p in props)
        {
            d[p.Key.Value?.ToString() ?? "null"] = Simplify(p.Value);
        }

        return d;
    }
}
