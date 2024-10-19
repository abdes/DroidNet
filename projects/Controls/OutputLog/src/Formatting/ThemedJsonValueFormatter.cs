// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Formatting;

using System;
using System.Globalization;
using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Formatting.Json;

internal sealed class ThemedJsonValueFormatter(Theme theme, IFormatProvider? formatProvider)
    : ThemedValueFormatter(theme, formatProvider)
{
    private readonly ThemedDisplayValueFormatter displayFormatter = new(theme, formatProvider);

    protected override VoidResult? VisitScalarValue(State state, ScalarValue scalar)
    {
        // At the top level, for scalar values, use "display" rendering.
        if (state.IsTopLevel)
        {
            this.displayFormatter.FormatLiteralValue(scalar, state.Container, state.Format);
        }

        this.FormatLiteralValue(scalar, state.Container);

        return default;
    }

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
            this.Visit(
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

    protected override VoidResult? VisitStructureValue(State state, StructureValue structure)
    {
        StringWriter output;

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

            output = new StringWriter();
            JsonValueFormatter.WriteQuotedJsonString(SpecialCharsEscaping.Apply(property.Name), output);
            this.AddThemedRun(span, output.ToString(), ThemeStyle.Name);

            this.AddThemedRun(span, ": ", ThemeStyle.TertiaryText);

            this.Visit(
                new State
                {
                    Container = span,
                    Format = state.Format,
                    IsTopLevel = false,
                },
                property.Value);
        }

        if (structure.TypeTag is not null)
        {
            this.AddThemedRun(span, delim, ThemeStyle.TertiaryText);

            output = new StringWriter();
            JsonValueFormatter.WriteQuotedJsonString("$type", output);
            this.AddThemedRun(span, output.ToString(), ThemeStyle.Name);

            this.AddThemedRun(span, ": ", ThemeStyle.TertiaryText);

            output = new StringWriter();
            JsonValueFormatter.WriteQuotedJsonString(SpecialCharsEscaping.Apply(structure.TypeTag), output);
            this.AddThemedRun(span, output.ToString(), ThemeStyle.String);
        }

        this.AddThemedRun(span, "}", ThemeStyle.TertiaryText);

        return default;
    }

    protected override VoidResult? VisitDictionaryValue(State state, DictionaryValue dictionary)
    {
        StringWriter output;

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

            output = new StringWriter();
            JsonValueFormatter.WriteQuotedJsonString(
                SpecialCharsEscaping.Apply(element.Key.Value?.ToString() ?? "null"),
                output);
            this.AddThemedRun(span, output.ToString(), style);

            this.AddThemedRun(span, ": ", ThemeStyle.TertiaryText);

            this.Visit(
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

    private void FormatLiteralValue(ScalarValue scalar, dynamic paragraph)
    {
        var output = new StringWriter();

        switch (scalar.Value)
        {
            case null:
                this.AddThemedRun(paragraph, "null", ThemeStyle.Null);
                break;

            case string str:
                var escapedValue = SpecialCharsEscaping.Apply(str);
                JsonValueFormatter.WriteQuotedJsonString(escapedValue, output);
                this.AddThemedRun(paragraph, output.ToString(), ThemeStyle.String);
                break;

            case ValueType and (int or uint or long or ulong or decimal or byte or sbyte or short or ushort):
                var formattedValue = ((IFormattable)scalar.Value).ToString(format: null, CultureInfo.InvariantCulture);
                this.AddThemedRun(paragraph, formattedValue, ThemeStyle.Number);
                break;

            case double d:
                this.FormatDoubleValue(paragraph, d);
                break;

            case float f:
                this.FormatDoubleValue(paragraph, f);
                break;

            case bool b:
                output.Write(b ? "true" : "false");
                this.AddThemedRun(paragraph, output.ToString(), ThemeStyle.Boolean);
                break;

            case char ch:
                var charString = SpecialCharsEscaping.Apply(ch.ToString());
                JsonValueFormatter.WriteQuotedJsonString(charString, output);
                this.AddThemedRun(paragraph, output.ToString(), ThemeStyle.Scalar);
                break;

            case DateTime or DateTimeOffset:
                output.Write('\"');
                output.Write(((IFormattable)scalar.Value).ToString("O", CultureInfo.InvariantCulture));
                output.Write('\"');
                this.AddThemedRun(paragraph, output.ToString(), ThemeStyle.Scalar);
                break;

            default:
                JsonValueFormatter.WriteQuotedJsonString(
                    SpecialCharsEscaping.Apply(scalar.Value.ToString() ?? "null"),
                    output);
                this.AddThemedRun(paragraph, output.ToString(), ThemeStyle.Scalar);
                break;
        }
    }

    private void FormatDoubleValue(dynamic paragraph, double value)
    {
        var output = new StringWriter();
        if (double.IsNaN(value) || double.IsInfinity(value))
        {
            JsonValueFormatter.WriteQuotedJsonString(value.ToString(CultureInfo.InvariantCulture), output);
        }
        else
        {
            output.Write(value.ToString("R", CultureInfo.InvariantCulture));
        }

        this.AddThemedRun(paragraph, output.ToString(), ThemeStyle.Number);
    }
}
