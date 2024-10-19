// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ViewModels;

using DroidNet.Controls.OutputLog;
using DroidNet.Hosting.Generators;
using Microsoft.Extensions.DependencyInjection;

[InjectAs(ServiceLifetime.Transient)]
public class LogsViewModel(DelegatingSink<RichTextBlockSink> outputLogSink)
{
    public DelegatingSink<RichTextBlockSink> OutputLogSink => outputLogSink;
}
