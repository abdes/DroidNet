// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog;

using DroidNet.Controls.OutputLog.Theming;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Serilog;
using Serilog.Configuration;
using Serilog.Core;
using Serilog.Events;

public static class LoggerSinkConfigurationOutputLogViewExtensions
{
    private const string DefaultRichTextBoxOutputTemplate
        = "[{Timestamp:HH:mm:ss} {Level:u3}] {Message:l} {Properties}{NewLine}{Exception}";
    /*= "[{Timestamp:HH:mm:ss} {Level:u3}] {Message:lj} {Properties:j}{NewLine}{Exception}";*/

    /// <summary>
    /// Configures Serilog to output logs to a specified <see cref="ILogEventSink" /> and integrates it with the provided <see cref="IHostBuilder" />.
    /// </summary>
    /// <typeparam name="T">The type of <see cref="ILogEventSink" /> to output logs to.</typeparam>
    /// <param name="sinkConfiguration">The <see cref="LoggerSinkConfiguration" /> to apply the sink to.</param>
    /// <param name="builder">The <see cref="IHostBuilder" /> to integrate the sink with.</param>
    /// <param name="outputTemplate">The output template to format log events. Defaults to <c>DefaultRichTextBoxOutputTemplate</c>.</param>
    /// <param name="formatProvider">An optional provider to format values.</param>
    /// <param name="restrictedToMinimumLevel">The minimum level for events passed through the sink. Defaults to <see cref="LevelAlias.Minimum" />.</param>
    /// <param name="levelSwitch">An optional switch allowing the pass-through minimum level to be changed at runtime.</param>
    /// <param name="theme">An optional theme to apply to the output.</param>
    /// <returns>A <see cref="LoggerConfiguration" /> allowing further configuration.</returns>
    /// <remarks>
    /// This method configures a delegating sink of the specified type <typeparamref name="T" /> to format and output log events.
    /// </remarks>
    public static LoggerConfiguration OutputLogView<T>(
        this LoggerSinkConfiguration sinkConfiguration,
        IHostBuilder builder,
        string outputTemplate = DefaultRichTextBoxOutputTemplate,
        IFormatProvider? formatProvider = null,
        LogEventLevel restrictedToMinimumLevel = LevelAlias.Minimum,
        LoggingLevelSwitch? levelSwitch = null,
        ThemeId? theme = default)
        where T : ILogEventSink
    {
        var delegatingSink = new DelegatingSink<T>(outputTemplate, formatProvider, theme);

        // Add the delegating sink
        builder.ConfigureServices((_, services) => services.AddSingleton(delegatingSink));

        return sinkConfiguration.Logger(
            lc => lc.Filter.ByIncludingOnly(FilterDelegatingSinkLogEvents)
                .WriteTo.Sink(delegatingSink, restrictedToMinimumLevel, levelSwitch));
    }

    private static bool FilterDelegatingSinkLogEvents(LogEvent evt)
    {
        if (!evt.Properties.TryGetValue("EventId", out var property))
        {
            return false;
        }

        if (property is not StructureValue structureValue)
        {
            return false;
        }

        var eventNameProp
            = structureValue.Properties.FirstOrDefault(
                prop => prop.Name.Equals("Name", StringComparison.OrdinalIgnoreCase));
        var eventName = (string?)(eventNameProp?.Value as ScalarValue)?.Value;
        return eventName?.StartsWith("ui-", StringComparison.OrdinalIgnoreCase) ?? false;
    }
}
