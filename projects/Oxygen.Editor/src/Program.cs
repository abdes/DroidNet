// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor;

using System;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO.Abstractions;
using System.Reflection;
using System.Runtime.InteropServices;
using CommunityToolkit.Mvvm.DependencyInjection;
using DroidNet.Config;
using DroidNet.Docking.Controls;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.Core.Services;
using Oxygen.Editor.Data;
using Oxygen.Editor.Models;
using Oxygen.Editor.ProjectBrowser.Config;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Config;
using Oxygen.Editor.Projects.Storage;
using Oxygen.Editor.Services;
using Oxygen.Editor.Shell;
using Oxygen.Editor.Storage;
using Oxygen.Editor.Storage.Native;
using Oxygen.Editor.WorldEditor.ProjectExplorer;
using Oxygen.Editor.WorldEditor.Views;
using Serilog;
using Serilog.Events;
using Serilog.Templates;
using Testably.Abstractions;

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
/// <see cref="IHostApplicationBuilder.Properties" /> of the host builder. Currently, the IsLifetimeLinked property allows
/// to specify if the User Interface thread lifetime is linked to the application lifetime or not. When the two lifetimes are
/// linked, terminating either of them will result in terminating the other.
/// </para>
/// </remarks>
[ExcludeFromCodeCoverage]
public static partial class Program
{
    private static readonly IPathFinder Finder = new DevelopmentPathFinder(new RealFileSystem());

    /// <summary>
    /// Ensures that the process can run XAML, and provides a deterministic error if a
    /// check fails. Otherwise, it quietly does nothing.
    /// </summary>
    [LibraryImport("Microsoft.ui.xaml.dll")]
    private static partial void XamlCheckProcessRequirements();

    [STAThread]
    private static void Main(string[] args)
    {
        XamlCheckProcessRequirements();

        ConfigureLogger();

        Log.Information("Setting up the host");

        try
        {
            var builder = Host.CreateDefaultBuilder(args);

            // Use DryIoc instead of the built-in service provider.
            _ = builder.UseServiceProviderFactory(new DryIocServiceProviderFactory(new Container()));

            // Add the WinUI User Interface hosted service as early as possible to allow the UI to start showing up
            // while you continue setting up other services not required for the UI.
            builder.Properties.Add(
                DroidNet.Hosting.WinUI.HostingExtensions.HostingContextKey,
                new HostingContext() { IsLifetimeLinked = true });

            var host = builder
                .ConfigureAppConfiguration(AddConfigurationFiles)
                .ConfigureLogging()
                .ConfigureWinUI<App>()
                .ConfigureRouter(MakeRoutes())
                .ConfigureAutoInjected()

                // Setup injections for the Options pattern
                .ConfigureServices(ConfigureOptionsPattern)

                // Configure the DB Context for the persistent state
                .ConfigureServices(ConfigurePersistentStateDatabase)

                // Continue setting up the DI container using DryIo API for more flexibility
                .ConfigureContainer<DryIocServiceProvider>(RegisterApplicationServices)

                // Build the host
                .Build();

            using (var scope = host.Services.CreateScope())
            {
                var db = scope.ServiceProvider.GetRequiredService<PersistentState>();
                db.Database.Migrate();
            }

            Ioc.Default.ConfigureServices(host.Services);

            // Finally start the host. This will block until the application lifetime is terminated through CTRL+C,
            // closing the UI windows or programmatically.
            host.Run();
        }
        catch (Exception ex)
        {
            Log.Fatal(ex, "Host terminated unexpectedly");
        }
        finally
        {
            Log.CloseAndFlush();
        }
    }

    /// <summary>
    /// Use Serilog, but decouple the logging clients from the implementation by using the generic <see cref="ILogger" /> instead
    /// of Serilog's ILogger.
    /// </summary>
    /// <seealso href="https://nblumhardt.com/2021/06/customize-serilog-text-output/" />
    private static void ConfigureLogger() =>
        Log.Logger = new LoggerConfiguration()
            .MinimumLevel.Debug()
            .MinimumLevel.Override("Microsoft", LogEventLevel.Information)
            .Enrich.FromLogContext()
            .WriteTo.Debug(
                new ExpressionTemplate(
                    "[{@t:HH:mm:ss} {@l:u3} ({Substring(SourceContext, LastIndexOf(SourceContext, '.') + 1)})] {@m:lj}\n{@x}",
                    new CultureInfo("en-US")))
            /* .WriteTo.Seq("http://localhost:5341/") */
            .CreateLogger();

    private static void RegisterApplicationServices(HostBuilderContext context, DryIocServiceProvider sp)
    {
        /*
         * Register core services.
         */

        sp.Container.Register<IFileSystem, RealFileSystem>(Reuse.Singleton);
        sp.Container.Register<NativeStorageProvider>(Reuse.Singleton);
        sp.Container.Register<IPathFinder, DevelopmentPathFinder>(Reuse.Singleton); // TODO: release version
        sp.Container.Register<IThemeSelectorService, ThemeSelectorService>(Reuse.Singleton);
        sp.Container.Register<IAppNotificationService, AppNotificationService>(Reuse.Singleton);
        sp.Container.Register<IActivationService, ActivationService>(Reuse.Singleton);

        /*
         * Register domain specific services.
         */

        // Register the universal template source with NO key, so it gets selected when injected an instance of ITemplateSource.
        // Register specific template source implementations KEYED. They are injected only as a collection of implementation
        // instances, only by the universal source.
        sp.Container.Register<ITemplatesSource, UniversalTemplatesSource>(reuse: Reuse.Singleton);
        sp.Container.Register<ITemplatesSource, LocalTemplatesSource>(
            reuse: Reuse.Singleton,
            serviceKey: Uri.UriSchemeFile);
        sp.Container.Register<ITemplatesService, TemplatesService>(Reuse.Singleton);

        // TODO: use keyed registration and parameter name to key mappings
        // https://github.com/dadhi/DryIoc/blob/master/docs/DryIoc.Docs/SpecifyDependencyAndPrimitiveValues.md#complete-example-of-matching-the-parameter-name-to-the-service-key
        sp.Container.Register<IStorageProvider, NativeStorageProvider>(Reuse.Singleton);
        sp.Container.Register<LocalProjectsSource>(Reuse.Singleton);
        sp.Container.Register<IProjectSource, UniversalProjectSource>(Reuse.Singleton);
        sp.Container.Register<IProjectBrowserService, ProjectBrowserService>(Reuse.Singleton);
        sp.Container.Register<IProjectManagerService, ProjectManagerService>(Reuse.Singleton);

        // Register the project instance using a delegate that will request the currently open project from the project
        // browser service.
        sp.Container.RegisterDelegate(
            resolverContext => resolverContext.Resolve<IProjectManagerService>().CurrentProject);

        /*
         * Set up the view model to view converters. We're using the standard converter, and a custom one with fall back
         * if the view cannot be located.
         */

        sp.Container.Register<IViewLocator, DefaultViewLocator>(Reuse.Singleton);
        sp.Container.Register<IValueConverter, ViewModelToView>(Reuse.Singleton, serviceKey: "VmToView");

        /*
         * Configure the Application's Windows. Each window represents a target in which to open the requested url. The
         * target name is the key used when registering the window type.
         *
         * There should always be a Window registered for the special target <c>_main</c>.
         */

        // The Main Window is a singleton and its content can be re-assigned as needed. It is registered with a key that
        // corresponding to name of the special target <see cref="Target.Main" />.
        sp.Container.Register<Window, MainWindow>(Reuse.Singleton, serviceKey: Target.Main);

        // Views and ViewModels that are not auto-registered for injection
        // TODO(abdes): refactor into extension method
        sp.Container.Register<DockPanelViewModel>(Reuse.Transient);
        sp.Container.Register<DockPanel>(Reuse.Transient);
        sp.Container.Register<ProjectExplorerViewModel>(Reuse.Transient);
        sp.Container.Register<ProjectExplorerView>(Reuse.Transient);
    }

    private static Routes MakeRoutes() => new(
    [
        new Route
        {
            Path = string.Empty,
            MatchMethod = PathMatch.StrictPrefix,
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
                    ViewModelType = typeof(WorldEditor.ViewModels.WorkspaceViewModel),
                },
            ]),
        },
    ]);

    private static void AddConfigurationFiles(HostBuilderContext context, IConfigurationBuilder config)
    {
        _ = context; // unused

        var localSettingsPath = Path.GetFullPath("LocalSettings.json", Finder.LocalAppData);
        var projectBrowserConfigPath = Path.GetFullPath(
            $"{Assembly.GetAssembly(typeof(ProjectBrowserSettings))!.GetName().Name}/Config/ProjectBrowser.config.json",
            Finder.ProgramData);
        var categoriesConfigPath = Path.GetFullPath(
            $"{Assembly.GetAssembly(typeof(ProjectsSettings))!.GetName().Name}/Config/Categories.config.json",
            Finder.ProgramData);

        _ = config.AddJsonFile(localSettingsPath, optional: true)
            .AddJsonFile(projectBrowserConfigPath)
            .AddJsonFile(categoriesConfigPath);
    }

    private static void ConfigureOptionsPattern(HostBuilderContext context, IServiceCollection sc)
    {
        _ = sc.Configure<ProjectBrowserSettings>(
            context.Configuration.GetSection(ProjectBrowserSettings.ConfigSectionName));
        _ = sc.Configure<ProjectsSettings>(context.Configuration.GetSection(ProjectsSettings.ConfigSectionName));
        _ = sc.ConfigureWritable<ThemeSettings>(
            context.Configuration.GetSection(nameof(ThemeSettings)),
            Path.Combine(Finder.LocalAppData, "LocalSettings.json"));
    }

    private static void ConfigurePersistentStateDatabase(HostBuilderContext context, IServiceCollection sc)
    {
        _ = context; // unused

        var localStateFolder = Finder.LocalAppState;
        var dbPath = Path.Combine(localStateFolder, "state.db");
        _ = sc.AddSqlite<PersistentState>($"Data Source={dbPath}; Mode=ReadWriteCreate");
    }
}
