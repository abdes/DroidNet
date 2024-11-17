// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputLog.Rendering;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Parsing;

namespace DroidNet.Controls.OutputLog.Output;

/// <summary>
/// Renders new line tokens with optional alignment in the output log.
/// </summary>
/// <param name="alignment">
/// Optional alignment for the new line text. When specified, the new line text is padded
/// according to the alignment settings. Use <see langword="null"/> for no alignment.
/// </param>
/// <remarks>
/// <para>
/// This renderer handles the insertion of new lines in the output log, with support for
/// optional text alignment. It ensures that new lines are properly formatted and aligned
/// according to the specified settings.
/// </para>
/// <para>
/// <strong>Alignment Options:</strong>
/// Positive values for right alignment, negative values for left alignment.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var renderer = new NewLineTokenRenderer(alignment: 10);
/// renderer.Render(logEvent, paragraph);
/// // Outputs: "\n" with padding applied
/// ]]></code>
/// </para>
/// </remarks>
internal sealed class NewLineTokenRenderer(Alignment? alignment) : TokenRenderer
{
    /// <summary>
    /// Renders a new line token into the specified container with optional alignment.
    /// </summary>
    /// <param name="logEvent">The log event containing the new line token.</param>
    /// <param name="paragraph">The target paragraph for the rendered output.</param>
    /// <remarks>
    /// <para>
    /// The rendering process applies the following steps:
    /// </para>
    /// <para>
    /// 1. Checks if alignment is specified
    /// 2. Applies padding to the new line text if alignment is specified
    /// 3. Adds the new line text to the paragraph.
    /// </para>
    /// </remarks>
    public override void Render(LogEvent logEvent, Paragraph paragraph)
    {
        var run = new Run
        {
            Text = alignment.HasValue
                ? PaddingTransform.Apply(Environment.NewLine, alignment.Value.Widen(Environment.NewLine.Length))
                : Environment.NewLine,
        };
        paragraph.Inlines.Add(run);
    }
}
