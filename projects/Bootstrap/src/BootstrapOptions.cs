// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Serilog.Core;
using Serilog.Events;

namespace DroidNet.Bootstrap;

/// <summary>
///     Configuration options for Bootstrapper.
/// </summary>
public sealed class BootstrapOptions
{
    /// <summary>
    ///     Gets a value indicating whether the `Serilog` should be hooked into <c>Microsoft.Extensions.Logging</c>.
    /// </summary>
    internal bool UseLoggingAbstraction { get; private set; }

    /// <summary>
    ///     Gets a value indicating whether the <c>OutputLog</c> sink should be configured.
    /// </summary>
    internal bool RegisterOutputLog { get; private set; }

    /// <summary>
    ///     Gets the output template for formatting OutputLog events.
    /// </summary>
    internal string OutputLogTemplate { get; private set; } = "[{Timestamp:HH:mm:ss} {Level:u3}] {Message:lj}{NewLine}{Exception}";

    /// <summary>
    ///     Gets the format provider for OutputLog values.
    /// </summary>
    internal IFormatProvider? OutputLogFormatProvider { get; private set; }

    /// <summary>
    ///     Gets the minimum log level for the OutputLog sink.
    /// </summary>
    internal LogEventLevel OutputLogMinimumLevel { get; private set; } = LogEventLevel.Verbose;

    /// <summary>
    ///     Gets the optional level switch for the OutputLog sink.
    /// </summary>
    internal LoggingLevelSwitch? OutputLogLevelSwitch { get; private set; }

    /// <summary>
    ///     Gets the theme ID for the OutputLog sink.
    /// </summary>
    internal uint? OutputLogThemeId { get; private set; }

    /// <summary>
    ///     Gets a value indicating whether the <c>OutputConsole</c> sink should be configured.
    /// </summary>
    internal bool RegisterOutputConsole { get; private set; }

    /// <summary>
    ///     Gets the maximum number of entries to retain in the OutputConsole ring buffer.
    /// </summary>
    internal int OutputConsoleCapacity { get; private set; }

    /// <summary>
    ///     Gets the minimum log level for the OutputConsole sink.
    /// </summary>
    internal LogEventLevel OutputConsoleMinimumLevel { get; private set; }

    /// <summary>
    ///     Gets the optional level switch for the OutputConsole sink.
    /// </summary>
    internal LoggingLevelSwitch? OutputConsoleLevelSwitch { get; private set; }

    /// <summary>
    ///     Enables integration of Serilog with <c>Microsoft.Extensions.Logging</c> abstraction.
    /// </summary>
    /// <returns>This <see cref="BootstrapOptions"/> instance for method chaining.</returns>
    public BootstrapOptions WithLoggingAbstraction()
    {
        this.UseLoggingAbstraction = true;
        return this;
    }

    /// <summary>
    ///     Registers a Logger sink for the <c>OutputLog</c> control.
    /// </summary>
    /// <param name="formatProvider">An optional provider to format values.</param>
    /// <param name="outputTemplate">The output template to format log events.</param>
    /// <param name="restrictedToMinimumLevel">Minimum level for this sink. Defaults to Verbose.</param>
    /// <param name="levelSwitch">Optional level switch for dynamic level control.</param>
    /// <param name="themeId">An optional theme to apply to the output. Defaults to Literate theme (ID: 1).</param>
    /// <returns>This <see cref="BootstrapOptions"/> instance for method chaining.</returns>
    public BootstrapOptions WithOutputLog(
        IFormatProvider formatProvider,
        string? outputTemplate = null,
        LogEventLevel restrictedToMinimumLevel = LogEventLevel.Verbose,
        LoggingLevelSwitch? levelSwitch = null,
        uint? themeId = 0)
    {
        this.RegisterOutputLog = true;
        if (outputTemplate is not null)
        {
            this.OutputLogTemplate = outputTemplate;
        }

        this.OutputLogFormatProvider = formatProvider;
        this.OutputLogMinimumLevel = restrictedToMinimumLevel;
        this.OutputLogLevelSwitch = levelSwitch;
        this.OutputLogThemeId = themeId;
        return this;
    }

    /// <summary>
    ///     Registers a Logger sink for the <c>OutputConsole</c> control.
    /// </summary>
    /// <param name="capacity">Max entries to retain in the ring buffer. Defaults to 10000.</param>
    /// <param name="restrictedToMinimumLevel">Minimum level for this sink. Defaults to Verbose.</param>
    /// <param name="levelSwitch">Optional level switch for dynamic level control.</param>
    /// <returns>This <see cref="BootstrapOptions"/> instance for method chaining.</returns>
    public BootstrapOptions WithOutputConsole(
        int capacity = 10000,
        LogEventLevel restrictedToMinimumLevel = LogEventLevel.Verbose,
        LoggingLevelSwitch? levelSwitch = null)
    {
        this.RegisterOutputConsole = true;
        this.OutputConsoleCapacity = capacity;
        this.OutputConsoleMinimumLevel = restrictedToMinimumLevel;
        this.OutputConsoleLevelSwitch = levelSwitch;
        return this;
    }
}
