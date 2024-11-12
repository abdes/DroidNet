// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor;

using System;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using System.Reflection;
using System.Runtime.InteropServices;
using CommunityToolkit.Mvvm.DependencyInjection;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Oxygen.Editor.Core.Services;
using Oxygen.Editor.Data;
using Oxygen.Editor.Models;
using Oxygen.Editor.ProjectBrowser.Config;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.Projects.Config;
using Oxygen.Editor.Shell;
using Serilog;
using Testably.Abstractions;

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
    private static OxygenPathFinder? PathFinderService { get; set; }

    private static IFileSystem FileSystemService { get; } = new RealFileSystem();

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

        Log.Information("Setting up the host");

        try
        {
            var builder = Host.CreateDefaultBuilder(args);

            // Use DryIoc instead of the built-in service provider.
            _ = builder.UseServiceProviderFactory(new DryIocServiceProviderFactory(new Container()))

                // Add configuration files and configure Options, but first, initialize the IPathFinder instance, so it
                // can be used to resolve configuration file paths.
                .ConfigureAppConfiguration(
                    (_, config) =>
                    {
                        // Build a temporary config to get access to the command line arguments.
                        // NOTE: we expect the `--mode dev|real` optional argument.
                        var tempConfig = config.Build();
                        PathFinderService = CreatePathFinder(tempConfig);
                    })
                .ConfigureAppConfiguration(AddConfigurationFiles)
                .ConfigureServices(ConfigureOptionsPattern)
                .ConfigureServices(ConfigurePersistentStateDatabase)

                // Continue configuration using DryIoc container API. Configure early services, including Logging. Note
                // however, that before this point, Logger injection cannot be used.
                .ConfigureContainer<DryIocServiceProvider>(provider => ConfigureEarlyServices(provider.Container));

            // Add the WinUI User Interface hosted service as early as possible to allow the UI to start showing up
            // while you continue setting up other services not required for the UI.
            builder.Properties.Add(
                WinUiHostingExtensions.HostingContextKey,
                new HostingContext() { IsLifetimeLinked = true });
            _ = builder.ConfigureWinUI<App>();

            // Configure the rest of the application services
            _ = builder.ConfigureContainer<DryIocServiceProvider>(
                (_, provider) =>
                {
                    var container = provider.Container;
                    container.ConfigureApplicationServices();
                });

            var host = builder.Build();
            using (var scope = host.Services.CreateScope())
            {
                var db = scope.ServiceProvider.GetRequiredService<PersistentState>();
                db.Database.Migrate();
            }

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

    private static void ConfigureEarlyServices(IContainer container)
    {
        container.RegisterInstance(FileSystemService);
        container.RegisterInstance<IPathFinder>(PathFinderService!);
        container.RegisterInstance<IOxygenPathFinder>(PathFinderService!);
        container.ConfigureLogging();
        container.ConfigureRouter(MakeRoutes());

        // Setup the CommunityToolkit.Mvvm Ioc helper
        Ioc.Default.ConfigureServices(container);

        Debug.Assert(
            PathFinderService is not null,
            "did you forget to register the IPathFinder service?");
        Log.Information(
            $"Application `{PathFinderService.ApplicationName}` starting in `{PathFinderService.Mode}` mode");
    }

    private static OxygenPathFinder CreatePathFinder(IConfiguration configuration)
    {
#if DEBUG
        var mode = configuration["mode"] ?? PathFinder.DevelopmentMode;
#else
        var mode = configuration["mode"] ?? PathFinder.RealMode;
#endif
        var assembly = Assembly.GetEntryAssembly() ?? throw new CouldNotIdentifyMainAssemblyException();
        var companyName = GetAssemblyAttribute<AssemblyCompanyAttribute>(assembly)?.Company;
        var applicationName = GetAssemblyAttribute<AssemblyProductAttribute>(assembly)?.Product;

        var finderConfig = CreateFinderConfig(mode, companyName, applicationName);
        return new OxygenPathFinder(FileSystemService, finderConfig);
    }

    private static PathFinder.Config CreateFinderConfig(string mode, string? companyName, string? applicationName)
        => new(
            mode,
            companyName ?? throw new ArgumentNullException(nameof(companyName)),
            applicationName ?? throw new ArgumentNullException(applicationName));

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
                    ViewModelType = typeof(WorldEditor.ViewModels.WorkspaceViewModel),
                },
            ]),
        },
    ]);

    private static void AddConfigurationFiles(IConfigurationBuilder config)
    {
        Debug.Assert(PathFinderService is not null, "must setup the PathFinderService before adding config files");

        /* TODO: use the settings service */
        /* TODO: this path is plain wrong - need to embed the configs or find a way to copy them to the app root folder */

        var projectBrowserConfigPath = PathFinderService.GetProgramConfigFilePath(
            $"{Assembly.GetAssembly(typeof(ProjectBrowserSettings))!.GetName().Name}/Config/ProjectBrowser.config.json");

        var categoriesConfigPath = PathFinderService.GetProgramConfigFilePath(
            $"{Assembly.GetAssembly(typeof(ProjectsSettings))!.GetName().Name}/Config/Categories.config.json");

        // NOTE: Some settings classes may share the same configuration file, and only use a section
        // in it. We should only add the file once.
        var configFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            // TODO: PathFinderService.GetConfigFilePath(AppearanceSettings.ConfigFileName),
            PathFinderService.GetConfigFilePath("LocalSettings.json"),
            projectBrowserConfigPath,
            categoriesConfigPath,
        };

        foreach (var configFile in configFiles)
        {
            _ = config.AddJsonFile(configFile, optional: true, reloadOnChange: true);
        }
    }

    private static void ConfigureOptionsPattern(HostBuilderContext context, IServiceCollection sc)
    {
        var config = context.Configuration;
        _ = sc.Configure<ProjectBrowserSettings>(config.GetSection(ProjectBrowserSettings.ConfigSectionName));
        _ = sc.Configure<ProjectsSettings>(config.GetSection(ProjectsSettings.ConfigSectionName));

        // TODO: use the new appearance settings service
        _ = sc.Configure<ThemeSettings>(config.GetSection(nameof(ThemeSettings)));
    }

    private static void ConfigurePersistentStateDatabase(IServiceCollection sc)
    {
        Debug.Assert(PathFinderService is not null, "must setup the PathFinderService before adding config files");

        var localStateFolder = PathFinderService.LocalAppState;
        var dbPath = Path.Combine(localStateFolder, "state.db");
        _ = sc.AddSqlite<PersistentState>($"Data Source={dbPath}; Mode=ReadWriteCreate");
    }

    private static T? GetAssemblyAttribute<T>(Assembly assembly)
        where T : Attribute
        => (T?)Attribute.GetCustomAttribute(assembly, typeof(T));

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Roslynator",
        "RCS1194:Implement exception constructors",
        Justification = "This is a simple exception, used only in this class")]
    private sealed class CouldNotIdentifyMainAssemblyException : Exception;
}
