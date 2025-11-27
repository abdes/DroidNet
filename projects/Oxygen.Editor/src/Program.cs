// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;
using DroidNet.Aura;
using DroidNet.Aura.Windowing;
using DroidNet.Bootstrap;
using DroidNet.Config;
using DroidNet.Coordinates;
using DroidNet.Docking.Controls;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DryIoc;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.Core.Services;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Documents;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.ProjectBrowser.Views;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Services;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;
using Oxygen.Editor.WorldEditor.Workspace;
using Serilog;

namespace Oxygen.Editor;

/// <summary>
///     The Main entry of the application.
///     <para>
///         Overrides the usual WinUI XAML entry point in order to be able to control what exactly
///         happens at the entry point of the application. Customized here to build an application
///         <see cref="Host" /> and populate it with the default services (such as Configuration,
///         Logging, etc...) and a specialized <see cref="IHostedService" /> for running the User
///         Interface thread.
///     </para>
/// </summary>
/// <remarks>
///     <para>
///         Convenience hosting extension methods are used to simplify the setup of services needed
///         for the User Interface, logging, etc.
///     </para>
///     <para>
///         The WinUI service configuration supports customization, through a <see
///         cref="HostingContext" /> object placed in the <see
///         cref="IHostApplicationBuilder.Properties" /> of the host builder. Currently, the
///         IsLifetimeLinked property allows to specify if the User Interface thread lifetime is
///         linked to the application lifetime or not. When the two lifetimes are linked,
///         terminating either of them will result in terminating the other.
///     </para>
/// </remarks>
[ExcludeFromCodeCoverage]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "Program entry point must be public")]
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
            _ = bootstrap
                .Configure(options => options.WithOutputConsole())
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
                ConfigurePersistentStateDatabase(container);

                var dbPath = container.Resolve<IOxygenPathFinder>().StateDatabasePath;
                Log.Information("DB path: {DbPath}", dbPath);

                // Apply pending migrations to ensure database schema is up to date
                using var persistentState = container.Resolve<PersistentState>();
                persistentState.Database.MigrateAsync().GetAwaiter().GetResult();

                RegisterViewsAndViewModels(container);
            }
            catch (Exception ex) when (ex is ContainerException or InvalidOperationException)
            {
                // No EditorSettingsManager registered - skip initialization
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
            bootstrap.Dispose();
            Log.CloseAndFlush();
        }
    }

    private static Routes MakeRoutes() => new(
    [
        new Route
        {
            Path = string.Empty,
            MatchMethod = PathMatch.Prefix,
            ViewModelType = typeof(MainShellViewModel),
            Children = new Routes(
            [
                new Route
                {
                    Path = "we",
                    MatchMethod = PathMatch.Prefix,
                    ViewModelType = typeof(WorkspaceViewModel),
                },
                new Route
                {
                    Path = "pb",
                    MatchMethod = PathMatch.Prefix,
                    ViewModelType = typeof(MainViewModel),
                    Children = new Routes(
                    [
                        new Route
                        {
                            Path = "home",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(HomeViewModel),
                        },
                        new Route
                        {
                            Path = "new",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(NewProjectViewModel),
                        },
                        new Route
                        {
                            Path = "open",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(OpenProjectViewModel),
                        },
                        /* TODO: uncomment after settings page is refactored
                        new Route()
                        {
                            Path = "settings",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(SettingsViewModel),
                        },
                        */
                    ]),
                },
            ]),
        },
    ]);

    private static void InitializeSettings(DryIoc.IContainer container)
    {
        // Register Config module
        _ = container.WithConfig();

        // If the bootstrapper included `Config` module, it will have added a EditorSettingsManager.
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

    // Register PersistentState to be created lazily so IOxygenPathFinder is resolved later
    private static void ConfigurePersistentStateDatabase(IContainer container)
        => container.RegisterDelegate(
            resolver =>
            {
                var path = resolver.Resolve<IOxygenPathFinder>().StateDatabasePath;
                Log.Information("Configuring PersistentState database at path: {DbPath}", path);
                return new PersistentState(
                    new DbContextOptionsBuilder<PersistentState>()
                        .UseLoggerFactory(resolver.Resolve<ILoggerFactory>())
                        .EnableDetailedErrors()
                        .EnableSensitiveDataLogging()
                        .UseSqlite($"Data Source={resolver.Resolve<IOxygenPathFinder>().StateDatabasePath}; Mode=ReadWriteCreate")
                    .Options);
            },
            Reuse.Transient);

    private static void ConfigureApplicationServices(this IContainer container)
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

        // Core services
        container.Register<IOxygenPathFinder, OxygenPathFinder>(Reuse.Singleton);
        container.Register<NativeStorageProvider>(Reuse.Singleton);
        container.Register<IActivationService, ActivationService>(Reuse.Singleton);

        // Window placement service
        container.Register<WindowPlacementService, WindowPlacementService>(Reuse.Singleton);

        RegisterEditorDataServices(container);
        container.Register<IEngineService, EngineService>(Reuse.Singleton);

        /*
         * Set up the view model to view converters. We're using the standard
         * converter, and a custom one with fall back if the view cannot be located.
         */
        container.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        container.Register<IValueConverter, ViewModelToView>(Reuse.Singleton, serviceKey: "VmToView");

        container.Register<DockPanelViewModel>(Reuse.Transient);
        container.Register<DockPanel>(Reuse.Transient);

        ConfigureWindows(container);
        RegisterViewsAndViewModels(container);
    }

    private static void RegisterEditorDataServices(IContainer container)
    {
        container.RegisterDelegate<IMemoryCache>(_ => new MemoryCache(new MemoryCacheOptions()), Reuse.Singleton, setup: Setup.With(preventDisposal: true));

        container.Register<IEditorSettingsManager, EditorSettingsManager>(Reuse.Singleton);
        container.Register<IProjectUsageService, ProjectUsageService>(Reuse.Singleton);
        container.Register<ITemplateUsageService, TemplateUsageService>(Reuse.Singleton);

        // TODO: use keyed registration and parameter name to key mappings
        // https://github.com/dadhi/DryIoc/blob/master/docs/DryIoc.Docs/SpecifyDependencyAndPrimitiveValues.md#complete-example-of-matching-the-parameter-name-to-the-service-key
        container.Register<IStorageProvider, NativeStorageProvider>(Reuse.Singleton);

        // Register the universal template source with NO key, so it gets selected when injected an
        // instance of ITemplateSource. Register specific template source implementations KEYED.
        // They are injected only as a collection of implementation instances, only by the universal
        // source.
        container.Register<ITemplatesSource, UniversalTemplatesSource>(Reuse.Singleton);
        container.Register<ITemplatesSource, LocalTemplatesSource>(Reuse.Singleton, serviceKey: Uri.UriSchemeFile);
        container.Register<ITemplatesService, TemplatesService>(Reuse.Transient);

        container.Register<IProjectBrowserService, ProjectBrowserService>(Reuse.Singleton);
        container.Register<IProjectManagerService, ProjectManagerService>(Reuse.Singleton);
        container.Register<IDocumentService, EditorDocumentService>(Reuse.Singleton);

        // Register the project instance using a delegate that will request the currently open project from the project browser service.
        container.RegisterDelegate(resolverContext => resolverContext.Resolve<IProjectManagerService>().CurrentProject);
    }

    private static void ConfigureWindows(IContainer container)
    {
        /*
         * Configure the Application's Windows. Each window represents a target in which to open the
         * requested url. Now we create the instances via the registered IWindowFactory and annotate
         * them with the window category that matches the target Name. This enables the Aura window
         * manager to apply category-specific decoration.
         */

        // In Oxygen Editor, we don't really have a "Main" window - all windows are created equally.
        container.Register<RoutedWindow>(Reuse.Transient);

        // Workspace window
        container.RegisterDelegate<Window>(
            resolverContext =>
            {
                var windowFactory = resolverContext.Resolve<IWindowFactory>();
                var category = new WindowCategory("wnd-we");
                return windowFactory.CreateDecoratedWindow<RoutedWindow>(category).GetAwaiter().GetResult();
            },
            Reuse.Transient,
            serviceKey: new Target { Name = "wnd-we" });

        // Project Browser window
        container.RegisterDelegate<Window>(
            resolverContext =>
            {
                var windowFactory = resolverContext.Resolve<IWindowFactory>();
                var category = new WindowCategory("wnd-pb");
                return windowFactory.CreateDecoratedWindow<RoutedWindow>(category).GetAwaiter().GetResult();
            },
            Reuse.Transient,
            serviceKey: new Target { Name = "wnd-pb" });
    }

    private static void RegisterViewsAndViewModels(IContainer container)
    {
        container.Register<MainShellViewModel>(Reuse.Transient);
        container.Register<MainShellView>(Reuse.Transient);

        container.Register<MainViewModel>(Reuse.Transient);
        container.Register<MainView>(Reuse.Transient);
        container.Register<HomeViewModel>(Reuse.Transient);
        container.Register<HomeView>(Reuse.Transient);
        container.Register<NewProjectViewModel>(Reuse.Transient);
        container.Register<NewProjectView>(Reuse.Transient);
        container.Register<OpenProjectViewModel>(Reuse.Transient);
        container.Register<OpenProjectView>(Reuse.Transient);

        container.Register<WorkspaceViewModel>(Reuse.Transient);
        container.Register<WorkspaceView>(Reuse.Transient);
    }
}
