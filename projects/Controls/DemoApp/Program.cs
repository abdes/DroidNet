// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Runtime.InteropServices;
using DroidNet.Aura;
using DroidNet.Bootstrap;
using DroidNet.Config;
using DroidNet.Controls.Demo.DemoBrowser;
using DroidNet.Controls.Demo.DynamicTree;
using DroidNet.Controls.Demo.InPlaceEdit;
using DroidNet.Controls.Demo.Menus;
using DroidNet.Controls.Demo.OutputConsole;
using DroidNet.Controls.Demo.OutputLog;
using DroidNet.Controls.Demo.TabStrip;
using DroidNet.Hosting;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Serilog;

namespace DroidNet.Controls.Demo;

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
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "program entrypoint class must be public")]
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
            // Configure aned Build the host so the final container is available.
            _ = bootstrap.Configure(
                options => options
                    .WithOutputConsole()
                    .WithOutputLog(formatProvider: CultureInfo.InvariantCulture))
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

        container.Register<MainShellView>(Reuse.Singleton);
        container.Register<MainShellViewModel>(Reuse.Singleton);

        container.Register<DemoBrowserView>(Reuse.Singleton);
        container.Register<DemoBrowserViewModel>(Reuse.Singleton);
        container.Register<OutputLogDemoView>(Reuse.Transient);
        container.Register<OutputLogDemoViewModel>(Reuse.Transient);
        container.Register<OutputConsoleDemoView>(Reuse.Transient);
        container.Register<OutputConsoleDemoViewModel>(Reuse.Singleton);
        container.Register<ProjectLayoutView>(Reuse.Transient);
        container.Register<ProjectLayoutViewModel>(Reuse.Transient);
        container.Register<InPlaceEditDemoView>(Reuse.Transient);
        container.Register<InPlaceEditDemoViewModel>(Reuse.Transient);
        container.Register<NumberBoxDemoView>(Reuse.Transient);
        container.Register<NumberBoxDemoViewModel>(Reuse.Transient);
        container.Register<VectorBoxDemoView>(Reuse.Transient);
        container.Register<VectorBoxDemoViewModel>(Reuse.Transient);
        container.Register<MenuBarDemoView>(Reuse.Transient);
        container.Register<MenuBarDemoViewModel>(Reuse.Transient);
        container.Register<MenuFlyoutDemoView>(Reuse.Transient);
        container.Register<MenuFlyoutDemoViewModel>(Reuse.Transient);
        container.Register<MenuItemDemoView>(Reuse.Transient);
        container.Register<MenuItemDemoViewModel>(Reuse.Transient);
        container.Register<TabStripDemoView>(Reuse.Transient);
        container.Register<TabStripDemoViewModel>(Reuse.Transient);
    }

    [SuppressMessage("Design", "MA0051:Method is too long", Justification = "methoid gains in clarity iof all routes are together")]
    private static Routes MakeRoutes() => new(
    [
        new Route
        {
            // We want the shell view to be loaded no matter what absolute URL we are navigating to.
            // So, it has an empty path with a Prefix matching type, which will always match and not
            // consume a segment.
            Path = string.Empty,
            MatchMethod = PathMatch.Prefix,
            ViewModelType = typeof(MainShellViewModel),
            Children = new Routes(
            [
                new Route
                {
                    Path = "demos",
                    MatchMethod = PathMatch.Prefix,
                    ViewModelType = typeof(DemoBrowserViewModel),
                    Children = new Routes(
                    [
                        new Route()
                        {
                            Path = "output-log",
                            ViewModelType = typeof(OutputLogDemoViewModel),
                        },
                        new Route()
                        {
                            Path = "output-console",
                            ViewModelType = typeof(OutputConsoleDemoViewModel),
                        },
                        new Route()
                        {
                            Path = "in-place",
                            ViewModelType = typeof(InPlaceEditDemoViewModel),
                        },
                        new Route()
                        {
                            Path = "numberbox",
                            ViewModelType = typeof(NumberBoxDemoViewModel),
                        },
                        new Route()
                        {
                            Path = "vectorbox",
                            ViewModelType = typeof(VectorBoxDemoViewModel),
                        },
                        new Route()
                        {
                            Path = "dynamic-tree",
                            ViewModelType = typeof(ProjectLayoutViewModel),
                        },
                        new Route()
                        {
                            Path = "menubar",
                            ViewModelType = typeof(MenuBarDemoViewModel),
                        },
                        new Route()
                        {
                            Path = "menuflyout",
                            ViewModelType = typeof(MenuFlyoutDemoViewModel),
                        },
                        new Route()
                        {
                            Path = "menuitem",
                            ViewModelType = typeof(MenuItemDemoViewModel),
                        },
                        new Route()
                        {
                            Path = "tabstrip",
                            ViewModelType = typeof(TabStripDemoViewModel),
                        },
                    ]),
                },
            ]),
        },
    ]);
}
