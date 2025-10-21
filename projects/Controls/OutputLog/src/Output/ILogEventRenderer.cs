// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Documents;
using Serilog.Events;

namespace DroidNet.Controls.OutputLog.Output;

/// <summary>
/// Defines a contract for rendering log events into rich text paragraphs.
/// </summary>
/// <remarks>
/// <para>
/// Implementers of this interface handle the conversion of Serilog <see cref="LogEvent"/>s
/// into rich text content with appropriate formatting and theming.
/// </para>
/// <para>
/// <strong>Implementation Notes:</strong>
/// - Must handle all log event properties and levels
/// - Should apply consistent formatting across log entries
/// - Must be thread-safe for concurrent logging.
/// </para>
/// <para>
/// Example implementation:
/// <code><![CDATA[
/// public class SimpleLogEventRenderer : ILogEventRenderer
/// {
///     public void Render(LogEvent logEvent, Paragraph paragraph)
///     {
///         var run = new Run { Text = logEvent.RenderMessage() };
///         paragraph.Inlines.Add(run);
///     }
/// }
/// ]]></code>
/// </para>
/// </remarks>
public interface ILogEventRenderer
{
    /// <summary>
    /// Renders a log event into a rich text paragraph.
    /// </summary>
    /// <param name="logEvent">The log event to render.</param>
    /// <param name="paragraph">The paragraph to render the log event into.</param>
    /// <remarks>
    /// <para>
    /// Implementations should:
    /// - Format the message and properties
    /// - Apply appropriate styling/theming
    /// - Handle exceptions if present
    /// - Ensure proper escaping of content.
    /// </para>
    /// </remarks>
    public void Render(LogEvent logEvent, Paragraph paragraph);
}
