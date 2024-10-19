// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog;

using DroidNet.Controls.OutputLog.Output;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Serilog.Core;
using Serilog.Events;

public class RichTextBlockSink(RichTextBlock richTextBlock, ILogEventRenderer renderer)
    : ILogEventSink
{
    public void Emit(LogEvent logEvent)
    {
        if (richTextBlock.DispatcherQueue.HasThreadAccess)
        {
            this.AppendLog(logEvent);
        }
        else
        {
            richTextBlock.DispatcherQueue.TryEnqueue(() => this.AppendLog(logEvent));
        }
    }

    private void AppendLog(LogEvent logEvent)
    {
        var paragraph = new Paragraph();
        renderer.Render(logEvent, paragraph);
        richTextBlock.Blocks.Add(paragraph);
    }
}
