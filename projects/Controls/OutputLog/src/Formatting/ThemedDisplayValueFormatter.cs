// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Serilog.Events;
using Serilog.Formatting.Json;
using Span = Microsoft.UI.Xaml.Documents.Span;

namespace DroidNet.Controls.OutputLog.Formatting;

/// <summary>
/// Provides themed formatting capabilities for scalar values in log events, applying consistent styling
/// and proper escaping rules based on the value type.
/// </summary>
/// <param name="theme">The theme to apply to the formatted output.</param>
/// <param name="formatProvider">
/// An optional format provider for value formatting. If <see langword="null"/>, uses the current culture.
/// </param>
/// <remarks>
/// <para>
/// This formatter handles various scalar types with specialized formatting rules and theme styles:
/// </para>
/// <para>
/// <strong>Value Types and Their Formatting:</strong>
/// - <see langword="null"/> values render as "null" with Null style
/// - Strings receive proper escaping and optional quotation based on format
/// - Numeric types use the configured format provider
/// - Boolean values are formatted using invariant culture
/// - Characters are wrapped in single quotes with escape sequences.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var formatter = new ThemedDisplayValueFormatter(theme, CultureInfo.InvariantCulture);
/// var scalarValue = new ScalarValue("Hello\nWorld");
/// var (text, style) = formatter.FormatLiteralValue(scalarValue, format: null);
/// // Result: text = "\"Hello\nWorld\"", style = ThemeStyle.String
/// ]]></code>
/// </para>
/// </remarks>
internal sealed class ThemedDisplayValueFormatter(Theme theme, IFormatProvider? formatProvider)
    : ThemedValueFormatter(theme, formatProvider)
{
    /// <summary>
    /// Formats a scalar value with appropriate theming and escaping rules.
    /// </summary>
    /// <param name="scalar">The scalar value to format. Must not be <see langword="null"/>.</param>
    /// <param name="format">
    /// Optional format string that controls the formatting behavior.
    /// Use "l" for literal string formatting without quotes.
    /// </param>
    /// <returns>
    /// A tuple containing the formatted text and its corresponding theme style.
    /// </returns>
    /// <remarks>
    /// <para>
    /// The method applies specific formatting rules based on the scalar value type:
    /// </para>
    /// <para>
    /// <strong>String Formatting:</strong>
    /// When format is "l", strings are rendered without quotes.
    /// Otherwise, strings are JSON-escaped and wrapped in quotes.
    /// </para>
    /// <para>
    /// <strong>Numeric Types:</strong>
    /// All numeric types use the formatter's configured <see cref="IFormatProvider"/>.
    /// </para>
    /// <para>
    /// Example results:
    /// <code><![CDATA[
    /// FormatLiteralValue(new ScalarValue("test"), null)    -> ("\"test\"", ThemeStyle.String)
    /// FormatLiteralValue(new ScalarValue("test"), "l")     -> ("test", ThemeStyle.String)
    /// FormatLiteralValue(new ScalarValue(42), null)        -> ("42", ThemeStyle.Number)
    /// FormatLiteralValue(new ScalarValue(true), null)      -> ("true", ThemeStyle.Boolean)
    /// FormatLiteralValue(new ScalarValue('a'), null)       -> ("'a'", ThemeStyle.Scalar)
    /// ]]></code>
    /// </para>
    /// </remarks>
    internal (string text, ThemeStyle style) FormatLiteralValue(ScalarValue scalar, string? format)
    {
        using var output = new StringWriter();

        switch (scalar.Value)
        {
            case null:
                return ("null", ThemeStyle.Null);

            case string str:
                var formattedValue = SpecialCharsEscaping.Apply(str);

                if (!string.Equals(format, "l", StringComparison.Ordinal))
                {
                    JsonValueFormatter.WriteQuotedJsonString(formattedValue, output);
                    formattedValue = output.ToString();
                }

                return (formattedValue, ThemeStyle.String);

            case ValueType and (int or uint or long or ulong or decimal or byte or sbyte or short or ushort or float
                or double):
                scalar.Render(output, format, this.FormatProvider);
                return (output.ToString(), ThemeStyle.Number);

            case bool b:
                output.Write(b.ToString(CultureInfo.InvariantCulture));
                return (output.ToString(), ThemeStyle.Boolean);

            case char ch:
                output.Write('\'');
                output.Write(SpecialCharsEscaping.Apply(ch.ToString()));
                output.Write('\'');
                return (output.ToString(), ThemeStyle.Scalar);

            default:
                scalar.Render(output, format, this.FormatProvider);
                return (output.ToString(), ThemeStyle.Scalar);
        }
    }

    /// <summary>
    /// Visits a scalar value and applies themed formatting within the current container.
    /// </summary>
    /// <param name="state">The current formatting state containing the container and format settings.</param>
    /// <param name="scalar">The scalar value to format and render.</param>
    /// <returns>
    /// <see langword="null"/> as no additional processing is needed after rendering.
    /// </returns>
    /// <remarks>
    /// <para>
    /// This method delegates the actual formatting to <see cref="FormatLiteralValue"/> and then
    /// adds the formatted text to the container using the appropriate theme style.
    /// </para>
    /// </remarks>
    protected override VoidResult? VisitScalarValue(State state, ScalarValue scalar)
    {
        var (text, style) = this.FormatLiteralValue(scalar, state.Format);
        this.AddThemedRun(state.Container, text, style);

        return default;
    }

    /// <summary>
    /// Visits a dictionary value and renders it as a themed key-value collection.
    /// </summary>
    /// <param name="state">The current formatting state containing the container and format settings.</param>
    /// <param name="dictionary">The dictionary value to format and render.</param>
    /// <returns>
    /// <see langword="null"/> as no additional processing is needed after rendering.
    /// </returns>
    /// <remarks>
    /// <para>
    /// The dictionary is rendered in the format: <c>{ [key1]=value1, [key2]=value2, ... }</c>
    /// where each component receives appropriate theming:
    /// </para>
    /// <para>
    /// <strong>Component Styling:</strong>
    /// - Braces and brackets use TertiaryText style
    /// - Keys and values are recursively formatted
    /// - Delimiters use TertiaryText style.
    /// </para>
    /// <para>
    /// Example output:
    /// <code><![CDATA[
    /// { ["name"]="John", ["age"]=42 }
    /// ]]></code>
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

            this.AddThemedRun(span, "[", ThemeStyle.TertiaryText);
            _ = this.Visit(
                new State
                {
                    Container = span,
                    Format = state.Format,
                    IsTopLevel = false,
                },
                element.Key);
            this.AddThemedRun(span, "]=", ThemeStyle.TertiaryText);

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
    /// Visits a sequence value and renders it as a themed array-like collection.
    /// </summary>
    /// <param name="state">The current formatting state containing the container and format settings.</param>
    /// <param name="sequence">The sequence value to format and render.</param>
    /// <returns>
    /// <see langword="null"/> as no additional processing is needed after rendering.
    /// </returns>
    /// <remarks>
    /// <para>
    /// The sequence is rendered in the format: <c>[element1, element2, ...]</c>
    /// where each component receives appropriate theming:
    /// </para>
    /// <para>
    /// <strong>Component Styling:</strong>
    /// - Square brackets use TertiaryText style
    /// - Elements are recursively formatted
    /// - Comma delimiters use TertiaryText style.
    /// </para>
    /// <para>
    /// Example outputs:
    /// <code><![CDATA[
    /// [1, 2, 3]                     // Number sequence
    /// ["apple", "banana", "cherry"] // String sequence
    /// [true, false]                 // Boolean sequence
    /// ]]></code>
    /// </para>
    /// <para>
    /// <strong>Performance Note:</strong>
    /// The method intentionally uses a traditional for loop instead of foreach for optimal performance
    /// when dealing with large sequences.
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
    /// Visits a structure value and renders it as a themed object-like representation with properties.
    /// </summary>
    /// <param name="state">The current formatting state containing the container and format settings.</param>
    /// <param name="structure">The structure value to format and render.</param>
    /// <returns>
    /// <see langword="null"/> as no additional processing is needed after rendering.
    /// </returns>
    /// <remarks>
    /// <para>
    /// The structure is rendered in the format: <c>TypeTag {property1=value1, property2=value2, ...}</c>
    /// where each component receives appropriate theming:
    /// </para>
    /// <para>
    /// <strong>Component Styling:</strong>
    /// - TypeTag (if present) uses Name style
    /// - Braces and equals signs use TertiaryText style
    /// - Property names use Name style with special character escaping
    /// - Property values are recursively formatted
    /// - Delimiters use TertiaryText style.
    /// </para>
    /// <para>
    /// Example outputs:
    /// <code><![CDATA[
    /// Person {name="John", age=42}
    /// Exception {message="Error occurred", code=500}
    /// {x=10, y=20}  // Structure without TypeTag
    /// ]]></code>
    /// </para>
    /// <para>
    /// <strong>Performance Note:</strong>
    /// The method intentionally uses a traditional for loop instead of foreach for optimal performance
    /// when dealing with structures containing many properties.
    /// </para>
    /// </remarks>
    protected override VoidResult? VisitStructureValue(State state, StructureValue structure)
    {
        var span = new Span();
        state.Container.Inlines.Add(span);

        if (structure.TypeTag != null)
        {
            this.AddThemedRun(span, structure.TypeTag, ThemeStyle.Name);
            this.AddThemedRun(span, " ", ThemeStyle.Null);
        }

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

            this.AddThemedRun(span, SpecialCharsEscaping.Apply(property.Name), ThemeStyle.Name);
            this.AddThemedRun(span, "=", ThemeStyle.TertiaryText);

            _ = this.Visit(
                new State
                {
                    Container = span,
                    Format = state.Format,
                    IsTopLevel = false,
                },
                property.Value);
        }

        this.AddThemedRun(span, "}", ThemeStyle.TertiaryText);

        return default;
    }
}
