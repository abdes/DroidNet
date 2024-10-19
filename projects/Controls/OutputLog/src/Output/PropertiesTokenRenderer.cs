// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Output;

using System;
using DroidNet.Controls.Demo.Logging.Formatting;
using DroidNet.Controls.OutputLog.Formatting;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Parsing;

internal sealed class PropertiesTokenRenderer : TokenRenderer
{
    private readonly MessageTemplate template;
    private readonly ThemedValueFormatter valueFormatter;

    public PropertiesTokenRenderer(
        Theme theme,
        PropertyToken token,
        MessageTemplate template,
        IFormatProvider? formatProvider)
    {
        this.template = template;

        var isJson = false;
        if (token.Format != null)
        {
            // ReSharper disable once ForCanBeConvertedToForeach
            for (var i = 0; i < token.Format.Length; ++i)
            {
                if (token.Format[i] == 'j')
                {
                    isJson = true;
                }
            }
        }

        this.valueFormatter = isJson
            ? new ThemedJsonValueFormatter(theme, formatProvider)
            : new ThemedDisplayValueFormatter(theme, formatProvider);
    }

    public override void Render(LogEvent logEvent, Paragraph paragraph)
    {
        // Avoid repeating properties that are already part of the message template
        var included = logEvent.Properties
            .Where(
                p => !TemplateContainsPropertyName(logEvent.MessageTemplate, p.Key) &&
                     !TemplateContainsPropertyName(this.template, p.Key))
            .Select(p => new LogEventProperty(p.Key, p.Value));

        var value = new StructureValue(included);
        this.valueFormatter.Render(value, paragraph, format: null);
    }

    private static bool TemplateContainsPropertyName(MessageTemplate template, string propertyName)
    {
        foreach (var templateToken in template.Tokens)
        {
            if (templateToken is PropertyToken namedProperty &&
                string.Equals(namedProperty.PropertyName, propertyName, StringComparison.Ordinal))
            {
                return true;
            }
        }

        return false;
    }
}
