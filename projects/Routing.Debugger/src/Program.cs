// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#if !DISABLE_XAML_GENERATED_MAIN
#error "This project only works with custom Main entry point. Must set DISABLE_XAML_GENERATED_MAIN to True."
#endif

namespace DroidNet.Routing.Debugger;

using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;
using DroidNet.Bootstrap;
using DroidNet.Docking.Controls;
using DroidNet.Docking.Layouts;
using DroidNet.Routing;
using DroidNet.Routing.Debugger.UI.Config;
using DroidNet.Routing.Debugger.UI.Docks;
using DroidNet.Routing.Debugger.UI.Shell;
using DroidNet.Routing.Debugger.UI.State;
using DroidNet.Routing.Debugger.UI.UrlTree;
using DroidNet.Routing.Debugger.UI.WorkSpace;
using DroidNet.Routing.Debugger.Welcome;
using DryIoc;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Serilog;

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
            bootstrap.Configure()
                .WithConfiguration((_, _, _) => [], null)
                .WithLoggingAbstraction()
                .WithMvvm()
                .WithRouting(MakeRoutes())
                .WithWinUI<App>()
                .WithAppServices(ConfigureApplicationServices);

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

    private static void ConfigureApplicationServices(this IContainer container)
    {
        container.Register<DockViewFactory>(Reuse.Singleton);
        container.Register<DockPanelViewModel>(Reuse.Transient);
        container.Register<DockPanel>(Reuse.Transient);

        // The Main Window is a singleton and its content can be re-assigned as needed. It is registered with a key that
        // corresponding to name of the special target <see cref="Target.Main" />.
        container.Register<Window, MainWindow>(Reuse.Singleton, serviceKey: Target.Main);

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
    ]);
}
