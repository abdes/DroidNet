// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using DroidNet.Controls.OutputLog.Theming;
using Serilog.Core;
using Serilog.Events;

namespace DroidNet.Controls.OutputLog;

/// <summary>
/// Represents a delegating sink that forwards log events to a specified sink.
/// </summary>
/// <typeparam name="T">The type of <see cref="ILogEventSink"/> to delegate log events to.</typeparam>
/// <param name="outputTemplate">The output template to format log events.</param>
/// <param name="formatProvider">An optional provider to format values.</param>
/// <param name="theme">An optional theme to apply to the output.</param>
/// <remarks>
/// <para>
/// This class is used to delegate log events to a specified sink, allowing for flexible and dynamic log event handling.
/// It ensures that log events are formatted and themed consistently according to the specified settings.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// Use this class to create a delegating sink that forwards log events to another sink. This can be useful for
/// scenarios where log events need to be processed or formatted before being forwarded to the final sink.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var delegatingSink = new DelegatingSink<RichTextBlockSink>(
///     outputTemplate: "[{Timestamp:HH:mm:ss} {Level:u3}] {Message:l} {Properties}{NewLine}{Exception}",
///     formatProvider: null,
///     theme: Themes.Literate);
/// delegatingSink.DelegateSink = new RichTextBlockSink(richTextBlock, renderer);
/// // Log events will be forwarded to the RichTextBlockSink with the specified formatting and theming
/// ]]></code>
/// </para>
/// </remarks>
public class DelegatingSink<T>(
    string outputTemplate,
    IFormatProvider? formatProvider,
    ThemeId? theme) : ILogEventSink
    where T : ILogEventSink
{
    private const int MaxBufferedEvents = 1000;
    private readonly ConcurrentQueue<LogEvent> buffer = new();
    private T? delegateSink;

    /// <summary>
    /// Gets or sets the delegate sink to forward log events to.
    /// </summary>
    public T? DelegateSink
    {
        get => this.delegateSink;
        set
        {
            this.delegateSink = value;

            // When a sink attaches (e.g., the view becomes active), flush buffered events
            if (this.delegateSink is not null)
            {
                while (this.buffer.TryDequeue(out var evt))
                {
                    this.delegateSink.Emit(evt);
                }
            }
        }
    }

    /// <summary>
    /// Gets the output template to format log events.
    /// </summary>
    public string OutputTemplate { get; } = outputTemplate;

    /// <summary>
    /// Gets the format provider to format values.
    /// </summary>
    public IFormatProvider? FormatProvider { get; } = formatProvider;

    /// <summary>
    /// Gets the theme to apply to the output.
    /// </summary>
    public ThemeId? Theme { get; } = theme;

    /// <summary>
    /// Emits the specified log event to the delegate sink.
    /// </summary>
    /// <param name="logEvent">The log event to emit.</param>
    /// <remarks>
    /// <para>
    /// This method forwards the log event to the delegate sink, if it is set.
    /// </para>
    /// </remarks>
    public void Emit(LogEvent logEvent)
    {
        var sink = this.delegateSink;
        if (sink is not null)
        {
            // If we have pending items (race), flush them first to preserve order
            while (this.buffer.TryDequeue(out var pending))
            {
                sink.Emit(pending);
            }

            sink.Emit(logEvent);
            return;
        }

        // No live sink attached yet — buffer the event (bounded)
        this.buffer.Enqueue(logEvent);
        while (this.buffer.Count > MaxBufferedEvents && this.buffer.TryDequeue(out _))
        {
            // drop oldest to keep memory bounded
        }
    }
}
