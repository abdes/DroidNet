// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;
using DroidNet.Aura;
using DroidNet.Bootstrap;
using DroidNet.Config;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Layouts;
using DroidNet.Routing.Debugger.UI.Config;
using DroidNet.Routing.Debugger.UI.Docks;
using DroidNet.Routing.Debugger.UI.Shell;
using DroidNet.Routing.Debugger.UI.State;
using DroidNet.Routing.Debugger.UI.UrlTree;
using DroidNet.Routing.Debugger.UI.WorkSpace;
using DroidNet.Routing.Debugger.Welcome;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Serilog;

#if !DISABLE_XAML_GENERATED_MAIN
#error "This project only works with custom Main entry point. Must set DISABLE_XAML_GENERATED_MAIN to True."
#endif

namespace DroidNet.Routing.Debugger;

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
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "generated code uses public")]
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
        XamlCheckProcessRequirements();

        var bootstrap = new Bootstrapper(args);
        try
        {
            _ = bootstrap.Configure()
                .WithMvvm()
                .WithRouting(MakeRoutes())
                .WithWinUI<App>()
                .Build();

            // Final container is now available.
            var container = bootstrap.Container;
            try
            {
                InitializeSettings(container);
                ConfigureApplicationServices(container);
            }
            catch (Exception ex) when (ex is DryIoc.ContainerException or InvalidOperationException)
            {
                // No SettingsManager registered - skip initialization
            }

            // Finally start the host. This will block until the application lifetime is terminated
            // through CTRL+C, closing the UI windows or programmatically.
            bootstrap.Run();
        }
        catch (Exception ex)
        {
            Log.Fatal(ex, "Host terminated unexpectedly");
        }
        finally
        {
            Log.CloseAndFlush();
            bootstrap.Dispose();
        }
    }

    private static void InitializeSettings(DryIoc.IContainer container)
    {
        // Register Config module
        _ = container.WithConfig();

        // If the bootstrapper included `Config` module, it will have added a SettingsManager.
        // Configure settings source, and initialize it now so sources are loaded and
        // services receive their initial values.
        var manager = container.Resolve<SettingsManager>();
        Debug.Assert(manager is not null, "SettingsManager should be registered");

        var pathFinder = container.GetRequiredService<IPathFinder>();
        _ = container
            .WithJsonConfigSource(
                "aura.decoration",
                pathFinder.GetConfigFilePath("aura.json"),
                watch: true)
            .WithJsonConfigSource(
                "aura.appearance",
                pathFinder.GetConfigFilePath("settings.json"),
                watch: true);

        manager.InitializeAsync().GetAwaiter().GetResult();
        _ = manager.AutoSave = true;
    }

    private static void ConfigureApplicationServices(this IContainer container)
    {
        // Register Aura window management with all required services
        _ = container.WithAura(options => options
            .WithAppearanceSettings()
            .WithDecorationSettings()
            .WithBackdropService()
            .WithChromeService()
            .WithThemeModeService());

        container.Register<DockViewFactory>(Reuse.Singleton);
        container.Register<DockPanelViewModel>(Reuse.Transient);
        container.Register<DockPanel>(Reuse.Transient);

        /*
         * Configure the Application's Windows. Each window represents a target in which to open the
         * requested url. The target name is the key used when registering the window type.
         *
         * There should always be a Window registered for the special target <c>_main</c>.
         */

        // The Main Window is a singleton and its content can be re-assigned as needed. It is
        // registered with a key that corresponding to name of the special target <see
        // cref="Target.Main" />.
        container.Register<Window, RoutedWindow>(Reuse.Singleton, serviceKey: Target.Main);

        container.Register<MainShellView>(Reuse.Singleton);
        container.Register<MainShellViewModel>(Reuse.Singleton);

        container.Register<ShellViewModel>(Reuse.Singleton);
        container.Register<ShellView>(Reuse.Singleton);
        container.Register<EmbeddedAppViewModel>(Reuse.Transient);
        container.Register<EmbeddedAppView>(Reuse.Transient);
        container.Register<WelcomeViewModel>(Reuse.Transient);
        container.Register<WelcomeView>(Reuse.Transient);
        container.Register<WorkSpaceViewModel>(Reuse.Transient);
        container.Register<WorkSpaceView>(Reuse.Singleton);
        container.Register<RoutesViewModel>(Reuse.Singleton);
        container.Register<RoutesView>(Reuse.Transient);
        container.Register<UrlTreeViewModel>(Reuse.Singleton);
        container.Register<UrlTreeView>(Reuse.Transient);
        container.Register<RouterStateViewModel>(Reuse.Transient);
        container.Register<RouterStateView>(Reuse.Transient);
        container.Register<IDockViewFactory, DockViewFactory>(Reuse.Singleton);
    }

    private static Routes MakeRoutes() => new(
    [
        new Route
        {
            Path = string.Empty,
            ViewModelType = typeof(MainShellViewModel),
            Children = new Routes(
            [
                new Route
                {
                    Path = "workspace",
                    MatchMethod = PathMatch.Prefix,
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
                                    Path = "Config",
                                    ViewModelType = typeof(RoutesViewModel),
                                },
                                new Route
                                {
                                    Outlet = "url-tree",
                                    Path = "Parser",
                                    ViewModelType = typeof(UrlTreeViewModel),
                                },
                                new Route
                                {
                                    Outlet = "router-state",
                                    Path = "Recognizer",
                                    ViewModelType = typeof(RouterStateViewModel),
                                },
                            ]),
                        },
                    ]),
                },
            ]),
        },
    ]);
}
