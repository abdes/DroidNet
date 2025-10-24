// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;
using DroidNet.Aura;
using DroidNet.Bootstrap;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing.Demo.Navigation;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Serilog;

namespace DroidNet.Routing.Demo;

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
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "partial Main Program")]
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
        Debug.Assert(manager is not null, "Expecting ViewModelType not to be null");

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

        /*
         * Configure the Application's Windows. Each window represents a target in which to open the
         * requested url. The target name is the key used when registering the window type.
         *
         * There should always be a Window registered for the special target <c>_main</c>.
         */

        // The Main Window is a singleton and its content can be re-assigned as needed. It is
        // registered with a key that corresponding to name of the special target <see
        // cref="Target.Main" />.
        container.Register<Window, MainWindow>(Reuse.Singleton, serviceKey: Target.Main);

        // Views and ViewModels
        container.Register<MainShellView>(Reuse.Singleton);
        container.Register<MainShellViewModel>(Reuse.Singleton);

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
            ViewModelType = typeof(MainShellViewModel),
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
