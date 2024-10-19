// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Output;

using System;
using System.Diagnostics;
using DroidNet.Controls.OutputLog.Formatting;
using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Parsing;

internal sealed class MessageTemplateTokenRenderer : TokenRenderer
{
    private readonly Theme theme;
    private readonly bool isLiteral;
    private readonly ThemedValueFormatter valueFormatter;

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

    private void RenderTextToken(TextToken tt, Span span)
    {
        using var styleContext = this.theme.Apply(span, ThemeStyle.Text);
        styleContext.Run.Text = SpecialCharsEscaping.Apply(tt.Text);
    }

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

    private void RenderValue(LogEventPropertyValue propertyValue, string? format, Span span)
        => this.valueFormatter.Render(propertyValue, span, format, this.isLiteral);
}
