// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura;
using DroidNet.Aura.Windowing;
using DroidNet.Composition;
using DroidNet.Config;
using DroidNet.Coordinates;
using DroidNet.Routing;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using DryIoc;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Oxygen.Editor.ContentExplorer;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.CookedInspector;
using Oxygen.Editor.Cooking;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Diagnostics;
using Oxygen.Editor.LivePreview;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Materials;
using Oxygen.Editor.Output;
using Oxygen.Editor.Problems;
using Oxygen.Editor.ProjectExplorer;
using Oxygen.Editor.ProjectExplorer.Activation;
using Oxygen.Editor.ProjectExplorer.ViewModels;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.SceneEditor;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.SceneExplorer;
using Oxygen.Editor.SceneInspector;
using Oxygen.Editor.Services;
using Oxygen.Editor.Shell;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Textures;
using Oxygen.Editor.Workspace;
using Oxygen.Managed.Core.Diagnostics;
using Oxygen.Managed.Core.Services;
using Serilog;

namespace Oxygen.Editor;

/// <summary>
///     The registrations of the editor scope, the root of every scope of the editor: the application's services, its
///     windows, the Project Explorer and the workspace window's view model, and the features each workspace window shows.
/// </summary>
/// <remarks>
///     The application host registers them into its container once it is built, before it initializes the settings and
///     runs; the topology tests compose the same registrations over a host-equivalent container.
/// </remarks>
public static class EditorModule
{
    // The editor-layer modules of the subsystems.
    private static readonly IScopeModule<IApplicationLayer>[] Modules =
    [
        new ShellModule(),
        new ProjectsModule(),
        new ProjectExplorerModule(),
        new ContentPipelineModule(),
        new LivePreviewModule(),
        new MaterialsModule(),
        new TexturesModule(),
    ];

    /// <summary>
    ///     Creates the routes of the application's root router: the Project Explorer and the workspace window.
    /// </summary>
    /// <returns>The routes.</returns>
    public static Routes CreateRoutes() => new(
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
                    Path = "pe",
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

    /// <summary>
    ///     Registers the editor scope's services into the host's container.
    /// </summary>
    /// <param name="container">The container the host built.</param>
    public static void Register(IContainer container)
    {
        ArgumentNullException.ThrowIfNull(container);

        // Register Config module
        _ = container.WithConfig();
        foreach (var module in Modules)
        {
            module.Register(container);
        }

        ConfigureApplicationServices(container);
        ConfigurePersistentStateDatabase(container);
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

        // Core services
        _ = container.WithSettings<IEngineStartupSettings, EngineStartupSettingsService>();
        container.Register<IOxygenPathFinder, OxygenPathFinder>(Reuse.Singleton);
        container.Register<NativeStorageProvider>(Reuse.Singleton);
        container.Register<IActivationService, ActivationService>(Reuse.Singleton);

        // Window placement service
        container.Register<WindowPlacementService, WindowPlacementService>(Reuse.Singleton);

        RegisterEditorDataServices(container);
        RegisterDiagnosticsServices(container);
        container.Register<IEngineService, EngineService>(Reuse.Singleton);

        // Transform gizmo snapping and space: per-user preferences kept across projects, serialized writes.
        container.RegisterDelegate(
            resolver => new TransformToolSettingsService(
                resolver.Resolve<IEditorSettingsManager>(),
                resolver.Resolve<ILoggerFactory>()),
            Reuse.Singleton);
        container.Register<EngineShutdownService>(Reuse.Singleton);
        container.RegisterDelegate<IHostedService>(resolver => resolver.Resolve<EngineShutdownService>(), Reuse.Singleton);

        ConfigureWindows(container);
        RegisterViewsAndViewModels(container);

        // Every scope of the editor is created under the root's handle.
        container.Register<ScopeFactory>(Reuse.Singleton);
        _ = ScopeFactory.CreateRoot(container);
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
        container.RegisterDelegate(context => context.Resolve<IStorageProvider>().AtomicFiles, Reuse.Singleton);

        container.Register<IProjectActivationCoordinator, ProjectActivationCoordinator>(Reuse.Singleton);
        container.Register<IProjectFolder, ActiveProjectFolder>(Reuse.Singleton);
    }

    private static void RegisterDiagnosticsServices(IContainer container)
    {
        container.Register<IStatusReducer, OperationStatusReducer>(Reuse.Singleton);
        container.Register<IExceptionDiagnosticAdapter, ExceptionDiagnosticAdapter>(Reuse.Singleton);
        container.Register<OperationResultStore>(Reuse.Singleton);
        container.RegisterDelegate<IOperationResultStore>(
            resolverContext => resolverContext.Resolve<OperationResultStore>(),
            Reuse.Singleton);
        container.RegisterDelegate<IOperationResultPublisher>(
            resolverContext => resolverContext.Resolve<OperationResultStore>(),
            Reuse.Singleton);
        container.Register<OperationResultOutputLogAdapter>(Reuse.Singleton);
        _ = container.Resolve<OperationResultOutputLogAdapter>();
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

        // Project Explorer window
        container.RegisterDelegate<Window>(
            resolverContext =>
            {
                var windowFactory = resolverContext.Resolve<IWindowFactory>();
                var category = new WindowCategory("wnd-pe");
                return windowFactory.CreateDecoratedWindow<RoutedWindow>(category).GetAwaiter().GetResult();
            },
            Reuse.Transient,
            serviceKey: new Target { Name = "wnd-pe" });
    }

    private static void RegisterViewsAndViewModels(IContainer container)
    {
        container.Register<MainShellViewModel>(Reuse.Transient);
        container.Register<MainShellView>(Reuse.Transient);

        container.Register<WorkspaceViewModel>(Reuse.Transient);
        container.Register<WorkspaceView>(Reuse.Transient);

        // The features of the workspace windows: each window registers their services in its own container.
        container.Register<IFeatureModule, ContentExplorerModule>(Reuse.Singleton);
        container.Register<IFeatureModule, CookedInspectorModule>(Reuse.Singleton);
        container.Register<IFeatureModule, CookingModule>(Reuse.Singleton);
        container.Register<IFeatureModule, MaterialEditorModule>(Reuse.Singleton);
        container.Register<IFeatureModule, OutputModule>(Reuse.Singleton);
        container.Register<IFeatureModule, ProblemsModule>(Reuse.Singleton);
        container.Register<IFeatureModule, SceneEditorModule>(Reuse.Singleton);
        container.Register<IFeatureModule, SceneExplorerModule>(Reuse.Singleton);
        container.Register<IFeatureModule, SceneInspectorModule>(Reuse.Singleton);
    }
}
