// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor;

using System;
using System.Globalization;
using System.Reflection;
using DroidNet.Controls.OutputLog;
using DroidNet.Controls.OutputLog.Theming;
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Serilog;
using Serilog.Core;
using Serilog.Events;
using Serilog.Extensions.Logging;
using Serilog.Templates;

public static class HostingExtensions
{
    public static IHostBuilder ConfigureLogging(this IHostBuilder builder)
    {
        Log.Logger = CreateLogger(builder);

        return builder.ConfigureContainer<DryIocServiceProvider>(
            (_, serviceProvider) =>
            {
                var container = serviceProvider.Container;
                container.RegisterInstance<ILoggerFactory>(new SerilogLoggerFactory(Log.Logger));

                container.Register(
                    Made.Of(
                        _ => ServiceInfo.Of<ILoggerFactory>(),
                        f => f.CreateLogger(null!)),
                    setup: Setup.With(condition: r => r.Parent.ImplementationType == null));

                container.Register(
                    Made.Of(
                        _ => ServiceInfo.Of<ILoggerFactory>(),
                        f => f.CreateLogger(Arg.Index<Type>(0)),
                        r => r.Parent.ImplementationType),
                    setup: Setup.With(condition: r => r.Parent.ImplementationType != null));
            });
    }

    public static IHostBuilder ConfigureAutoInjected(this IHostBuilder builder) => builder.ConfigureServices(
        (context, services) =>
        {
            _ = context; // unused

            // Get all loaded assemblies
            foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                // Look for the AutoInjectExtensions class
                var autoInjectType = assembly.GetTypes()
                    .FirstOrDefault(
                        t => t is { IsClass: true, IsSealed: true, IsAbstract: true } && string.Equals(
                            t.Name,
                            "AutoInjectExtensions",
                            StringComparison.Ordinal));

                if (autoInjectType != null)
                {
                    // Look for the UseAutoInject method
                    var useAutoInjectMethod = autoInjectType.GetMethod(
                        "UseAutoInject",
                        BindingFlags.Public | BindingFlags.Static,
                        binder: null,
                        [typeof(IServiceCollection)],
                        modifiers: null);

                    // Invoke the UseAutoInject method on the given services
                    _ = useAutoInjectMethod?.Invoke(null, [services]);
                }
            }
        });

    /// <summary>
    /// Configures and creates a Serilog <see cref="Serilog.Core.Logger" /> that integrates with the provided <see cref="IHostBuilder" />.
    /// This setup decouples logging clients from the implementation by using the generic <see cref="Microsoft.Extensions.Logging.ILogger" />
    /// instead of Serilog's ILogger.
    /// For more information, visit: <see href="https://nblumhardt.com/2021/06/customize-serilog-text-output/" />.
    /// </summary>
    /// <param name="builder">The <see cref="IHostBuilder" /> used to configure the application's services and logging.</param>
    /// <returns>An instance of <see cref="Serilog.Core.Logger" /> configured with the specified settings.</returns>
    /// <remarks>
    /// The created logger has the following configuration:
    /// <list type="bullet">
    /// <item>
    /// <description>Minimum logging level is set to Debug.</description>
    /// </item>
    /// <item>
    /// <description>Overrides the minimum logging level for Microsoft namespaces to Information.</description>
    /// </item>
    /// <item>
    /// <description>Enriches log events with contextual information from the log context.</description>
    /// </item>
    /// <item>
    /// <description>Writes log events to the debug output using a custom expression template.</description>
    /// </item>
    /// <item>
    /// <description>Optionally writes log events to a Seq server (commented out).</description>
    /// </item>
    /// <item>
    /// <description>Writes log events to a custom sink for a RichTextBlock, integrating with the host builder.</description>
    /// </item>
    /// </list>
    /// </remarks>
    private static Logger CreateLogger(IHostBuilder builder)
        => new LoggerConfiguration()
            .MinimumLevel.Debug()
            .MinimumLevel.Override("Microsoft", LogEventLevel.Information)
            .Enrich.FromLogContext()
            .WriteTo.Debug(
                new ExpressionTemplate(
                    "[{@t:HH:mm:ss} {@l:u3} ({Substring(SourceContext, LastIndexOf(SourceContext, '.') + 1)})] {@m:lj}\n{@x}",
                    new CultureInfo("en-US")))
            /* .WriteTo.Seq("http://localhost:5341/") */
            .WriteTo.OutputLogView<RichTextBlockSink>(builder, theme: Themes.Literate)
            .CreateLogger();
}
