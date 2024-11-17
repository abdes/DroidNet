// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Documents;
using Serilog.Events;

namespace DroidNet.Controls.OutputLog.Output;

/// <summary>
/// Provides an abstract base class for rendering log event tokens with themed styling.
/// </summary>
/// <remarks>
/// <para>
/// This class serves as the foundation for specialized token renderers that apply consistent theming
/// to different types of log event tokens (e.g., text, level, timestamp).
/// </para>
/// <para>
/// <strong>Implementation Notes:</strong>
/// Derived classes must implement the <see cref="Render"/> method to define how the token is rendered
/// into the specified paragraph. The method should handle theming and formatting as appropriate for the token type.
/// </para>
/// <para>
/// Example implementation:
/// <code><![CDATA[
/// internal sealed class TextTokenRenderer : TokenRenderer
/// {
///     private readonly Theme theme;
///     private readonly string text;
///
///     public TextTokenRenderer(Theme theme, string text)
///     {
///         this.theme = theme;
///         this.text = text;
///     }
///
///     public override void Render(LogEvent logEvent, Paragraph paragraph)
///     {
///         using var styleInfo = theme.Apply(paragraph, ThemeStyle.TertiaryText);
///         styleInfo.Run.Text = SpecialCharsEscaping.Apply(text);
///     }
/// }
/// ]]></code>
/// </para>
/// </remarks>
internal abstract class TokenRenderer
{
    /// <summary>
    /// Renders a log event token into the specified paragraph with themed styling.
    /// </summary>
    /// <param name="logEvent">The log event containing the token.</param>
    /// <param name="paragraph">The target paragraph for the rendered output.</param>
    /// <remarks>
    /// <para>
    /// This method must be implemented by derived classes to define how the token is rendered.
    /// The implementation should handle theming and formatting as appropriate for the token type.
    /// </para>
    /// </remarks>
    public abstract void Render(LogEvent logEvent, Paragraph paragraph);
}
