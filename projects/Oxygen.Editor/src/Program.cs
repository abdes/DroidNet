// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using System.Runtime.InteropServices;
using DroidNet.Bootstrap;
using DroidNet.Config;
using DroidNet.Docking.Controls;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DryIoc;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.Core.Services;
using Oxygen.Editor.Data;
using Oxygen.Editor.Models;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.ProjectBrowser.Views;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Services;
using Oxygen.Editor.Shell;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;
using Oxygen.Editor.WorldEditor.ContentBrowser;
using Oxygen.Editor.WorldEditor.ProjectExplorer;
using Oxygen.Editor.WorldEditor.ViewModels;
using Oxygen.Editor.WorldEditor.Views;
using Serilog;

namespace Oxygen.Editor;

/// <summary>
/// The Main entry of the application.
/// <para>
/// Overrides the usual WinUI XAML entry point in order to be able to control what exactly happens
/// at the entry point of the application. Customized here to build an application <see cref="Host" />
/// and populate it with the default services (such as Configuration, Logging, etc...) and a
/// specialized <see cref="IHostedService" /> for running the User Interface thread.
/// </para>
/// </summary>
/// <remarks>
/// <para>
/// Convenience hosting extension methods are used to simplify the setup of services needed for the
/// User Interface, logging, etc.
/// </para>
/// <para>
/// The WinUI service configuration supports customization, through a <see cref="HostingContext" />
/// object placed in the <see cref="IHostApplicationBuilder.Properties" /> of the host builder.
/// Currently, the IsLifetimeLinked property allows to specify if the User Interface thread lifetime
/// is linked to the application lifetime or not. When the two lifetimes are linked, terminating
/// either of them will result in terminating the other.
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
            _ = bootstrap.Configure()
                .WithConfiguration(AddConfigurationFiles, ConfigureOptionsPattern)
                .WithLoggingAbstraction()
                .WithMvvm()
                .WithRouting(MakeRoutes())
                .WithWinUI<App>()
                .WithAppServices(ConfigureApplicationServices)
                .WithAppServices(ConfigurePersistentStateDatabase)
                .WithAppServices(RegisterViewsAndViewModels);

            _ = bootstrap.Build();

            var db = bootstrap.Container.Resolve<PersistentState>();
            db.Database.Migrate();

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
            ViewModelType = typeof(ShellViewModel),
            Children = new Routes(
            [
                new Route()
                {
                    // The project browser is the root of a navigation view with multiple pages.
                    Path = "pb",
                    ViewModelType = typeof(MainViewModel),
                    Children = new Routes(
                    [
                        new Route()
                        {
                            Path = "home",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(HomeViewModel),
                        },
                        new Route()
                        {
                            Path = "new",
                            MatchMethod = PathMatch.Prefix,
                            ViewModelType = typeof(NewProjectViewModel),
                        },
                        new Route()
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
                new Route()
                {
                    // The project browser is the root of a navigation view with multiple pages.
                    Path = "we",
                    ViewModelType = typeof(WorkspaceViewModel),
                },
            ]),
        },
    ]);

    private static IEnumerable<string> AddConfigurationFiles(
        IPathFinder finder,
        IFileSystem fileSystem,
        IConfiguration config)
    {
        Debug.Assert(finder is not null, "must setup the PathFinderService before adding config files");

        return
        [
            /* TODO: PathFinderService.GetConfigFilePath(AppearanceSettings.ConfigFileName),*/

            finder.GetConfigFilePath("LocalSettings.json"),
        ];
    }

    private static void ConfigureOptionsPattern(IConfiguration config, IServiceCollection sc) =>

        // TODO: use the new appearance settings service
        _ = sc.Configure<ThemeSettings>(config.GetSection(nameof(ThemeSettings)));

    private static void ConfigurePersistentStateDatabase(IContainer container)
    {
        container.RegisterInstance(
            new DbContextOptionsBuilder<PersistentState>()
                .UseLoggerFactory(container.Resolve<ILoggerFactory>())
                .EnableDetailedErrors()
                .EnableSensitiveDataLogging()
                .UseSqlite(
                    $"Data Source={container.Resolve<IOxygenPathFinder>().StateDatabasePath}; Mode=ReadWriteCreate")
                .Options);
        container.Register<PersistentState>(Reuse.Singleton);
    }

    private static void ConfigureApplicationServices(this IContainer container)
    {
        /*
         * Register core services.
         */

        container.Register<IOxygenPathFinder, OxygenPathFinder>(Reuse.Singleton);
        container.Register<NativeStorageProvider>(Reuse.Singleton);
        container.Register<IActivationService, ActivationService>(Reuse.Singleton);

        /*
         * Register domain specific services.
         */

        // TODO: use keyed registration and parameter name to key mappings
        // https://github.com/dadhi/DryIoc/blob/master/docs/DryIoc.Docs/SpecifyDependencyAndPrimitiveValues.md#complete-example-of-matching-the-parameter-name-to-the-service-key
        container.Register<IStorageProvider, NativeStorageProvider>(Reuse.Singleton);

        // Register the universal template source with NO key, so it gets selected when injected an instance of ITemplateSource.
        // Register specific template source implementations KEYED. They are injected only as a collection of implementation
        // instances, only by the universal source.
        container.Register<ITemplatesSource, UniversalTemplatesSource>(Reuse.Singleton);
        container.Register<ITemplatesSource, LocalTemplatesSource>(Reuse.Singleton, serviceKey: Uri.UriSchemeFile);
        container.Register<ITemplatesService, TemplatesService>(Reuse.Transient);

        container.Register<ISettingsManager, SettingsManager>(Reuse.Singleton);
        container.Register<IProjectBrowserService, ProjectBrowserService>(Reuse.Transient);
        container.Register<IProjectManagerService, ProjectManagerService>(Reuse.Singleton);

        // Register the project instance using a delegate that will request the currently open project from the project
        // browser service.
        container.RegisterDelegate(resolverContext => resolverContext.Resolve<IProjectManagerService>().CurrentProject);

        /*
         * Set up the view model to view converters. We're using the standard converter, and a custom one with fall back
         * if the view cannot be located.
         */

        container.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        container.Register<IValueConverter, ViewModelToView>(Reuse.Singleton, serviceKey: "VmToView");

        container.Register<DockPanelViewModel>(Reuse.Transient);
        container.Register<DockPanel>(Reuse.Transient);

        /*
         * Configure the Application's Windows. Each window represents a target in which to open the requested url. The
         * target name is the key used when registering the window type.
         *
         * There should always be a Window registered for the special target <c>_main</c>.
         */

        // The Main Window is a singleton and its content can be re-assigned as needed. It is registered with a key that
        // corresponding to name of the special target <see cref="Target.Main" />.
        container.Register<Window, MainWindow>(Reuse.Transient, serviceKey: Target.Main);

        // Views and ViewModels
        RegisterViewsAndViewModels(container);
    }

    private static void RegisterViewsAndViewModels(IContainer container)
    {
        container.Register<ShellViewModel>(Reuse.Transient);
        container.Register<ShellView>(Reuse.Transient);

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
        container.Register<SceneDetailsView>(Reuse.Transient);
        container.Register<SceneDetailsViewModel>(Reuse.Transient);
        container.Register<RendererView>(Reuse.Transient);
        container.Register<RendererViewModel>(Reuse.Transient);
        container.Register<LogsView>(Reuse.Transient);
        container.Register<LogsViewModel>(Reuse.Transient);
        container.Register<ProjectExplorerView>(Reuse.Transient);
        container.Register<ProjectExplorerViewModel>(Reuse.Transient);
        container.Register<ContentBrowserView>(Reuse.Transient);
        container.Register<ContentBrowserViewModel>(Reuse.Transient);
    }
}
