// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputConsole.Model;
using Oxygen.Core.Diagnostics;
using Serilog.Events;

namespace Oxygen.Editor.Diagnostics;

/// <summary>
/// Adapts operation results into output-console summary log entries.
/// </summary>
public sealed class OperationResultOutputLogAdapter : IObserver<OperationResult>, IDisposable
{
    private readonly OutputLogBuffer buffer;
    private readonly IDisposable subscription;

    /// <summary>
    /// Initializes a new instance of the <see cref="OperationResultOutputLogAdapter"/> class.
    /// </summary>
    /// <param name="publisher">The operation result publisher.</param>
    /// <param name="buffer">The output log buffer.</param>
    public OperationResultOutputLogAdapter(IOperationResultPublisher publisher, OutputLogBuffer buffer)
    {
        ArgumentNullException.ThrowIfNull(publisher);
        this.buffer = buffer ?? throw new ArgumentNullException(nameof(buffer));
        this.subscription = publisher.Subscribe(this);
    }

    /// <inheritdoc/>
    public void OnCompleted()
    {
    }

    /// <inheritdoc/>
    public void OnError(Exception error)
    {
    }

    /// <inheritdoc/>
    public void OnNext(OperationResult value)
        => this.buffer.Append(
            new OutputLogEntry
            {
                Timestamp = value.CompletedAt ?? DateTimeOffset.Now,
                Level = ToLogEventLevel(value.Severity),
                Source = "OperationResult",
                Channel = "Operations",
                Message = $"{value.OperationKind}: {value.Title} - {value.Message}",
                Properties = new Dictionary<string, object?>(StringComparer.Ordinal)
                {
                    ["OperationId"] = value.OperationId,
                    ["OperationKind"] = value.OperationKind,
                    ["Status"] = value.Status.ToString(),
                    ["Severity"] = value.Severity.ToString(),
                },
            });

    /// <inheritdoc/>
    public void Dispose() => this.subscription.Dispose();

    private static LogEventLevel ToLogEventLevel(DiagnosticSeverity severity)
        => severity switch
        {
            DiagnosticSeverity.Fatal => LogEventLevel.Fatal,
            DiagnosticSeverity.Error => LogEventLevel.Error,
            DiagnosticSeverity.Warning => LogEventLevel.Warning,
            _ => LogEventLevel.Information,
        };
}
