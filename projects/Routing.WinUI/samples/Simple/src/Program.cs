// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Samples.Simple;

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Runtime.InteropServices;
using CommunityToolkit.Mvvm.DependencyInjection;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Samples.Simple.Navigation;
using DroidNet.Routing.Samples.Simple.Shell;
using DroidNet.Routing.WinUI;
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Serilog;
using Serilog.Events;
using Serilog.Templates;

/// <summary>The Main entry of the application.</summary>
/// <remarks>
/// <para>
/// Overrides the usual WinUI XAML entry point in order to be able to control what exactly happens at the entry point of the
/// application. Customized here to build an application <see cref="Host" /> and populate it with the default services (such as
/// Configuration, Logging, etc...) and a specialized <see cref="IHostedService" /> for running the User Interface thread.
/// </para>
/// <para>
/// Convenience hosting extension methods are used to simplify the setup of services needed for the User Interface, logging, etc.
/// </para>
/// <para>
/// The WinUI service configuration supports customization, through a <see cref="HostingContext" /> object placed in the
/// <see cref="IHostApplicationBuilder.Properties" /> of the host builder. Currently, the IsLifetimeLinked property allows
/// to specify if the User Interface thread lifetime is linked to the application lifetime or not. When the two lifetimes are
/// linked, terminating either of them will result in terminating the other.
/// </para>
/// </remarks>
[ExcludeFromCodeCoverage]
public static partial class Program
{
    /// <summary>
    /// Ensures that the process can run XAML, and provides a deterministic error if a
    /// check fails. Otherwise, it quietly does nothing.
    /// </summary>
    [LibraryImport("Microsoft.ui.xaml.dll")]
    private static partial void XamlCheckProcessRequirements();

    [STAThread]
    private static void Main(string[] args)
    {
        XamlCheckProcessRequirements();

        ConfigureLogger();

        Log.Information("Setting up the host");

        try
        {
            var builder = Host.CreateDefaultBuilder(args);

            // Use DryIoc instead of the built-in service provider.
            _ = builder.UseServiceProviderFactory(new DryIocServiceProviderFactory(new Container()));

            // Add the WinUI User Interface hosted service as early as possible to allow the UI to start showing up
            // while you continue setting up other services not required for the UI.
            builder.Properties.Add(
                Hosting.WinUI.HostingExtensions.HostingContextKey,
                new HostingContext() { IsLifetimeLinked = true });

            var host = builder
                .ConfigureAppConfiguration(AddConfigurationFiles)
                .ConfigureWinUI<App>()
                .ConfigureContainer<DryIocServiceProvider>(
                    (_, serviceProvider) =>
                    {
                        var container = serviceProvider.Container;
                        container.ConfigureLogging();
                        container.ConfigureRouter(MakeRoutes());
                        container.ConfigureApplicationServices();

                        // Setup the CommunityToolkit.Mvvm Ioc helper
                        Ioc.Default.ConfigureServices(serviceProvider);
                    })
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
        }
    }

    /// <summary>
    /// Use Serilog, but decouple the logging clients from the implementation by using the generic <see cref="ILogger" /> instead
    /// of Serilog's ILogger.
    /// </summary>
    /// <seealso href="https://nblumhardt.com/2021/06/customize-serilog-text-output/" />
    private static void ConfigureLogger() =>
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

    private static Routes MakeRoutes() => new(
    [
        new Route
        {
            // We want the shell view to be loaded not matter what absolute URL we are navigating to. So, it has an
            // empty path with a Prefix matching type, which will always match and not consume a segment.
            Path = string.Empty,
            MatchMethod = PathMatch.StrictPrefix,
            ViewModelType = typeof(ShellViewModel),
            Children = new Routes(
            [
                new Route()
                {
                    // This the root of a navigation view with multiple pages.
                    Path = "nav",
                    ViewModelType = typeof(RoutedNavigationViewModel),
                    Children = new Routes(
                    [
                        new Route()
                        {
                            Path = "1",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(PageOneViewModel),
                        },
                        new Route()
                        {
                            Path = "2",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(PageTwoViewModel),
                        },
                        new Route()
                        {
                            Path = "3",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(PageThreeViewModel),
                        },
                        new Route()
                        {
                            Path = "settings",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(SettingsViewModel),
                        },
                    ]),
                },
            ]),
        },
    ]);

    private static void AddConfigurationFiles(HostBuilderContext context, IConfigurationBuilder config)
    {
        _ = context; // unused
        _ = config; // unused
    }
}
