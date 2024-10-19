// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Output;

using System;
using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Parsing;

internal sealed class EventPropertyTokenRenderer(Theme theme, PropertyToken token, IFormatProvider? formatProvider)
    : TokenRenderer
{
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

        var output = new StringWriter();

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
