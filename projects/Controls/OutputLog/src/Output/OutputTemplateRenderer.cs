// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Formatting.Display;
using Serilog.Parsing;

namespace DroidNet.Controls.OutputLog.Output;

/// <summary>
/// Renders log events based on a specified output template, applying themed styling to each token.
/// </summary>
/// <remarks>
/// <para>
/// This renderer processes Serilog output templates, converting them into themed text with support for
/// various tokens such as level, message, timestamp, and properties.
/// </para>
/// <para>
/// <strong>Supported Tokens:</strong>
/// - Level
/// - Message
/// - Timestamp
/// - Properties
/// - NewLine.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var renderer = new OutputTemplateRenderer(theme, "[{Timestamp:HH:mm:ss} {Level:u3}] {Message:lj} {Properties}{NewLine}{Exception}", formatProvider);
/// renderer.Render(logEvent, paragraph);
/// // Outputs: "[12:34:56 INF] Hello, World! {Property1=Value1, Property2=Value2}"
/// ]]></code>
/// </para>
/// </remarks>
internal sealed class OutputTemplateRenderer : ILogEventRenderer
{
    private readonly TokenRenderer[] renderers;

    /// <summary>
    /// Initializes a new instance of the <see cref="OutputTemplateRenderer"/> class.
    /// </summary>
    /// <param name="theme">The theme to apply to the rendered output.</param>
    /// <param name="outputTemplate">The template defining the structure and content of the log output.</param>
    /// <param name="formatProvider">The format provider for value formatting. Can be <see langword="null"/>.</param>
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

    /// <summary>
    /// Renders a log event into a rich text paragraph based on the output template.
    /// </summary>
    /// <param name="logEvent">The log event to render.</param>
    /// <param name="paragraph">The target paragraph for the rendered output.</param>
    /// <remarks>
    /// <para>
    /// The rendering process iterates through each token renderer, applying the appropriate
    /// formatting and theming to the log event properties.
    /// </para>
    /// </remarks>
    public void Render(LogEvent logEvent, Paragraph paragraph)
    {
        foreach (var renderer in this.renderers)
        {
            renderer.Render(logEvent, paragraph);
        }
    }
}
