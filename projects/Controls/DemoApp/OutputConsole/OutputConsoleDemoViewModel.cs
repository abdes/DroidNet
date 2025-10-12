// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.OutputConsole.Model;
using Serilog;
using Serilog.Events;

namespace DroidNet.Controls.Demo.OutputConsole;

/// <summary>
///     View model for the OutputConsole demo.
/// </summary>
public partial class OutputConsoleDemoViewModel : ObservableObject
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="OutputConsoleDemoViewModel"/> class.
    /// </summary>
    /// <param name="buffer">The <see cref="OutputLogBuffer"/> to use. The view model does not manage the buffer's lifetime.</param>
    public OutputConsoleDemoViewModel(OutputLogBuffer buffer)
        => this.Buffer = buffer;

    /// <summary>
    ///     Gets the output log buffer used by the demo UI.
    /// </summary>
    public OutputLogBuffer Buffer { get; }

    /// <summary>
    /// Writes a single Serilog event at the specified log level.
    /// </summary>
    /// <param name="logLevel">A string representing a <see cref="LogEventLevel"/> value (case-insensitive).</param>
    /// <remarks>
    /// If <paramref name="logLevel"/> parses to a valid <see cref="LogEventLevel"/>, a single
    /// log entry is written using Serilog's static <see cref="Log"/> API. The demo harness
    /// should configure a sink that forwards entries into <see cref="OutputLogBuffer"/>.
    /// </remarks>
    [RelayCommand]
    private static void MakeLog(string logLevel)
    {
        if (Enum.TryParse(logLevel, out LogEventLevel level))
        {
            Log.Write(level, "{Level} log message", level);
        }
    }
}
