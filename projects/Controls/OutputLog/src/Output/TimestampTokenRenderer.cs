// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Parsing;

namespace DroidNet.Controls.OutputLog.Output;

/// <summary>
/// Renders timestamp tokens with themed styling and optional formatting in the output log.
/// </summary>
/// <param name="theme">The theme to apply to the rendered output.</param>
/// <param name="token">The property token representing the timestamp.</param>
/// <param name="formatProvider">The format provider for value formatting. Can be <see langword="null"/>.</param>
/// <remarks>
/// <para>
/// This renderer processes timestamp tokens, converting them into themed text with appropriate styling and formatting.
/// It ensures that timestamps are rendered consistently and with the specified theme.
/// </para>
/// <para>
/// <strong>Formatting Options:</strong>
/// Timestamps can be formatted using standard .NET date and time format strings.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var renderer = new TimestampTokenRenderer(theme, token, formatProvider);
/// renderer.Render(logEvent, paragraph);
/// // Outputs: "2023-10-05T14:48:00.0000000Z" with appropriate styling
/// ]]></code>
/// </para>
/// </remarks>
internal sealed class TimestampTokenRenderer(Theme theme, PropertyToken token, IFormatProvider? formatProvider)
    : TokenRenderer
{
    /// <summary>
    /// Renders a timestamp token into the specified container with themed styling and formatting.
    /// </summary>
    /// <param name="logEvent">The log event containing the timestamp token.</param>
    /// <param name="paragraph">The target paragraph for the rendered output.</param>
    /// <remarks>
    /// <para>
    /// The rendering process applies the following steps:
    /// </para>
    /// <para>
    /// 1. Converts the timestamp to a string using the specified format
    /// 2. Applies padding if alignment is specified
    /// 3. Creates a themed run with the formatted timestamp
    /// 4. Adds the run to the paragraph.
    /// </para>
    /// </remarks>
    public override void Render(LogEvent logEvent, Paragraph paragraph)
    {
        // We need access to ScalarValue.Render() to avoid this alloc; just ensures
        // that custom format providers are supported properly.
        var sv = new ScalarValue(logEvent.Timestamp);

        using var styleInfo = theme.Apply(paragraph, ThemeStyle.SecondaryText);

        using var output = new StringWriter();
        sv.Render(output, token.Format, formatProvider);

        styleInfo.Run.Text = token.Alignment is null
            ? output.ToString()
            : PaddingTransform.Apply(output.ToString(), token.Alignment);
    }
}
