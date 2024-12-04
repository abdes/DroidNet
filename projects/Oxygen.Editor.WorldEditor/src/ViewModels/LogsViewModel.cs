// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputLog;

namespace Oxygen.Editor.WorldEditor.ViewModels;

public class LogsViewModel(DelegatingSink<RichTextBlockSink> outputLogSink)
{
    public DelegatingSink<RichTextBlockSink> OutputLogSink => outputLogSink;
}
