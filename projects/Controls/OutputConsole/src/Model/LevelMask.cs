// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using Serilog.Events;

namespace DroidNet.Controls.OutputConsole.Model;

[Flags]
public enum LevelMask
{
    None = 0,
    Verbose = 1 << (int)LogEventLevel.Verbose,
    Debug = 1 << (int)LogEventLevel.Debug,
    Information = 1 << (int)LogEventLevel.Information,
    Warning = 1 << (int)LogEventLevel.Warning,
    Error = 1 << (int)LogEventLevel.Error,
    Fatal = 1 << (int)LogEventLevel.Fatal,

    All = Verbose | Debug | Information | Warning | Error | Fatal,
}
