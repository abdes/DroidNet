// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using DroidNet.Controls.OutputConsole.Model;
using Serilog.Core;
using Serilog.Events;

namespace DroidNet.Controls.OutputConsole.Sinks;

/// <summary>
/// Serilog sink that translates LogEvents to OutputLogEntry and appends them to an OutputLogBuffer.
/// </summary>
public sealed class BufferedSink : ILogEventSink
{
    private readonly OutputLogBuffer _buffer;

    public BufferedSink(OutputLogBuffer buffer)
    {
        _buffer = buffer ?? throw new ArgumentNullException(nameof(buffer));
    }

    public void Emit(LogEvent logEvent)
    {
        var entry = new OutputLogEntry
        {
            Timestamp = logEvent.Timestamp,
            Level = logEvent.Level,
            Message = logEvent.RenderMessage(),
            Source = logEvent.Properties.TryGetValue("SourceContext", out var sc) ? sc.ToString().Trim('"') : null,
            Channel = logEvent.Properties.TryGetValue("Channel", out var ch) ? ch.ToString().Trim('"') : null,
            Exception = logEvent.Exception,
            Properties = ConvertProps(logEvent.Properties),
        };
        _buffer.Append(entry);
    }

    private static IReadOnlyDictionary<string, object?> ConvertProps(IReadOnlyDictionary<string, LogEventPropertyValue> props)
    {
        var dict = new Dictionary<string, object?>(StringComparer.Ordinal);
        foreach (var kvp in props)
        {
            dict[kvp.Key] = Simplify(kvp.Value);
        }
        return dict;
    }

    private static object? Simplify(LogEventPropertyValue v) => v switch
    {
        ScalarValue s => s.Value,
        SequenceValue seq => seq.Elements is { Count: 0 } ? Array.Empty<object?>() : MapSeq(seq.Elements),
        StructureValue str => MapStruct(str.Properties),
        DictionaryValue dv => MapDict(dv.Elements),
        _ => v.ToString(),
    };

    private static object?[] MapSeq(IReadOnlyList<LogEventPropertyValue> items)
    {
        var arr = new object?[items.Count];
        for (int i = 0; i < items.Count; i++) arr[i] = Simplify(items[i]);
        return arr;
    }

    private static Dictionary<string, object?> MapStruct(IReadOnlyList<LogEventProperty> props)
    {
        var d = new Dictionary<string, object?>(StringComparer.Ordinal);
        foreach (var p in props) d[p.Name] = Simplify(p.Value);
        return d;
    }

    private static Dictionary<string, object?> MapDict(IReadOnlyDictionary<ScalarValue, LogEventPropertyValue> props)
    {
        var d = new Dictionary<string, object?>(StringComparer.Ordinal);
        foreach (var p in props) d[p.Key.Value?.ToString() ?? "null"] = Simplify(p.Value);
        return d;
    }
}
