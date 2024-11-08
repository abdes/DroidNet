// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#if !DISABLE_XAML_GENERATED_MAIN
#error "This project only works with custom Main entry point. Must set DISABLE_XAML_GENERATED_MAIN to True."
#endif

namespace DroidNet.Controls.Demo;

using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;
using CommunityToolkit.Mvvm.DependencyInjection;
using DroidNet.Controls.Demo.DemoBrowser;
using DroidNet.Controls.Demo.DynamicTree;
using DroidNet.Hosting;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Serilog;

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
    /// <summary>
    /// Ensures that the process can run XAML, and provides a deterministic error if a check fails. Otherwise, it
    /// quietly does nothing.
    /// </summary>
    [LibraryImport("Microsoft.ui.xaml.dll")]
    private static partial void XamlCheckProcessRequirements();

    [STAThread]
    private static void Main(string[] args)
    {
        XamlCheckProcessRequirements();

        Log.Information("Setting up the host");

        try
        {
            // Use a default application host builder, which comes with logging, configuration providers for environment
            // variables, command line, 'appsettings.json' and secrets.
            var builder = Host.CreateDefaultBuilder(args);

            // Use DryIoc instead of the built-in service provider.
            _ = builder.UseServiceProviderFactory(new DryIocServiceProviderFactory(new Container()));

            // Add the WinUI User Interface hosted service as early as possible to allow the UI to start showing up
            // while you continue setting up other services not required for the UI.
            builder.Properties.Add(
                Hosting.WinUI.HostingExtensions.HostingContextKey,
                new HostingContext() { IsLifetimeLinked = true });

            var host = builder
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

    private static Routes MakeRoutes() => new(
    [
        new Route
        {
            Path = string.Empty,
            MatchMethod = PathMatch.Prefix,
            ViewModelType = typeof(DemoBrowserViewModel),
            Children = new Routes(
            [
                new Route()
                {
                    // The project browser is the root of a navigation view with multiple pages.
                    Path = "dynamic-tree",
                    ViewModelType = typeof(ProjectLayoutViewModel),
                },
            ]),
        },
    ]);
}
