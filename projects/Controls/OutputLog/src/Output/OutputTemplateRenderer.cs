// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Output;

using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Formatting.Display;
using Serilog.Parsing;

internal sealed class OutputTemplateRenderer : ILogEventRenderer
{
    private readonly TokenRenderer[] renderers;

    public OutputTemplateRenderer(Theme theme, string outputTemplate, IFormatProvider? formatProvider)
    {
        var template = new MessageTemplateParser().Parse(outputTemplate);
        var renderers_ = new List<TokenRenderer>();
        foreach (var token in template.Tokens)
        {
            if (token is TextToken tt)
            {
                renderers_.Add(new TextTokenRenderer(theme, tt.Text));
                continue;
            }

            var pt = (PropertyToken)token;

            switch (pt.PropertyName)
            {
                case OutputProperties.LevelPropertyName:
                    renderers_.Add(new LevelTokenRenderer(theme, pt));
                    break;

                case OutputProperties.NewLinePropertyName:
                    renderers_.Add(new NewLineTokenRenderer(pt.Alignment));
                    break;

                case OutputProperties.MessagePropertyName:
                    renderers_.Add(new MessageTemplateTokenRenderer(theme, pt, formatProvider));
                    break;

                case OutputProperties.TimestampPropertyName:
                    renderers_.Add(new TimestampTokenRenderer(theme, pt, formatProvider));
                    break;

                case "Properties":
                    renderers_.Add(new PropertiesTokenRenderer(theme, pt, template, formatProvider));
                    break;

                /*
                    case OutputProperties.ExceptionPropertyName:
                        renderersCollection.Add(new ExceptionTokenRenderer(theme));
                        break;
                */
                default:
                    renderers_.Add(new EventPropertyTokenRenderer(theme, pt, formatProvider));
                    break;
            }
        }

        this.renderers = [.. renderers_];
    }

    public void Render(LogEvent logEvent, Paragraph paragraph)
    {
        foreach (var renderer in this.renderers)
        {
            renderer.Render(logEvent, paragraph);
        }
    }
}
