// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog;

using DroidNet.Controls.OutputLog.Theming;
using Serilog.Core;
using Serilog.Events;

public class DelegatingSink<T>(
    string outputTemplate,
    IFormatProvider? formatProvider,
    ThemeId? theme) : ILogEventSink
    where T : ILogEventSink
{
    public T? DelegateSink { get; set; }

    public string OutputTemplate => outputTemplate;

    public IFormatProvider? FormatProvider => formatProvider;

    public ThemeId? Theme => theme;

    public void Emit(LogEvent logEvent) => this.DelegateSink?.Emit(logEvent);
}
