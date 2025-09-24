// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Serilog.Events;

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     Bitmask representing which <see cref="LogEventLevel" /> values are enabled for
///     display in the OutputConsole control.
/// </summary>
[Flags]
public enum LevelMask
{
    /// <summary>
    ///     No log levels selected.
    /// </summary>
    None = 0,

    /// <summary>
    ///     Include <see cref="LogEventLevel.Verbose" /> events.
    /// </summary>
    Verbose = 1 << LogEventLevel.Verbose,

    /// <summary>
    ///     Include <see cref="LogEventLevel.Debug" /> events.
    /// </summary>
    Debug = 1 << LogEventLevel.Debug,

    /// <summary>
    ///     Include <see cref="LogEventLevel.Information" /> events.
    /// </summary>
    Information = 1 << LogEventLevel.Information,

    /// <summary>
    ///     Include <see cref="LogEventLevel.Warning" /> events.
    /// </summary>
    Warning = 1 << LogEventLevel.Warning,

    /// <summary>
    ///     Include <see cref="LogEventLevel.Error" /> events.
    /// </summary>
    Error = 1 << LogEventLevel.Error,

    /// <summary>
    ///     Include <see cref="LogEventLevel.Fatal" /> events.
    /// </summary>
    Fatal = 1 << LogEventLevel.Fatal,

    /// <summary>
    ///     All log levels selected (Verbose, Debug, Information, Warning, Error, Fatal).
    /// </summary>
    All = Verbose | Debug | Information | Warning | Error | Fatal,
}
