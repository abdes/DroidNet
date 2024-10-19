// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Output;

using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Parsing;

internal sealed class TimestampTokenRenderer(Theme theme, PropertyToken token, IFormatProvider? formatProvider)
    : TokenRenderer
{
    public override void Render(LogEvent logEvent, Paragraph paragraph)
    {
        // We need access to ScalarValue.Render() to avoid this alloc; just ensures
        // that custom format providers are supported properly.
        var sv = new ScalarValue(logEvent.Timestamp);

        using var styleInfo = theme.Apply(paragraph, ThemeStyle.SecondaryText);

        var output = new StringWriter();
        sv.Render(output, token.Format, formatProvider);

        styleInfo.Run.Text = token.Alignment is null
            ? output.ToString()
            : PaddingTransform.Apply(output.ToString(), token.Alignment);
    }
}
