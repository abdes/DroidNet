// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Formatting;

using System;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Data;
using Serilog.Events;
using static DroidNet.Controls.OutputLog.Formatting.ThemedValueFormatter;

internal abstract class ThemedValueFormatter(Theme theme, IFormatProvider? formatProvider)
    : LogEventPropertyValueVisitor<State, VoidResult?>
{
    protected IFormatProvider? FormatProvider { get; } = formatProvider;

    public void Render(LogEventPropertyValue value, dynamic container, string? format, bool literalTopLevel = false)
        => this.Visit(
            new State
            {
                Container = container,
                Format = format,
                IsTopLevel = literalTopLevel,
            },
            value);

    protected void AddThemedRun(Paragraph paragraph, string text, ThemeStyle style)
    {
        using var styleContext = theme.Apply(paragraph, style);
        styleContext.Run.Text = text;
    }

    protected void AddThemedRun(Span span, string text, ThemeStyle style)
    {
        using var styleContext = theme.Apply(span, style);
        styleContext.Run.Text = text;
    }

    public sealed class State
    {
        public required dynamic Container { get; init; }

        public string? Format { get; init; }

        public required bool IsTopLevel { get; init; }
    }

    public abstract class VoidResult;
}
