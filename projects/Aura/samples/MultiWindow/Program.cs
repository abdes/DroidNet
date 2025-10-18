// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using System.Reactive.Linq;
using System.Runtime.InteropServices;
using DroidNet.Aura;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.WindowManagement;
using DroidNet.Bootstrap;
using DroidNet.Config;
using DroidNet.Routing;
using DroidNet.Samples.Aura.MultiWindow;
using DroidNet.Samples.WinPackagedApp;
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Serilog;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// The Main entry of the multi-window sample application.
/// </summary>
/// <remarks>
/// This sample demonstrates Aura's multi-window management capabilities including:
/// <list type="bullet">
/// <item>Creating and managing multiple windows</item>
/// <item>Window lifecycle events and tracking</item>
/// <item>Theme synchronization across windows</item>
/// <item>Different window types (Main, Tool, Document)</item>
/// </list>
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
            _ = bootstrap.Configure()
                .WithLoggingAbstraction()
                .WithConfiguration(
                    MakeConfigFiles,
                    ConfigureOptionsPattern)
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

    private static IEnumerable<string> MakeConfigFiles(IPathFinder finder, IFileSystem fs, IConfiguration config)
        =>
        [
            finder.GetConfigFilePath(AppearanceSettings.ConfigFileName),
        ];

    private static void ConfigureOptionsPattern(IConfiguration config, IServiceCollection sc)
        => _ = sc.Configure<AppearanceSettings>(config.GetSection(AppearanceSettings.ConfigSectionName));

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
                    Path = "demo",
                    ViewModelType = typeof(WindowManagerShellViewModel),
                },
            ]),
        },
    ]);

    private static void ConfigureApplicationServices(IContainer container)
    {
        // Register core services in DryIoc
        container.Register<ISettingsService<IAppearanceSettings>, AppearanceSettingsService>(Reuse.Singleton);
        container.Register<IAppThemeModeService, AppThemeModeService>(Reuse.Singleton);

        // Register multi-window management services
        // Note: We use Microsoft.Extensions.DependencyInjection extensions for registration
        var serviceCollection = new ServiceCollection();

        // Add Aura window management with backdrop service and decoration settings
        _ = serviceCollection.WithAura(options => options
            .WithBackdropService()
            .WithDecorationSettings());

        // Register menu providers using standard DI patterns
        _ = serviceCollection.RegisterMenus();

        // Register secondary window types as transient (Main window is singleton)
        _ = serviceCollection.AddWindow<ToolWindow>();
        _ = serviceCollection.AddWindow<DocumentWindow>();

        // Integrate with DryIoc container
        container.Populate(serviceCollection);

        // Configure Main window decoration with menu after DI integration
        ConfigureMainWindowDecoration(container);

        /*
         * Configure the Application's Windows.
         * The Main Window is THE single main window of the application - registered as singleton.
         * WindowManagerService can create additional Tool and Document windows dynamically.
         */

        // The Main Window is a singleton registered for the special Target.Main
        container.Register<Window, MainWindow>(Reuse.Singleton, serviceKey: Target.Main);

        // Views and ViewModels
        container.Register<MainShellView>(Reuse.Singleton);
        container.Register<MainShellViewModel>(Reuse.Singleton);

        // Multi-window demo ViewModels
        container.Register<WindowManagerShellViewModel>(Reuse.Singleton);
        container.Register<WindowManagerShellView>(Reuse.Singleton);
    }

    /// <summary>
    /// Configures the Main window decoration to use the application's main menu.
    /// </summary>
    /// <param name="container">The DI container.</param>
    /// <remarks>
    /// This method sets a category override for the Main window to include the menu provider.
    /// The override is applied to the WindowDecorationSettings after DI registration completes.
    /// When the router creates the MainWindow via Target.Main, the WindowManagerService will resolve
    /// the decoration from settings and apply the menu.
    /// </remarks>
    private static void ConfigureMainWindowDecoration(IContainer container)
    {
        // Resolve the settings service following Config module pattern
        var decorationSettingsService = container.Resolve<ISettingsService<IWindowDecorationSettings>>();

        // Configure Main window with menu using the builder pattern
        var mainWindowDecoration = WindowDecorationBuilder
            .ForMainWindow()
            .WithMenu(MenuConfiguration.MainMenuId, isCompact: false)
            .Build();

        // Access via .Settings property - ALL methods are in IWindowDecorationSettings interface
        decorationSettingsService.Settings.SetCategoryOverride(WindowCategory.Main, mainWindowDecoration);
    }
}
