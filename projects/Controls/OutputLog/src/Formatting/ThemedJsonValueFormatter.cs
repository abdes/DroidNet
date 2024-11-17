// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Formatting.Json;

namespace DroidNet.Controls.OutputLog.Formatting;

/// <summary>
/// Provides JSON-specific themed formatting for log event values, ensuring proper JSON escaping
/// and consistent styling for JSON structure elements.
/// </summary>
/// <param name="theme">The theme to apply to the formatted output.</param>
/// <param name="formatProvider">
/// An optional format provider for value formatting. If <see langword="null"/>, uses the current culture.
/// </param>
/// <remarks>
/// <para>
/// This formatter specializes in producing JSON-like output with appropriate theming:
/// </para>
/// <para>
/// <strong>Formatting Features:</strong>
/// - Proper JSON string escaping for all string values
/// - Consistent JSON structure indicators (braces, brackets, quotes)
/// - Numeric values without quotes
/// - Boolean values in lowercase
/// - <see langword="null"/> as unquoted "null".
/// </para>
/// <para>
/// Example output:
/// <code><![CDATA[
/// {
///   "person": {
///     "name": "John Doe",
///     "age": 42,
///     "isActive": true,
///     "tags": ["employee", "manager"],
///     "address": null
///   }
/// }
/// ]]></code>
/// </para>
/// </remarks>
internal sealed class ThemedJsonValueFormatter(Theme theme, IFormatProvider? formatProvider)
    : ThemedValueFormatter(theme, formatProvider)
{
    private readonly ThemedDisplayValueFormatter displayFormatter = new(theme, formatProvider);

    /// <summary>
    /// Visits a scalar value and applies JSON-specific formatting within the current container.
    /// </summary>
    /// <param name="state">The current formatting state containing the container and format settings.</param>
    /// <param name="scalar">The scalar value to format according to JSON rules.</param>
    /// <returns>
    /// <see langword="null"/> as no additional processing is needed after rendering.
    /// </returns>
    /// <remarks>
    /// <para>
    /// The method handles formatting differently based on context:
    /// </para>
    /// <para>
    /// <strong>Top-level Values:</strong>
    /// Uses the display formatter for more human-readable output while maintaining JSON compatibility.
    /// This allows for optional formatting rules specified in the state.
    /// </para>
    /// <para>
    /// <strong>Nested Values:</strong>
    /// Uses strict JSON formatting rules to ensure valid JSON structure regardless of format settings.
    /// </para>
    /// <para>
    /// Example JSON output:
    /// <code><![CDATA[
    /// // Top-level:
    /// 42
    /// "Hello, World"
    /// true
    ///
    /// // Nested:
    /// {"value": 42}
    /// {"message": "Hello, World"}
    /// {"flag": true}
    /// ]]></code>
    /// </para>
    /// </remarks>
    protected override VoidResult? VisitScalarValue(State state, ScalarValue scalar)
    {
        // At the top level, for scalar values, use "display" rendering.
        var (text, style) = state.IsTopLevel
            ? this.displayFormatter.FormatLiteralValue(scalar, state.Format)
            : FormatLiteralValue(scalar, state.Format);

        this.AddThemedRun(state.Container, text, style);

        return default;
    }

    /// <summary>
    /// Visits a sequence value and renders it as a JSON array with themed formatting.
    /// </summary>
    /// <param name="state">The current formatting state containing the container and format settings.</param>
    /// <param name="sequence">The sequence value to format as a JSON array.</param>
    /// <returns>
    /// <see langword="null"/> as no additional processing is needed after rendering.
    /// </returns>
    /// <remarks>
    /// <para>
    /// Renders sequences as JSON arrays with proper theming for structure elements:
    /// </para>
    /// <para>
    /// <strong>JSON Array Structure:</strong>
    /// - Square brackets use TertiaryText style
    /// - Elements are comma-separated with TertiaryText style delimiters
    /// - Each element is recursively formatted according to JSON rules.
    /// </para>
    /// <para>
    /// Example outputs:
    /// <code><![CDATA[
    /// []                              // Empty array
    /// [1, 2, 3]                      // Numbers
    /// ["red", "green", "blue"]       // Strings
    /// [true, false]                  // Booleans
    /// [{"id": 1}, {"id": 2}]        // Objects
    /// [null, 42, "mixed", true]      // Mixed types
    /// ]]></code>
    /// </para>
    /// <para>
    /// <strong>Performance Note:</strong>
    /// Uses index-based iteration for optimal performance with large sequences.
    /// </para>
    /// </remarks>
    protected override VoidResult? VisitSequenceValue(State state, SequenceValue sequence)
    {
        var span = new Span();
        state.Container.Inlines.Add(span);

        this.AddThemedRun(span, "[", ThemeStyle.TertiaryText);

        var delim = string.Empty;

        // ReSharper disable once ForCanBeConvertedToForeach
        for (var index = 0; index < sequence.Elements.Count; ++index)
        {
            if (delim.Length != 0)
            {
                this.AddThemedRun(span, delim, ThemeStyle.TertiaryText);
            }

            delim = ", ";
            _ = this.Visit(
                new State
                {
                    Container = span,
                    Format = state.Format,
                    IsTopLevel = false,
                },
                sequence.Elements[index]);
        }

        this.AddThemedRun(span, "]", ThemeStyle.TertiaryText);

        return default;
    }

    /// <summary>
    /// Visits a structure value and renders it as a themed JSON object.
    /// </summary>
    /// <param name="state">The current formatting state containing the container and format settings.</param>
    /// <param name="structure">The structure value to format as a JSON object.</param>
    /// <returns>
    /// <see langword="null"/> as no additional processing is needed after rendering.
    /// </returns>
    /// <remarks>
    /// <para>
    /// Renders structures as JSON objects with themed components:
    /// </para>
    /// <para>
    /// <strong>JSON Object Structure:</strong>
    /// - Optional TypeTag rendered in Name style with space separator
    /// - Curly braces use TertiaryText style
    /// - Property names are escaped, quoted, and rendered in Name style
    /// - Property-value separator (":") uses TertiaryText style
    /// - Commas between properties use TertiaryText style.
    /// </para>
    /// <para>
    /// Example outputs:
    /// <code><![CDATA[
    /// {}                                  // Empty object
    /// {"name": "value"}                  // Simple property
    /// Person {"name": "John", "age": 42} // With TypeTag
    /// {
    ///   "nested": {
    ///     "array": [1, 2],
    ///     "null": null
    ///   }
    /// }                                  // Complex structure
    /// ]]></code>
    /// </para>
    /// <para>
    /// <strong>Performance Note:</strong>
    /// Uses index-based property iteration for optimal performance with large objects.
    /// </para>
    /// </remarks>
    protected override VoidResult? VisitStructureValue(State state, StructureValue structure)
    {
        var span = new Span();
        state.Container.Inlines.Add(span);

        this.AddThemedRun(span, "{", ThemeStyle.TertiaryText);

        var delim = string.Empty;

        // ReSharper disable once ForCanBeConvertedToForeach
        for (var index = 0; index < structure.Properties.Count; ++index)
        {
            if (delim.Length != 0)
            {
                this.AddThemedRun(span, delim, ThemeStyle.TertiaryText);
            }

            delim = ", ";

            var property = structure.Properties[index];

            this.AddThemedRun(span, delim, ThemeStyle.TertiaryText);

            using (var output = new StringWriter())
            {
                JsonValueFormatter.WriteQuotedJsonString(SpecialCharsEscaping.Apply(property.Name), output);
                this.AddThemedRun(span, output.ToString(), ThemeStyle.Name);
            }

            this.AddThemedRun(span, ": ", ThemeStyle.TertiaryText);

            _ = this.Visit(new State { Container = span, Format = state.Format, IsTopLevel = false, }, property.Value);
        }

        if (structure.TypeTag is not null)
        {
            this.AddThemedRun(span, delim, ThemeStyle.TertiaryText);

            using (var output = new StringWriter())
            {
                JsonValueFormatter.WriteQuotedJsonString("$type", output);
                this.AddThemedRun(span, output.ToString(), ThemeStyle.Name);
            }

            this.AddThemedRun(span, ": ", ThemeStyle.TertiaryText);

            using (var output = new StringWriter())
            {
                JsonValueFormatter.WriteQuotedJsonString(SpecialCharsEscaping.Apply(structure.TypeTag), output);
                this.AddThemedRun(span, output.ToString(), ThemeStyle.String);
            }
        }

        this.AddThemedRun(span, "}", ThemeStyle.TertiaryText);

        return default;
    }

    /// <summary>
    /// Visits a dictionary value and renders it as a themed JSON object with key-value pairs.
    /// </summary>
    /// <param name="state">The current formatting state containing the container and format settings.</param>
    /// <param name="dictionary">The dictionary value to format as a JSON object.</param>
    /// <returns>
    /// <see langword="null"/> as no additional processing is needed after rendering.
    /// </returns>
    /// <remarks>
    /// <para>
    /// Renders dictionaries as JSON objects with themed components following strict JSON formatting:
    /// </para>
    /// <para>
    /// <strong>JSON Dictionary Structure:</strong>
    /// - Curly braces delimit the object using TertiaryText style
    /// - Keys are formatted as JSON values (with full escaping) in Name style
    /// - Key-value separator (":") uses TertiaryText style
    /// - Values are recursively formatted according to JSON rules
    /// - Comma separators use TertiaryText style.
    /// </para>
    /// <para>
    /// Example outputs:
    /// <code><![CDATA[
    /// {}                                    // Empty dictionary
    /// {"key": "value"}                     // Simple key-value
    /// {42: true}                          // Numeric key
    /// {
    ///   "nested": {
    ///     "key": ["array", "values"],
    ///     "null": null
    ///   }
    /// }                                    // Complex structure
    /// ]]></code>
    /// </para>
    /// <para>
    /// <strong>Note:</strong>
    /// All dictionary keys are formatted as JSON values, ensuring valid JSON output
    /// regardless of the key's type (string, number, etc).
    /// </para>
    /// </remarks>
    protected override VoidResult? VisitDictionaryValue(State state, DictionaryValue dictionary)
    {
        var span = new Span();
        state.Container.Inlines.Add(span);

        this.AddThemedRun(span, "{", ThemeStyle.TertiaryText);

        var delim = string.Empty;
        foreach (var element in dictionary.Elements)
        {
            if (delim.Length != 0)
            {
                this.AddThemedRun(span, delim, ThemeStyle.TertiaryText);
            }

            delim = ", ";

            var style = element.Key.Value switch
            {
                null => ThemeStyle.Null,
                string => ThemeStyle.String,
                _ => ThemeStyle.Scalar,
            };

            using var output = new StringWriter();
            JsonValueFormatter.WriteQuotedJsonString(
                SpecialCharsEscaping.Apply(element.Key.Value?.ToString() ?? "null"),
                output);
            this.AddThemedRun(span, output.ToString(), style);

            this.AddThemedRun(span, ": ", ThemeStyle.TertiaryText);

            _ = this.Visit(
                new State
                {
                    Container = span,
                    Format = state.Format,
                    IsTopLevel = false,
                },
                element.Value);
        }

        this.AddThemedRun(span, "}", ThemeStyle.TertiaryText);

        return default;
    }

    /// <summary>
    /// Formats a double value according to JSON numeric formatting rules with special handling for non-finite values.
    /// </summary>
    /// <param name="value">The double value to format.</param>
    /// <returns>
    /// A tuple containing the JSON-formatted text and ThemeStyle.Number style.
    /// </returns>
    /// <remarks>
    /// <para>
    /// <strong>Formatting Rules:</strong>
    /// - Regular numbers use "R" format for round-trip precision
    /// - <see cref="double.NaN"/> is formatted as <c>"NaN"</c>
    /// - <see cref="double.PositiveInfinity"/> is formatted as <c>"Infinity"</c>
    /// - <see cref="double.NegativeInfinity"/> is formatted as <c>"-Infinity"</c>.
    /// </para>
    /// <para>
    /// Example outputs:
    /// <code><![CDATA[
    /// 42.0                  // Integer value
    /// 3.14159265359        // Regular double
    /// "NaN"                // Not-a-Number
    /// "Infinity"           // Positive infinity
    /// "-Infinity"          // Negative infinity
    /// 1.23E-4              // Scientific notation
    /// ]]></code>
    /// </para>
    /// <para>
    /// <strong>Note:</strong>
    /// Non-finite values are quoted as strings to ensure valid JSON output,
    /// as these values are not part of the JSON number specification.
    /// </para>
    /// </remarks>
    private static (string text, ThemeStyle style) FormatDoubleValue(double value)
    {
        using var output = new StringWriter();
        if (double.IsNaN(value) || double.IsInfinity(value))
        {
            JsonValueFormatter.WriteQuotedJsonString(value.ToString(CultureInfo.InvariantCulture), output);
        }
        else
        {
            output.Write(value.ToString("R", CultureInfo.InvariantCulture));
        }

        return (output.ToString(), ThemeStyle.Number);
    }

    /// <summary>
    /// Formats a scalar value according to JSON formatting rules with appropriate theming.
    /// </summary>
    /// <param name="scalar">The scalar value to format as JSON.</param>
    /// <param name="format">The format string (unused in JSON formatting).</param>
    /// <returns>
    /// A tuple containing the JSON-formatted text and its corresponding theme style.
    /// </returns>
    /// <remarks>
    /// <para>
    /// <strong>JSON Formatting Rules:</strong>
    /// - <see langword="null"/> values render as unquoted "null"
    /// - Strings are JSON-escaped and wrapped in double quotes
    /// - Numbers use invariant culture formatting
    /// - Floating point values use "R" format for round-trippable precision
    /// - Special numeric values (NaN, Infinity) are quoted as strings
    /// - Booleans render as unquoted "true"/"false"
    /// - DateTime/DateTimeOffset use ISO 8601 format with quotes
    /// - All other types are converted to strings, escaped, and quoted.
    /// </para>
    /// <para>
    /// Example outputs:
    /// <code><![CDATA[
    /// null                    // null value
    /// "Hello \"world\""       // string with escaping
    /// 42                     // integer
    /// 3.14159265359         // double
    /// "NaN"                 // special numeric value
    /// true                  // boolean
    /// "2024-03-14T12:00:00Z" // datetime
    /// ]]></code>
    /// </para>
    /// </remarks>
    private static (string text, ThemeStyle style) FormatLiteralValue(ScalarValue scalar, string? format)
    {
        _ = format; // unused

        using var output = new StringWriter();

        switch (scalar.Value)
        {
            case null:
                return ("null", ThemeStyle.Null);

            case string str:
                var escapedValue = SpecialCharsEscaping.Apply(str);
                JsonValueFormatter.WriteQuotedJsonString(escapedValue, output);
                return (output.ToString(), ThemeStyle.String);

            case ValueType and (int or uint or long or ulong or decimal or byte or sbyte or short or ushort):
                var formattedValue = ((IFormattable)scalar.Value).ToString(format: null, CultureInfo.InvariantCulture);
                return (formattedValue, ThemeStyle.Number);

            case double d:
                return FormatDoubleValue(d);

            case float f:
                return FormatDoubleValue(f);

            case bool b:
                output.Write(b ? "true" : "false");
                return (output.ToString(), ThemeStyle.Boolean);

            case char ch:
                var charString = SpecialCharsEscaping.Apply(ch.ToString());
                JsonValueFormatter.WriteQuotedJsonString(charString, output);
                return (output.ToString(), ThemeStyle.Scalar);

            case DateTime or DateTimeOffset:
                output.Write('\"');
                output.Write(((IFormattable)scalar.Value).ToString("O", CultureInfo.InvariantCulture));
                output.Write('\"');
                return (output.ToString(), ThemeStyle.Scalar);

            default:
                JsonValueFormatter.WriteQuotedJsonString(
                    SpecialCharsEscaping.Apply(scalar.Value.ToString() ?? "null"),
                    output);
                return (output.ToString(), ThemeStyle.Scalar);
        }
    }
}
