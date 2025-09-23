// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using Serilog.Events;

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
/// A lightweight, UI-friendly representation of a log entry.
/// Decoupled from Serilog's <see cref="LogEvent"/> to keep the control reusable.
/// </summary>
public sealed class OutputLogEntry
{
    public DateTimeOffset Timestamp { get; init; }
    public LogEventLevel Level { get; init; }
    public string Message { get; init; } = string.Empty;
    public string? Source { get; init; }
    public string? Channel { get; init; }
    public Exception? Exception { get; init; }
    public IReadOnlyDictionary<string, object?> Properties { get; init; } = new Dictionary<string, object?>();

    // Optional coalescing count when many identical consecutive messages appear
    public int RepeatCount { get; set; } = 1;

    public override string ToString() => Message;
}
