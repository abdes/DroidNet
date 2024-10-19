// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Output;

using DroidNet.Controls.OutputLog.Rendering;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Parsing;

internal sealed class NewLineTokenRenderer(Alignment? alignment) : TokenRenderer
{
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
