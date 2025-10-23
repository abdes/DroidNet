// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputConsole.Model;
using DroidNet.Controls.OutputConsole.Sinks;
using DryIoc;
using Serilog;
using Serilog.Configuration;
using Serilog.Core;
using Serilog.Events;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     Serilog sink configuration helpers to route logs into a shared OutputLogBuffer for the OutputConsole control.
/// </summary>
public static class LoggerSinkConfigurationOutputConsoleExtensions
{
    /// <summary>
    ///     Configure Serilog to write to an OutputLogBuffer, and register the buffer instance into the DryIoc container for UI
    ///     binding.
    /// </summary>
    /// <param name="sinkConfiguration">The Serilog sink configuration.</param>
    /// <param name="container">DryIoc container to register the shared buffer into.</param>
    /// <param name="capacity">Max entries to retain in the ring buffer.</param>
    /// <param name="restrictedToMinimumLevel">Minimum level for this sink.</param>
    /// <param name="levelSwitch">Optional level switch.</param>
    /// <returns>The logger configuration.</returns>
    public static LoggerConfiguration OutputConsole(
        this LoggerSinkConfiguration sinkConfiguration,
        IContainer container,
        int capacity,
        LogEventLevel restrictedToMinimumLevel,
        LoggingLevelSwitch? levelSwitch)
    {
        var buffer = new OutputLogBuffer(capacity);
        var sink = new BufferedSink(buffer);

        // Make the buffer available to the UI via DI
        container.RegisterInstance(buffer);

        return sinkConfiguration.Sink(sink, restrictedToMinimumLevel, levelSwitch);
    }
}
