// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputLog.Output;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Serilog.Core;
using Serilog.Events;

namespace DroidNet.Controls.OutputLog;

/// <summary>
/// A sink that writes log events to a <see cref="RichTextBlock"/> control.
/// </summary>
/// <param name="richTextBlock">The <see cref="RichTextBlock"/> control to write log events to.</param>
/// <param name="renderer">The renderer that formats log events into rich text paragraphs.</param>
/// <remarks>
/// <para>
/// This class is used to write log events to a <see cref="RichTextBlock"/> control, allowing for rich text formatting
/// and theming of log output. It ensures that log events are displayed in a visually appealing and consistent manner.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// Use this class to display log events in a <see cref="RichTextBlock"/> control. Configure the renderer to apply
/// the desired formatting and theming to the log events.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var richTextBlock = new RichTextBlock();
/// var renderer = new OutputTemplateRenderer(theme, outputTemplate, formatProvider);
/// var sink = new RichTextBlockSink(richTextBlock, renderer);
/// var logger = new LoggerConfiguration()
///     .WriteTo.Sink(sink)
///     .CreateLogger();
/// ]]></code>
/// </para>
/// </remarks>
public class RichTextBlockSink(RichTextBlock richTextBlock, ILogEventRenderer renderer)
    : ILogEventSink
{
    /// <summary>
    /// Emits the specified log event to the <see cref="RichTextBlock"/> control.
    /// </summary>
    /// <param name="logEvent">The log event to emit.</param>
    /// <remarks>
    /// <para>
    /// This method writes the log event to the <see cref="RichTextBlock"/> control, ensuring that the log event
    /// is formatted and themed according to the specified renderer.
    /// </para>
    /// <para>
    /// If the <see cref="RichTextBlock"/> control is not on the UI thread, the log event is enqueued to be processed
    /// on the UI thread.
    /// </para>
    /// </remarks>
    public void Emit(LogEvent logEvent)
    {
        if (richTextBlock.DispatcherQueue.HasThreadAccess)
        {
            this.AppendLog(logEvent);
        }
        else
        {
            _ = richTextBlock.DispatcherQueue.TryEnqueue(() => this.AppendLog(logEvent));
        }
    }

    /// <summary>
    /// Appends the log event to the <see cref="RichTextBlock"/> control.
    /// </summary>
    /// <param name="logEvent">The log event to append.</param>
    /// <remarks>
    /// <para>
    /// This method formats the log event into a rich text paragraph and adds it to the <see cref="RichTextBlock"/> control.
    /// </para>
    /// </remarks>
    private void AppendLog(LogEvent logEvent)
    {
        var paragraph = new Paragraph();
        renderer.Render(logEvent, paragraph);
        richTextBlock.Blocks.Insert(0, paragraph);
    }
}
