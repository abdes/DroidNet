// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#if !DISABLE_XAML_GENERATED_MAIN
#error "This project only works with custom Main entry point. Must set DISABLE_XAML_GENERATED_MAIN to True."
#endif

namespace DroidNet.Routing.Debugger;

using System.Runtime.InteropServices;
using CommunityToolkit.Mvvm.DependencyInjection;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Debugger.UI.Config;
using DroidNet.Routing.Debugger.UI.Shell;
using DroidNet.Routing.Debugger.UI.WorkSpace;
using DroidNet.Routing.Debugger.Welcome;
using DroidNet.Routing.UI;
using DroidNet.Routing.UI.Converters;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

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

        // Use a default application host builder, which comes with logging,
        // configuration providers for environment variables, command line,
        // appsettings.json and secrets.
        var builder = Host.CreateApplicationBuilder(args);

        // You can further customize and enhance the builder with additional
        // configuration sources, logging providers, etc.

        // Setup and provision the hosting context for the User Interface
        // service.
        ((IHostApplicationBuilder)builder).Properties.Add(
            HostingExtensions.HostingContextKey,
            new HostingContext() { IsLifetimeLinked = true });

        // Add the WinUI User Interface hosted service as early as possible to
        // allow the UI to start showing up while you continue setting up other
        // services not required for the UI.
        _ = builder.ConfigureWinUI<App>().ConfigureRouter(MakeRoutes());

        // TODO(abdes): Add Support for Docking.
        // _ = builder.ConfigureDocking();

        // Set up the view model to view converters. We're using the standard
        // converter, and a custom one with fall back if the view cannot be
        // located.
        _ = builder.Services.AddKeyedSingleton<IValueConverter, ViewModelToView>("VmToView");
        /*
         * Configure the Application's Windows. Each window represents a target
         * in which to open the requested url. The target name is the key used
         * when registering the window type.
         *
         * There should always be a Window registered for the special target
         * <c>_main</c>.
         */

        // The Main Window is a singleton and its content can be re-assigned
        // as needed. It is registered with the special target "_main".
        _ = builder.Services.AddKeyedSingleton<Window, MainWindow>(Target.Main);

        _ = builder.Services
                .AddTransient<ShellViewModel>()
                .AddTransient<ShellView>()
                .AddTransient<WelcomeViewModel>()
                .AddTransient<WelcomeView>()
                .AddTransient<WorkSpaceViewModel>()
                .AddTransient<WorkSpaceView>()
                .AddTransient<RoutesViewModel>()
                .AddTransient<RoutesView>()
            /*.AddTransient<DockView>()
            .AddTransient<DockPanelViewModel>()
            .AddTransient<DockPanelView>()
            .AddTransient<DockTestViewModel>()
            .AddTransient<DockTestView>()
            .AddTransient<EmptyShellViewModel>()
            .AddTransient<EmptyShellView>()
            .AddTransient<AppShellViewModel>()
            .AddTransient<AppShellView>()
            .AddTransient<HomeViewModel>()
            .AddTransient<HomeView>()
            .AddTransient<ConfigViewModel>()
            .AddTransient<ConfigView>()
            .AddTransient<UrlTreeViewModel>()
            .AddTransient<UrlTreeView>()
            .AddTransient<RouterStateViewModel>()
            .AddTransient<RouterStateView>()*/;

        var host = builder.Build();

        // Set up the MVVM Ioc using the host built services.
        Ioc.Default.ConfigureServices(host.Services);

        // Finally start the host. This will block until the application
        // lifetime is terminated through CTRL+C, closing the UI windows or
        // programmatically.
        host.Run();
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
                /*new Route
                {
                    Path = "Home",
                    ViewModelType = typeof(HomeViewModel),
                    Children = new Routes(
                    [
                        new Route
                        {
                            Path = string.Empty,
                            Children = new Routes(
                            [
                                new Route
                                {
                                    Path = "Blank",
                                    ViewModelType = typeof(BlankViewModel),
                                },
                                new Route
                                {
                                    Path = "Deep",
                                    ViewModelType = typeof(BlankViewModel),
                                    Children = new Routes(
                                    [
                                        new Route
                                        {
                                            Path = "Route",
                                            ViewModelType = typeof(BlankViewModel),
                                        },
                                    ]),
                                },

                                new Route
                                {
                                    Outlet = "config",
                                    Path = "Config",
                                    ViewModelType = typeof(ConfigViewModel),
                                },
                                new Route
                                {
                                    Outlet = "urlTree",
                                    Path = "UrlTree",
                                    ViewModelType = typeof(UrlTreeViewModel),
                                },
                                new Route
                                {
                                    Outlet = "state",
                                    Path = "State",
                                    ViewModelType = typeof(RouterStateViewModel),
                                },
                            ]),
                        },
                    ]),
                },
                new Route
                {
                    Path = "DockTest",
                    ViewModelType = typeof(DockTestViewModel),
                },*/
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
                        /*
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
                    */
                    ]),
                },
            ]),
        },
    ]);
}
