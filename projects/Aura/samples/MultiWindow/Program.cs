// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;
using DroidNet.Aura;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Documents;
using DroidNet.Aura.Settings;
using DroidNet.Bootstrap;
using DroidNet.Config;
using DroidNet.Coordinates;
using DroidNet.Routing;
using DroidNet.Samples.WinPackagedApp;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
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
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "program entrypoint class must be public")]
public static partial class Program
{
    [LibraryImport("Microsoft.ui.xaml.dll")]
    [DefaultDllImportSearchPaths(DllImportSearchPath.SafeDirectories)]
    private static partial void XamlCheckProcessRequirements();

    [STAThread]
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "the Main method need to catch all")]
    private static void Main(string[] args)
    {
        // Ensures that the process can run XAML, and provides a deterministic error if a check
        // fails. Otherwise, it quietly does nothing.
        XamlCheckProcessRequirements();

        var bootstrap = new Bootstrapper(args);
        try
        {
            // Configure aned Build the host so the final container is available.
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

                // Configure Main window decoration with menu after DI integration
                ConfigureMainWindowDecoration(container);
            }
            catch (Exception ex) when (ex is ContainerException or InvalidOperationException)
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
        // Register Aura window management with all required services
        _ = container
            .WithAura(options => options
                .WithAppearanceSettings()
                .WithDecorationSettings()
                .WithBackdropService()
                .WithChromeService()
                .WithThemeModeService()
                .WithDrag())
            .WithSpatialMapping();

        // Register menu providers using standard DI patterns
        _ = container.RegisterMenus();

        // Register a minimal in-demo document service for the sample
        container.Register<IDocumentService, DemoDocumentService>(Reuse.Singleton);

        // Register secondary window types as transient (Main window is singleton)
        _ = container.AddWindow<Window>();
        _ = container.AddWindow<ToolWindow>();
        _ = container.AddWindow<DocumentWindow>();

        /*
         * Configure the Application's Windows.
         * The Main Window is THE single main window of the application - registered as singleton.
         * WindowManagerService can create additional Tool and Document windows dynamically.
         */

        // The Main Window is a singleton registered for the special Target.Main
        container.Register<Window, RoutedWindow>(Reuse.Singleton, serviceKey: Target.Main);

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
    /// <para>
    /// This method provides a default configuration for the Main window that will be used
    /// ONLY on the very first run when no configuration file exists yet. Once the user has
    /// a configuration file, their settings are preserved.
    /// </para>
    /// <para>
    /// The check for existing overrides happens AFTER the configuration file is loaded by
    /// the IOptionsMonitor system, so persisted user customizations (like isCompact=true)
    /// are properly respected.
    /// </para>
    /// <para>
    /// Important: Do NOT call SetCategoryOverride if an override already exists, as this
    /// will trigger the auto-save mechanism and overwrite the user's file.
    /// </para>
    /// </remarks>
    private static void ConfigureMainWindowDecoration(IContainer container)
    {
        // Resolve the settings service following Config module pattern
        var decorationSettingsService = container.Resolve<ISettingsService<IWindowDecorationSettings>>();

        // Important: At this point, IOptionsMonitor has already loaded the Aura.json file
        // if it exists, so CategoryOverrides will contain the persisted user settings.
        var hasOverride = decorationSettingsService.Settings.CategoryOverrides.ContainsKey(WindowCategory.Main);

        Log.Information(
            "ConfigureMainWindowDecoration: Main category override exists = {HasOverride}, Override count = {Count}",
            hasOverride,
            decorationSettingsService.Settings.CategoryOverrides.Count);

        if (hasOverride)
        {
            var existingMenu = decorationSettingsService.Settings.CategoryOverrides[WindowCategory.Main].Menu;
            Log.Information(
                "ConfigureMainWindowDecoration: Preserving existing menu settings - Provider = {Menu}, IsCompact = {IsCompact}",
                existingMenu?.MenuProviderId,
                existingMenu?.IsCompact);

            // IMPORTANT: Do NOT call SetCategoryOverride here! Just return and let the
            // existing settings be used. Calling SetCategoryOverride would trigger auto-save
            // and overwrite the user's configuration.
            return;
        }

        // No persisted override exists - this is the first run or the user deleted their config
        Log.Information("ConfigureMainWindowDecoration: No override found, creating default with isCompact = false");

        var mainWindowDecoration = WindowDecorationBuilder
            .ForMainWindow()
            .WithMenu(MenuConfiguration.MainMenuId, isCompact: false)
            .Build();

        // This will trigger auto-save after 5 seconds, creating the initial Aura.json file
        decorationSettingsService.Settings.SetCategoryOverride(WindowCategory.Main, mainWindowDecoration);
    }
}
