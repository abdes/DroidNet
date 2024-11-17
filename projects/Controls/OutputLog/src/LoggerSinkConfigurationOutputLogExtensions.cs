// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputLog.Theming;
using DryIoc;
using Microsoft.Extensions.Hosting;
using Serilog;
using Serilog.Configuration;
using Serilog.Core;
using Serilog.Events;

namespace DroidNet.Controls.OutputLog;

/// <summary>
/// Provides extension methods for configuring Serilog to output logs to a specified sink with theming support.
/// </summary>
/// <remarks>
/// <para>
/// This static class contains extension methods that allow you to configure Serilog to output logs to a specified sink,
/// applying theming and formatting based on the provided settings.
/// </para>
/// <para>
/// <strong>Usage Guidelines:</strong>
/// Use these methods to configure Serilog to output logs to a custom sink, with support for theming and formatting.
/// This can be useful for scenarios where you need to display logs in a rich text control with consistent styling.
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var container = new Container();
/// var logger = new LoggerConfiguration()
///     .WriteTo.OutputLogView<RichTextBlockSink>(
///         container,
///         outputTemplate: "[{Timestamp:HH:mm:ss} {Level:u3}] {Message:l} {Properties}{NewLine}{Exception}",
///         theme: Themes.Literate)
///     .CreateLogger();
/// ]]></code>
/// </para>
/// </remarks>
public static class LoggerSinkConfigurationOutputLogExtensions
{
    private const string DefaultRichTextBoxOutputTemplate
        = "[{Timestamp:HH:mm:ss} {Level:u3}] {Message:l} {Properties}{NewLine}{Exception}";
    /*= "[{Timestamp:HH:mm:ss} {Level:u3}] {Message:lj} {Properties:j}{NewLine}{Exception}";*/

    /// <summary>
    /// Configures Serilog to output logs to a specified <see cref="ILogEventSink"/> and integrates it with the provided <see cref="IHostBuilder"/>.
    /// </summary>
    /// <typeparam name="T">The type of <see cref="ILogEventSink"/> to output logs to.</typeparam>
    /// <param name="sinkConfiguration">The <see cref="LoggerSinkConfiguration"/> to apply the sink to.</param>
    /// <param name="container">The DryIoc <see cref="IContainer"/> to integrate the sink with.</param>
    /// <param name="outputTemplate">The output template to format log events. Defaults to <c>DefaultRichTextBoxOutputTemplate</c>.</param>
    /// <param name="formatProvider">An optional provider to format values.</param>
    /// <param name="restrictedToMinimumLevel">The minimum level for events passed through the sink. Defaults to <see cref="LevelAlias.Minimum"/>.</param>
    /// <param name="levelSwitch">An optional switch allowing the pass-through minimum level to be changed at runtime.</param>
    /// <param name="theme">An optional theme to apply to the output.</param>
    /// <returns>A <see cref="LoggerConfiguration"/> allowing further configuration.</returns>
    /// <remarks>
    /// <para>
    /// This method configures a delegating sink of the specified type <typeparamref name="T"/> to format and output log events.
    /// </para>
    /// <para>
    /// <strong>Example usage:</strong>
    /// <code><![CDATA[
    /// var container = new Container();
    /// var logger = new LoggerConfiguration()
    ///     .WriteTo.OutputLogView<RichTextBlockSink>(
    ///         container,
    ///         outputTemplate: "[{Timestamp:HH:mm:ss} {Level:u3}] {Message:l} {Properties}{NewLine}{Exception}",
    ///         theme: Themes.Literate)
    ///     .CreateLogger();
    /// ]]></code>
    /// </para>
    /// </remarks>
    public static LoggerConfiguration OutputLogView<T>(
        this LoggerSinkConfiguration sinkConfiguration,
        IContainer container,
        string outputTemplate = DefaultRichTextBoxOutputTemplate,
        IFormatProvider? formatProvider = null,
        LogEventLevel restrictedToMinimumLevel = LevelAlias.Minimum,
        LoggingLevelSwitch? levelSwitch = null,
        ThemeId? theme = default)
        where T : ILogEventSink
    {
        var delegatingSink = new DelegatingSink<T>(outputTemplate, formatProvider, theme);

        // Add the delegating sink
        container.RegisterInstance(delegatingSink);

        return sinkConfiguration.Logger(
            lc => lc.Filter.ByIncludingOnly(FilterDelegatingSinkLogEvents)
                .WriteTo.Sink(delegatingSink, restrictedToMinimumLevel, levelSwitch));
    }

    /// <summary>
    /// Filters log events to include only those with an "EventId" property that starts with "ui-".
    /// </summary>
    /// <param name="evt">The log event to filter.</param>
    /// <returns><see langword="true"/> if the log event should be included; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// <para>
    /// This method filters log events to include only those with an "EventId" property that starts with "ui-".
    /// It is used to ensure that only relevant log events are processed by the delegating sink.
    /// </para>
    /// </remarks>
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
