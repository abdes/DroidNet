// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputLog.Formatting;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Parsing;

namespace DroidNet.Controls.OutputLog.Output;

/// <summary>
/// Renders log event properties as themed text with support for JSON and display formatting.
/// </summary>
/// <remarks>
/// <para>
/// This renderer processes Serilog properties, converting them into themed text with support for
/// JSON and display formatting. It ensures that properties are rendered consistently and with appropriate styling.
/// </para>
/// <para>
/// <strong>Formatting Options:</strong>
/// - 'j' for JSON formatting.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var renderer = new PropertiesTokenRenderer(theme, token, template, formatProvider);
/// renderer.Render(logEvent, paragraph);
/// // Outputs: "{Property1=Value1, Property2=Value2}" with appropriate styling
/// ]]></code>
/// </para>
/// </remarks>
internal sealed class PropertiesTokenRenderer : TokenRenderer
{
    private readonly MessageTemplate template;
    private readonly ThemedValueFormatter valueFormatter;

    /// <summary>
    /// Initializes a new instance of the <see cref="PropertiesTokenRenderer"/> class.
    /// </summary>
    /// <param name="theme">The theme to apply to the rendered output.</param>
    /// <param name="token">The property token representing the properties.</param>
    /// <param name="template">The message template containing the properties.</param>
    /// <param name="formatProvider">The format provider for value formatting. Can be <see langword="null"/>.</param>
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

    /// <summary>
    /// Renders a log event's properties into a rich text paragraph with theming and formatting.
    /// </summary>
    /// <param name="logEvent">The log event containing the properties.</param>
    /// <param name="paragraph">The target paragraph for the rendered output.</param>
    /// <remarks>
    /// <para>
    /// The rendering process applies the following steps:
    /// </para>
    /// <para>
    /// 1. Filters out properties already included in the message template
    /// 2. Formats the remaining properties
    /// 3. Applies theming to the formatted properties
    /// 4. Adds the formatted properties to the paragraph.
    /// </para>
    /// </remarks>
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

    /// <summary>
    /// Checks if the message template contains a property with the specified name.
    /// </summary>
    /// <param name="template">The message template to check.</param>
    /// <param name="propertyName">The name of the property to look for.</param>
    /// <returns><see langword="true"/> if the template contains the property; otherwise, <see langword="false"/>.</returns>
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
