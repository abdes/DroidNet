// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;

namespace DroidNet.Controls.OutputLog.Output;

/// <summary>
/// Renders text tokens with themed styling in the output log.
/// </summary>
/// <param name="theme">The theme to apply to the rendered output.</param>
/// <param name="text">The text content to render.</param>
/// <remarks>
/// <para>
/// This renderer processes plain text tokens, converting them into themed text with appropriate styling.
/// It ensures that text tokens are rendered consistently and with the specified theme.
/// </para>
/// <para>
/// <strong>Styling:</strong>
/// Text tokens are styled using the <see cref="ThemeStyle.TertiaryText"/> style by default.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var renderer = new TextTokenRenderer(theme, "Sample text");
/// renderer.Render(logEvent, paragraph);
/// // Outputs: "Sample text" with appropriate styling
/// ]]></code>
/// </para>
/// </remarks>
internal sealed class TextTokenRenderer(Theme theme, string text) : TokenRenderer
{
    /// <summary>
    /// Renders a text token into the specified container with themed styling.
    /// </summary>
    /// <param name="logEvent">The log event containing the text token.</param>
    /// <param name="paragraph">The target paragraph for the rendered output.</param>
    /// <remarks>
    /// <para>
    /// The rendering process applies the following steps:
    /// </para>
    /// <para>
    /// 1. Applies special character escaping to the text
    /// 2. Creates a themed run with the escaped text
    /// 3. Adds the run to the paragraph.
    /// </para>
    /// </remarks>
    public override void Render(LogEvent logEvent, Paragraph paragraph)
    {
        using var styleInfo = theme.Apply(paragraph, ThemeStyle.TertiaryText);
        styleInfo.Run.Text = SpecialCharsEscaping.Apply(text);
    }
}
