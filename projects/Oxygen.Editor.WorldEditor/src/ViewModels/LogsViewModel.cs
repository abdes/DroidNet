// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputLog;

namespace Oxygen.Editor.WorldEditor.ViewModels;

/// <summary>
/// The ViewModel for managing and displaying logs in the application.
/// </summary>
public class LogsViewModel
{
    /// <summary>
    /// Initializes a new instance of the <see cref="LogsViewModel"/> class.
    /// </summary>
    /// <param name="outputLogSink">The delegating sink for the output log.</param>
    public LogsViewModel(DelegatingSink<RichTextBlockSink> outputLogSink)
    {
        this.OutputLogSink = outputLogSink;
    }

    /// <summary>
    /// Gets the delegating sink for the output log.
    /// </summary>
    public DelegatingSink<RichTextBlockSink> OutputLogSink { get; }
}
