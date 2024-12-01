// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.OutputLog;
using Serilog.Events;
using Serilog.Parsing;

namespace DroidNet.Controls.Demo.OutputLog;

/// <summary>
/// ViewModel for demonstrating the output log.
/// </summary>
/// <param name="outputLogSink">The output log sink.</param>
public partial class OutputLogDemoViewModel(DelegatingSink<RichTextBlockSink> outputLogSink) : ObservableObject
{
    /// <summary>
    /// Gets the output log sink.
    /// </summary>
    public DelegatingSink<RichTextBlockSink> OutputLogSink { get; } = outputLogSink;

    [RelayCommand]
    private void MakeLog(string logLevel)
    {
        if (Enum.TryParse(logLevel, out LogEventLevel level))
        {
            var messageTemplate = new MessageTemplateParser().Parse($"{level} log message");
            var logEvent = new LogEvent(DateTimeOffset.Now, level, exception: null, messageTemplate, []);
            this.OutputLogSink.Emit(logEvent);
        }
    }
}
