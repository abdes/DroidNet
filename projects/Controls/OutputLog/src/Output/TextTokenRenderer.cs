// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Output;

using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;

internal sealed class TextTokenRenderer(Theme theme, string text) : TokenRenderer
{
    public override void Render(LogEvent logEvent, Paragraph paragraph)
    {
        using var styleInfo = theme.Apply(paragraph, ThemeStyle.TertiaryText);
        styleInfo.Run.Text = SpecialCharsEscaping.Apply(text);
    }
}
