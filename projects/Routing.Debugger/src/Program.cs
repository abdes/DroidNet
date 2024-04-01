// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#if !DISABLE_XAML_GENERATED_MAIN
#error "This project only works with custom Main entry point. Must set DISABLE_XAML_GENERATED_MAIN to True."
#endif

namespace DroidNet.Routing.Debugger;

using System.Globalization;
using System.Runtime.InteropServices;
using CommunityToolkit.Mvvm.DependencyInjection;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Layouts;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.Debugger.UI.Config;
using DroidNet.Routing.Debugger.UI.Shell;
using DroidNet.Routing.Debugger.UI.State;
using DroidNet.Routing.Debugger.UI.UrlTree;
using DroidNet.Routing.Debugger.UI.WorkSpace;
using DroidNet.Routing.Debugger.Welcome;
using DroidNet.Routing.WinUI;
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Serilog;
using Serilog.Events;
using Serilog.Templates;
using Container = DryIoc.Container;

/// <summary>
/// The Main entry of the application.
/// </summary>
/// <remarks>
/// Overrides the usual WinUI XAML entry point in order to be able to control
/// what exactly happens at the entry point of the application. Customized here
/// to build an application <see cref="Host" /> and populate it with the
/// default services (such as Configuration, Logging, etc...) and a specialized
/// <see cref="IHostedService" /> for running the User Interface thread.
/// </remarks>
public static partial class Program
{
    /// <summary>
    /// Ensures that the process can run XAML, and provides a deterministic
    /// error if a check fails. Otherwise, it quietly does nothing.
    /// </summary>
    [LibraryImport("Microsoft.ui.xaml.dll")]
    private static partial void XamlCheckProcessRequirements();

    [STAThread]
    private static void Main(string[] args)
    {
        XamlCheckProcessRequirements();

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
                .ConfigureLogging()
                .ConfigureWinUI<App>()
                .ConfigureRouter(MakeRoutes())
                .ConfigureAutoInjected()
                .ConfigureContainer<DryIocServiceProvider>(ConfigureAdditionalServices)
                .Build();

            // Set up the MVVM Ioc using the host built services.
            Ioc.Default.ConfigureServices(host.Services);

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

    private static void ConfigureAdditionalServices(
        HostBuilderContext hostBuilderContext,
        DryIocServiceProvider serviceProvider)
    {
        // TODO(abdes): refactor into extension method
        // Set up the view model to view converters. We're using the standard
        // converter, and a custom one with fall back if the view cannot be
        // located.
        serviceProvider.Container.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        serviceProvider.Container.Register<IValueConverter, ViewModelToView>(Reuse.Singleton, serviceKey: "VmToView");

        // TODO(abdes): refactor into extension method
        serviceProvider.Container.Register<DockPanelViewModel>(Reuse.Transient);
        serviceProvider.Container.Register<DockPanel>(Reuse.Transient);

        /*
         * Configure the Application's Windows. Each window represents a target in which to open the requested url. The
         * target name is the key used when registering the window type.
         *
         * There should always be a Window registered for the special target <c>_main</c>.
         */

        // The Main Window is a singleton and its content can be re-assigned as needed. It is registered with a key that
        // corresponding to name of the special target <see cref="Target.Main" />.
        serviceProvider.Container.Register<Window, MainWindow>(Reuse.Singleton, serviceKey: Target.Main);

        serviceProvider.Container.Register<ShellViewModel>(Reuse.Singleton);
        serviceProvider.Container.Register<ShellView>(Reuse.Singleton);
        serviceProvider.Container.Register<WelcomeViewModel>(Reuse.Transient);
        serviceProvider.Container.Register<WelcomeView>(Reuse.Transient);
        serviceProvider.Container.Register<WorkSpaceViewModel>(Reuse.Transient);
        serviceProvider.Container.Register<WorkSpaceView>(Reuse.Transient);
        serviceProvider.Container.Register<RoutesViewModel>(Reuse.Singleton);
        serviceProvider.Container.Register<RoutesView>(Reuse.Transient);
        serviceProvider.Container.Register<UrlTreeViewModel>(Reuse.Singleton);
        serviceProvider.Container.Register<UrlTreeView>(Reuse.Transient);
        serviceProvider.Container.Register<RouterStateViewModel>(Reuse.Singleton);
        serviceProvider.Container.Register<RouterStateView>(Reuse.Transient);
        serviceProvider.Container.Register<IDockViewFactory, DockViewFactory>(Reuse.Singleton);
    }

    private static Routes MakeRoutes() => new(
    [
        new Route
        {
            Path = string.Empty,
            MatchMethod = PathMatch.StrictPrefix,
            ViewModelType = typeof(ShellViewModel),
            Children = new Routes(
            [
                new Route
                {
                    Outlet = "dock",
                    Path = string.Empty,
                    ViewModelType = typeof(WorkSpaceViewModel),
                    Children = new Routes(
                    [
                        new Route
                        {
                            Outlet = "app",
                            Path = "Welcome",
                            ViewModelType = typeof(WelcomeViewModel),
                        },
                        new Route
                        {
                            Outlet = "routes",
                            Path = "Config/Routes",
                            ViewModelType = typeof(RoutesViewModel),
                        },
                        new Route
                        {
                            Outlet = "urlTree",
                            Path = "Parser/UrlTree",
                            ViewModelType = typeof(UrlTreeViewModel),
                        },
                        new Route
                        {
                            Outlet = "state",
                            Path = "Router/State",
                            ViewModelType = typeof(RouterStateViewModel),
                        },
                    ]),
                },
            ]),
        },
    ]);
}
