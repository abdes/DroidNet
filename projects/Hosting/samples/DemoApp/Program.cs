// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Runtime.InteropServices;
using DroidNet.Hosting.WinUI;
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Serilog;
using Serilog.Events;
using Serilog.Templates;

#if !DISABLE_XAML_GENERATED_MAIN
#error "This project only works with custom Main entry point. Must set DISABLE_XAML_GENERATED_MAIN to True."
#endif

namespace DroidNet.Hosting.Demo;

/// <summary>
/// The Main entry of the application.
/// <para>
/// Overrides the usual WinUI XAML entry point in order to be able to control what exactly happens at the entry point of the
/// application. Customized here to build an application <see cref="Host" /> and populate it with the default services (such as
/// Configuration, Logging, etc...) and a specialized <see cref="IHostedService" /> for running the User Interface thread.
/// </para>
/// </summary>
/// <remarks>
/// <para>
/// Convenience hosting extension methods are used to simplify the setup of services needed for the User Interface, logging, etc.
/// </para>
/// <para>
/// The WinUI service configuration supports customization, through a <see cref="HostingContext" /> object placed in the
/// <see cref="IHostApplicationBuilder.Properties" /> of the host builder. Currently, the
/// <see cref="BaseHostingContext.IsLifetimeLinked" /> property allows to specify if the User Interface thread lifetime is linked
/// to the application lifetime or not. When the two lifetimes are linked, terminating either of them will result in terminating
/// the other.
/// </para>
/// </remarks>
[ExcludeFromCodeCoverage]
public static partial class Program
{
    [LibraryImport("Microsoft.ui.xaml.dll")]
    [DefaultDllImportSearchPaths(DllImportSearchPath.SafeDirectories)]
    private static partial void XamlCheckProcessRequirements();

    [STAThread]
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "the Main method need to catch all")]
    private static void Main(string[] args)
    {
        // Ensures that the process can run XAML, and provides a deterministic error if a check
        // fails. Otherwise, it quietly does nothing.
        XamlCheckProcessRequirements();

        // Use Serilog, but decouple the logging clients from the implementation by using the generic
        // Microsoft.Extensions.Logging.ILogger instead of Serilog ILogger.
        // https://nblumhardt.com/2021/06/customize-serilog-text-output/
        Log.Logger = new LoggerConfiguration()
            .MinimumLevel.Debug()
            .MinimumLevel.Override("Microsoft", LogEventLevel.Information)
            .Enrich.FromLogContext()
            .WriteTo.Debug(
                new ExpressionTemplate(
                    "[{@t:HH:mm:ss} {@l:u3} ({Substring(SourceContext, LastIndexOf(SourceContext, '.') + 1)})] {@m:lj}\n{@x}",
                    new CultureInfo("en-US")))
            /* .WriteTo.Seq("http://localhost:5341/") */
            .CreateLogger();

        Log.Information("Setting up the host");

        var container = new Container();
        try
        {
            // Use a default application host builder, which comes with logging, configuration providers for environment
            // variables, command line, 'appsettings.json' and secrets.
            var builder = Host.CreateDefaultBuilder(args);

            // Use DryIoc instead of the built-in service provider.
            _ = builder.UseServiceProviderFactory(new DryIocServiceProviderFactory(container));

            // Add the WinUI User Interface hosted service as early as possible to allow the UI to start showing up
            // while you continue setting up other services not required for the UI.
            var host = builder
                .ConfigureLogging()
                .ConfigureServices(sc => sc.ConfigureWinUI<App>(isLifetimeLinked: true))
                .ConfigureDemoServices()
                .Build();

            // Finally start the host. This will block until the application lifetime is terminated through CTRL+C,
            // closing the UI windows or programmatically.
            host.Run();
        }
        catch (Exception ex)
        {
            Log.Fatal(ex, "Host terminated unexpectedly");
        }
        finally
        {
            Log.CloseAndFlush();
            container.Dispose();
        }
    }
}
