// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Data;
using Serilog.Events;
using static DroidNet.Controls.OutputLog.Formatting.ThemedValueFormatter;

namespace DroidNet.Controls.OutputLog.Formatting;

/// <summary>
/// Provides base functionality for themed formatting of log event property values.
/// </summary>
/// <param name="theme">The theme to apply to the formatted output.</param>
/// <param name="formatProvider">
/// An optional format provider for value formatting. If <see langword="null"/>, uses the current culture.
/// </param>
/// <remarks>
/// <para>
/// This abstract class serves as the foundation for specialized formatters that apply
/// consistent theming to different output formats (display, JSON, etc.).
/// </para>
/// <para>
/// <strong>Core Features:</strong>
/// - Theme-based styling of output elements
/// - Culture-aware value formatting
/// - Support for nested property values
/// - Container-based output rendering.
/// </para>
/// <para>
/// Example implementation:
/// <code><![CDATA[
/// public class CustomFormatter : ThemedValueFormatter
/// {
///     public CustomFormatter(Theme theme, IFormatProvider? formatProvider)
///         : base(theme, formatProvider)
///     {
///     }
///
///     protected override VoidResult? VisitScalarValue(State state, ScalarValue scalar)
///     {
///         // Custom formatting implementation
///     }
/// }
/// ]]></code>
/// </para>
/// </remarks>
internal abstract class ThemedValueFormatter(Theme theme, IFormatProvider? formatProvider)
    : LogEventPropertyValueVisitor<State, VoidResult?>
{
    /// <summary>
    /// Gets the format provider used for value formatting.
    /// </summary>
    /// <value>
    /// The format provider specified in constructor, or <see langword="null"/> to use current culture.
    /// </value>
    protected IFormatProvider? FormatProvider { get; } = formatProvider;

    /// <summary>
    /// Renders a log event property value with theming applied.
    /// </summary>
    /// <param name="value">The value to render.</param>
    /// <param name="container">The container to render into.</param>
    /// <param name="format">Optional format string for value formatting.</param>
    /// <param name="literalTopLevel">
    /// When <see langword="true"/>, renders top-level values as literals without additional formatting.
    /// </param>
    /// <remarks>
    /// <para>
    /// The method initiates the visitor pattern for rendering different value types with appropriate theming.
    /// </para>
    /// </remarks>
    public void Render(LogEventPropertyValue value, dynamic container, string? format, bool literalTopLevel = false)
        => this.Visit(
            new State
            {
                Container = container,
                Format = format,
                IsTopLevel = literalTopLevel,
            },
            value);

    /// <summary>
    /// Adds a themed run to a paragraph with specified text and style.
    /// </summary>
    /// <param name="paragraph">The paragraph to add the run to.</param>
    /// <param name="text">The text content of the run.</param>
    /// <param name="style">The theme style to apply.</param>
    /// <remarks>
    /// <para>
    /// Creates a new run with the specified text and applies the theme style using a disposable context.
    /// </para>
    /// </remarks>
    protected void AddThemedRun(Paragraph paragraph, string text, ThemeStyle style)
    {
        using var styleContext = theme.Apply(paragraph, style);
        styleContext.Run.Text = text;
    }

    /// <summary>
    /// Adds a themed run to a span with specified text and style.
    /// </summary>
    /// <param name="span">The span to add the run to.</param>
    /// <param name="text">The text content of the run.</param>
    /// <param name="style">The theme style to apply.</param>
    /// <remarks>
    /// <para>
    /// Creates a new run within the span and applies the theme style using a disposable context.
    /// </para>
    /// </remarks>
    protected void AddThemedRun(Span span, string text, ThemeStyle style)
    {
        using var styleContext = theme.Apply(span, style);
        styleContext.Run.Text = text;
    }

    /// <summary>
    /// Represents the state for the current formatting operation.
    /// </summary>
    public sealed class State
    {
        /// <summary>
        /// Gets the container to render content into.
        /// </summary>
        public required dynamic Container { get; init; }

        /// <summary>
        /// Gets the format string for value formatting.
        /// </summary>
        public string? Format { get; init; }

        /// <summary>
        /// Gets a value indicating whether gets or sets whether the current value is at the top level.
        /// </summary>
        public required bool IsTopLevel { get; init; }
    }

    /// <summary>
    /// Represents a void result type for the visitor pattern.
    /// </summary>
    public abstract class VoidResult;
}
