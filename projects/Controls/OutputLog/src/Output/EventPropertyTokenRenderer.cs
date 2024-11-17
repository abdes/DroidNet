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
/// Renders event properties as themed text with support for alignment and formatting.
/// </summary>
/// <remarks>
/// <para>
/// This renderer handles log event properties with support for:
/// </para>
/// <para>
/// <strong>Features:</strong>
/// - Property value formatting with theming
/// - Text alignment (left/right)
/// - Custom format strings
/// - Null value handling.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var renderer = new EventPropertyTokenRenderer(
///     valueFormatter: new ThemedDisplayValueFormatter(theme),
///     alignmentWidth: 10,
///     format: "N2"
/// );
/// renderer.Render(property, paragraph);  // Renders: "    123.45"
/// ]]></code>
/// </para>
/// </remarks>
internal sealed class EventPropertyTokenRenderer(Theme theme, PropertyToken token, IFormatProvider? formatProvider)
    : TokenRenderer
{
    /// <summary>
    /// Renders a property token into the specified container with theming and alignment.
    /// </summary>
    /// <param name="logEvent">The log event containing the property.</param>
    /// <param name="paragraph">The paragraph to which the rendered output will be added.</param>
    /// <remarks>
    /// <para>
    /// The rendering process:
    /// 1. Retrieves property value from the log event
    /// 2. Applies formatting and theming
    /// 3. Handles alignment if specified
    /// 4. Renders the final output.
    /// </para>
    /// </remarks>
    public override void Render(LogEvent logEvent, Paragraph paragraph)
    {
        // If a property is missing, don't render anything (message templates render the raw token here).
        if (!logEvent.Properties.TryGetValue(token.PropertyName, out var propertyValue))
        {
            var run = new Run();
            paragraph.Inlines.Add(run);
            run.Text = PaddingTransform.Apply(string.Empty, token.Alignment);
            return;
        }

        using var styleContext = theme.Apply(paragraph, ThemeStyle.TertiaryText);

        using var output = new StringWriter();

        // If the value is a scalar string, support some additional formats: 'u' for uppercase
        // and 'w' for lowercase.
        if (propertyValue is ScalarValue { Value: string literalString })
        {
            var cased = CasingTransform.Apply(literalString, token.Format);
            output.Write(cased);
        }
        else
        {
            propertyValue.Render(output, token.Format, formatProvider);
        }

        if (token.Alignment.HasValue)
        {
            var str = output.ToString();
            styleContext.Run.Text = PaddingTransform.Apply(str, token.Alignment);
        }

        styleContext.Run.Text = output.ToString();
    }
}
