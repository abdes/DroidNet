// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Controls.OutputLog.Formatting;
using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Parsing;

namespace DroidNet.Controls.OutputLog.Output;

/// <summary>
/// Renders message template tokens with themed styling and optional formatting.
/// </summary>
/// <remarks>
/// <para>
/// This renderer processes Serilog message templates, converting them into themed text with support for
/// literal and JSON formatting.
/// </para>
/// <para>
/// <strong>Formatting Options:</strong>
/// - 'l' for literal formatting
/// - 'j' for JSON formatting.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var renderer = new MessageTemplateTokenRenderer(theme, token, formatProvider);
/// renderer.Render(logEvent, paragraph);
/// // Outputs: "Hello, World!" with appropriate styling
/// ]]></code>
/// </para>
/// </remarks>
internal sealed class MessageTemplateTokenRenderer : TokenRenderer
{
    private readonly Theme theme;
    private readonly bool isLiteral;
    private readonly ThemedValueFormatter valueFormatter;

    /// <summary>
    /// Initializes a new instance of the <see cref="MessageTemplateTokenRenderer"/> class.
    /// </summary>
    /// <param name="theme">The theme to apply to the rendered output.</param>
    /// <param name="token">The property token representing the message template.</param>
    /// <param name="formatProvider">The format provider for value formatting. Can be <see langword="null"/>.</param>
    public MessageTemplateTokenRenderer(Theme theme, PropertyToken token, IFormatProvider? formatProvider)
    {
        this.theme = theme;

        this.isLiteral = false;
        var isJson = false;

        if (token.Format is not null)
        {
            foreach (var format in token.Format)
            {
                switch (format)
                {
                    case 'l':
                        this.isLiteral = true;
                        break;
                    case 'j':
                        isJson = true;
                        break;

                    default:
                        // Ignore
                        break;
                }
            }
        }

        this.valueFormatter = isJson
            ? new ThemedJsonValueFormatter(theme, formatProvider)
            : new ThemedDisplayValueFormatter(theme, formatProvider);
    }

    /// <summary>
    /// Renders a message template token into the specified container with theming and formatting.
    /// </summary>
    /// <param name="logEvent">The log event containing the message template.</param>
    /// <param name="paragraph">The target paragraph for the rendered output.</param>
    /// <remarks>
    /// <para>
    /// The rendering process applies the following steps:
    /// </para>
    /// <para>
    /// 1. Iterates through the message template tokens
    /// 2. Renders text tokens with appropriate styling
    /// 3. Renders property tokens with formatting and theming
    /// 4. Adds the rendered content to the paragraph.
    /// </para>
    /// </remarks>
    public override void Render(LogEvent logEvent, Paragraph paragraph)
    {
        var span = new Span();
        paragraph.Inlines.Add(span);

        foreach (var templateToken in logEvent.MessageTemplate.Tokens)
        {
            switch (templateToken)
            {
                case TextToken tt:
                    this.RenderTextToken(tt, span);
                    break;

                case PropertyToken pt:
                    this.RenderPropertyToken(pt, logEvent.Properties, span);
                    break;

                default:
                    Debug.Fail($"Unexpected MessageTemplateToken type: `{templateToken.GetType().FullName}`");
                    break;
            }
        }
    }

    /// <summary>
    /// Renders a text token with themed styling.
    /// </summary>
    /// <param name="tt">The text token to render.</param>
    /// <param name="span">The span to add the rendered text to.</param>
    private void RenderTextToken(TextToken tt, Span span)
    {
        using var styleContext = this.theme.Apply(span, ThemeStyle.Text);
        styleContext.Run.Text = SpecialCharsEscaping.Apply(tt.Text);
    }

    /// <summary>
    /// Renders a property token with formatting and theming.
    /// </summary>
    /// <param name="pt">The property token to render.</param>
    /// <param name="properties">The properties of the log event.</param>
    /// <param name="span">The span to add the rendered property to.</param>
    private void RenderPropertyToken(
        PropertyToken pt,
        IReadOnlyDictionary<string, LogEventPropertyValue> properties,
        Span span)
    {
        if (!properties.TryGetValue(pt.PropertyName, out var propertyValue))
        {
            using var styleContext = this.theme.Apply(span, ThemeStyle.Invalid);
            styleContext.Run.Text = SpecialCharsEscaping.Apply(pt.ToString());

            return;
        }

        this.RenderValue(propertyValue, pt.Format, span);
    }

    /// <summary>
    /// Renders a property value with the specified format and theming.
    /// </summary>
    /// <param name="propertyValue">The property value to render.</param>
    /// <param name="format">The format string to apply.</param>
    /// <param name="span">The span to add the rendered value to.</param>
    private void RenderValue(LogEventPropertyValue propertyValue, string? format, Span span)
        => this.valueFormatter.Render(propertyValue, span, format, this.isLiteral);
}
