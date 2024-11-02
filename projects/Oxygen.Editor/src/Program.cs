// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor;

using System;
using System.Diagnostics.CodeAnalysis;
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

            // Setup injections for the Options pattern
            // Configure the DB Context for the persistent state
            _ = builder
                .ConfigureAppConfiguration(AddConfigurationFiles)
                .ConfigureServices(ConfigureOptionsPattern)
                .ConfigureServices(ConfigurePersistentStateDatabase);

            var host = builder
                .ConfigureWinUI<App>()
                .ConfigureContainer<DryIocServiceProvider>(
                    (_, serviceProvider) =>
                    {
                        var container = serviceProvider.Container;
                        container.ConfigureLogging();
                        container.ConfigureRouter(MakeRoutes());
                        container.ConfigureApplicationServices();

                        // Setup the CommunityToolkit.Mvvm Ioc helper
                        Ioc.Default.ConfigureServices(serviceProvider);
                    })
                .Build();

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
