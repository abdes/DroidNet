// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Demo;

using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;
using DroidNet.Bootstrap;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Demo.Navigation;
using DroidNet.Routing.Demo.Shell;
using DryIoc;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Serilog;

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

        var bootstrap = new Bootstrapper(args);
        try
        {
            bootstrap.Configure()
                .WithConfiguration((_, _, _) => [], configureOptionsPattern: null)
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
        // The Main Window is a singleton and its content can be re-assigned as needed. It is registered with a key that
        // corresponding to name of the special target <see cref="Target.Main" />.
        container.Register<Window, MainWindow>(Reuse.Singleton, serviceKey: Target.Main);

        // Views and ViewModels
        container.Register<ShellView>(Reuse.Singleton);
        container.Register<ShellViewModel>(Reuse.Singleton);
        container.Register<PageOneView>(Reuse.Singleton);
        container.Register<PageOneViewModel>(Reuse.Singleton);
        container.Register<PageTwoView>(Reuse.Singleton);
        container.Register<PageTwoViewModel>(Reuse.Singleton);
        container.Register<PageThreeView>(Reuse.Singleton);
        container.Register<PageThreeViewModel>(Reuse.Singleton);
        container.Register<RoutedNavigationView>(Reuse.Singleton);
        container.Register<RoutedNavigationViewModel>(Reuse.Singleton);
        container.Register<SettingsView>(Reuse.Singleton);
        container.Register<SettingsViewModel>(Reuse.Singleton);
    }

    private static Routes MakeRoutes() => new(
    [
        new Route
        {
            // We want the shell view to be loaded not matter what absolute URL we are navigating to. So, it has an
            // empty path with a Prefix matching type, which will always match and not consume a segment.
            Path = string.Empty,
            MatchMethod = PathMatch.Prefix,
            ViewModelType = typeof(ShellViewModel),
            Children = new Routes(
            [
                new Route
                {
                    // This the root of a navigation view with multiple pages.
                    Path = "nav",
                    ViewModelType = typeof(RoutedNavigationViewModel),
                    Children = new Routes(
                    [
                        new Route
                        {
                            Path = "1",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(PageOneViewModel),
                        },
                        new Route
                        {
                            Path = "2",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(PageTwoViewModel),
                        },
                        new Route
                        {
                            Path = "3",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(PageThreeViewModel),
                        },
                        new Route
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
}
