// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor;

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO.Abstractions;
using System.Reflection;
using System.Runtime.InteropServices;
using CommunityToolkit.Mvvm.DependencyInjection;
using DroidNet.Config;
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
using Oxygen.Editor.Pages.Settings.ViewModels;
using Oxygen.Editor.Pages.Settings.Views;
using Oxygen.Editor.ProjectBrowser.Config;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Services;
using Oxygen.Editor.ProjectBrowser.Storage;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.Services;
using Oxygen.Editor.Storage.Native;
using Oxygen.Editor.ViewModels;
using Oxygen.Editor.Views;
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
        // Register core services
        sp.Container.Register<IFileSystem, RealFileSystem>(Reuse.Singleton);
        sp.Container.Register<NativeStorageProvider>(Reuse.Singleton);
        sp.Container.Register<IPathFinder, DevelopmentPathFinder>(Reuse.Singleton); // TODO: release version
        sp.Container.Register<IThemeSelectorService, ThemeSelectorService>(Reuse.Singleton);
        sp.Container.Register<INavigationService, NavigationService>(Reuse.Singleton);
        sp.Container.Register<IAppNotificationService, AppNotificationService>(Reuse.Singleton);
        sp.Container.Register<IPageService, PageService>(Reuse.Singleton);
        sp.Container.Register<IActivationService, ActivationService>(Reuse.Singleton);

        // Register domain specific services, which serve data required by the views
        sp.Container.Register<IKnownLocationsService, KnownLocationsService>(Reuse.Singleton);
        sp.Container.Register<LocalTemplatesSource>(Reuse.Singleton);
        sp.Container.Register<ITemplatesSource, TemplatesSource>(Reuse.Singleton);
        sp.Container.Register<ITemplatesService, TemplatesService>(Reuse.Singleton);
        sp.Container.Register<LocalProjectsSource>(Reuse.Singleton);
        sp.Container.Register<IProjectSource, UniversalProjectSource>(Reuse.Singleton);
        sp.Container.Register<IProjectsService, ProjectsService>(Reuse.Singleton);

        // Set up the view model to view converters. We're using the standard converter, and a custom one with fall back
        // if the view cannot be located.
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

        // Views and ViewModels
        sp.Container.Register<ShellViewModel>(Reuse.Transient);
        sp.Container.Register<SettingsViewModel>(Reuse.Transient);
        sp.Container.Register<StartHomeViewModel>(Reuse.Singleton);
        sp.Container.Register<StartNewViewModel>(Reuse.Transient);
        sp.Container.Register<StartOpenViewModel>(Reuse.Singleton);
        sp.Container.Register<StartViewModel>(Reuse.Singleton);
        sp.Container.Register<MainViewModel>(Reuse.Transient);
        sp.Container.Register<SettingsPage>(Reuse.Transient);
        sp.Container.Register<ShellPage>(Reuse.Transient);
        sp.Container.Register<MainPage>(Reuse.Transient);
    }

    private static Routes MakeRoutes() => new(
    [
        new Route
        {
            Path = string.Empty,
            MatchMethod = PathMatch.StrictPrefix,
            ViewModelType = typeof(ShellViewModel),
        },
    ]);

    private static void AddConfigurationFiles(HostBuilderContext context, IConfigurationBuilder config)
    {
        _ = context; // unused

        var localSettingsPath = Path.GetFullPath("LocalSettings.json", Finder.LocalAppData);
        var projectBrowserConfigPath = Path.GetFullPath(
            $"{Assembly.GetAssembly(typeof(ProjectBrowserSettings))!.GetName().Name}/Config/ProjectBrowser.config.json",
            Finder.ProgramData);
        _ = config.AddJsonFile(localSettingsPath, optional: true)
            .AddJsonFile(projectBrowserConfigPath);
    }

    private static void ConfigureOptionsPattern(HostBuilderContext context, IServiceCollection sc)
    {
        _ = sc.Configure<ProjectBrowserSettings>(
            context.Configuration.GetSection(ProjectBrowserSettings.ConfigSectionName));
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
