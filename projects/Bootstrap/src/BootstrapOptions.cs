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
    private readonly Dictionary<string, LogEventLevel> minimumLevels = new(StringComparer.Ordinal);

    /// <summary>
    ///     Gets a value indicating whether the `Serilog` should be hooked into <c>Microsoft.Extensions.Logging</c>.
    /// </summary>
    internal bool UseLoggingAbstraction { get; private set; }

    /// <summary>
    ///     Gets a value indicating whether the <c>OutputConsole</c> sink should be configured.
    /// </summary>
    internal bool RegisterOutputConsole { get; private set; }

    /// <summary>
    ///     Gets the <c>DroidNet.Controls.OutputConsole.Model.SubsystemMap</c> supplied by the application, typed as
    ///     <see cref="object"/> because Bootstrap does not reference the control.
    /// </summary>
    internal object? OutputConsoleSubsystems { get; private set; }

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
    ///     Gets the minimum levels of the sources that set their own, applied over the logging configuration.
    /// </summary>
    internal IReadOnlyDictionary<string, LogEventLevel> MinimumLevels => this.minimumLevels;

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
    ///     Sets the minimum level of the events of one source, over what the logging configuration says. A source
    ///     with its own level control, such as a native library's verbosity, takes <see cref="LogEventLevel.Verbose" />
    ///     so that its own control alone decides what it logs.
    /// </summary>
    /// <param name="source">The source context, or the prefix of the source contexts, the level applies to.</param>
    /// <param name="level">The minimum level of the source's events.</param>
    /// <returns>This <see cref="BootstrapOptions"/> instance for method chaining.</returns>
    public BootstrapOptions WithMinimumLevel(string source, LogEventLevel level)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(source);
        this.minimumLevels[source] = level;
        return this;
    }

    /// <summary>
    ///     Registers a Logger sink for the <c>OutputConsole</c> control.
    /// </summary>
    /// <param name="subsystems">
    ///     A <c>DroidNet.Controls.OutputConsole.Model.SubsystemMap</c> mapping log sources to subsystem names. The
    ///     application must supply it; the control has no default map.
    /// </param>
    /// <param name="capacity">Max entries to retain in the ring buffer. Defaults to 10000.</param>
    /// <param name="restrictedToMinimumLevel">Minimum level for this sink. Defaults to Verbose.</param>
    /// <param name="levelSwitch">Optional level switch for dynamic level control.</param>
    /// <returns>This <see cref="BootstrapOptions"/> instance for method chaining.</returns>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="subsystems"/> is <see langword="null"/>.</exception>
    public BootstrapOptions WithOutputConsole(
        object subsystems,
        int capacity = 10000,
        LogEventLevel restrictedToMinimumLevel = LogEventLevel.Verbose,
        LoggingLevelSwitch? levelSwitch = null)
    {
        ArgumentNullException.ThrowIfNull(subsystems);

        this.RegisterOutputConsole = true;
        this.OutputConsoleSubsystems = subsystems;
        this.OutputConsoleCapacity = capacity;
        this.OutputConsoleMinimumLevel = restrictedToMinimumLevel;
        this.OutputConsoleLevelSwitch = levelSwitch;
        return this;
    }
}
