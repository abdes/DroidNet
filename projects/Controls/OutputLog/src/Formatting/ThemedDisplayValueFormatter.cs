// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Formatting;

using System;
using System.Globalization;
using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Serilog.Events;
using Serilog.Formatting.Json;
using Span = Microsoft.UI.Xaml.Documents.Span;

internal sealed class ThemedDisplayValueFormatter(Theme theme, IFormatProvider? formatProvider)
    : ThemedValueFormatter(theme, formatProvider)
{
    internal (string text, ThemeStyle style) FormatLiteralValue(ScalarValue scalar, string? format)
    {
        var output = new StringWriter();

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

    protected override VoidResult? VisitScalarValue(State state, ScalarValue scalar)
    {
        var (text, style) = this.FormatLiteralValue(scalar, state.Format);
        this.AddThemedRun(state.Container, text, style);

        return default;
    }

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
            this.Visit(
                new State
                {
                    Container = span,
                    Format = state.Format,
                    IsTopLevel = false,
                },
                element.Key);
            this.AddThemedRun(span, "]=", ThemeStyle.TertiaryText);

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

            this.Visit(
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
