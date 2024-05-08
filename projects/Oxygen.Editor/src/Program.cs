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
using DryIoc;
using DryIoc.Microsoft.DependencyInjection;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
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
/// The WinUI service configuration supports customization, through a <see cref="HostingContext" /> object placed in the <see cref="IHostApplicationBuilder.Properties" /> of the host builder. Currently, the IsLifetimeLinked property allows to specify
/// if the User Interface thread lifetime is linked to the application lifetime or not. When the two lifetimes are linked,
/// terminating either of them will result in terminating the other.
/// </para>
/// </remarks>
[ExcludeFromCodeCoverage]
public static partial class Program
{
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

        // Use Serilog, but decouple the logging clients from the implementation by using the generic
        // Microsoft.Extensions.Logging.ILogger instead of Serilog's ILogger.
        // https://nblumhardt.com/2021/06/customize-serilog-text-output/
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

        Log.Information("Setting up the host");

        try
        {
            var fs = new RealFileSystem();
            var finder = new DevelopmentPathFinder(fs);

            // Use a default application host builder, which comes with logging, configuration providers for environment
            // variables, command line, 'appsettings.json' and secrets.
            var builder = Host.CreateDefaultBuilder(args);

            // Use DryIoc instead of the built-in service provider.
            _ = builder.UseServiceProviderFactory(new DryIocServiceProviderFactory(new Container()));

            // Add the WinUI User Interface hosted service as early as possible to allow the UI to start showing up
            // while you continue setting up other services not required for the UI.
            builder.Properties.Add(
                DroidNet.Hosting.WinUI.HostingExtensions.HostingContextKey,
                new HostingContext() { IsLifetimeLinked = true });

            var host = builder
                .ConfigureLogging()
                .ConfigureWinUI<App>()
                .ConfigureAppConfiguration(
                    (_, config) =>
                    {
                        var localSettingsPath = Path.GetFullPath("LocalSettings.json", finder.LocalAppData);
                        var projectBrowserConfigPath = Path.GetFullPath(
                            $"{Assembly.GetAssembly(typeof(ProjectBrowserSettings))!.GetName().Name}/Config/ProjectBrowser.config.json",
                            finder.ProgramData);
                        config
                            .AddJsonFile(localSettingsPath, optional: true)
                            .AddJsonFile(projectBrowserConfigPath);
                    })
                .ConfigureServices(
                    (context, sc) =>
                    {
                        // Injected access to LocalSettings configuration
                        _ = sc.Configure<ProjectBrowserSettings>(
                            context.Configuration.GetSection(ProjectBrowserSettings.ConfigSectionName));
                        _ = sc.ConfigureWritable<ThemeSettings>(
                            context.Configuration.GetSection(nameof(ThemeSettings)),
                            Path.Combine(finder.LocalAppData, "LocalSettings.json"));

                        _ = sc.AddSingleton<IFileSystem, RealFileSystem>()
                            .AddSingleton<NativeStorageProvider>()
                            .AddSingleton<IPathFinder, DevelopmentPathFinder>()
                            .AddSingleton<IThemeSelectorService, ThemeSelectorService>()
                            .AddSingleton<INavigationService, NavigationService>()
                            .AddSingleton<IAppNotificationService, AppNotificationService>()
                            .AddSingleton<IPageService, PageService>()
                            .AddSingleton<IActivationService, ActivationService>();

                        /* View related services */
                        _ = sc.AddSingleton<IKnownLocationsService, KnownLocationsService>()
                            .AddSingleton<LocalTemplatesSource>()
                            .AddSingleton<ITemplatesSource, TemplatesSource>()
                            .AddSingleton<ITemplatesService, TemplatesService>()
                            .AddSingleton<LocalProjectsSource>()
                            .AddSingleton<IProjectSource, UniversalProjectSource>()
                            .AddSingleton<IProjectsService, ProjectsService>();

                        /* Views and ViewModels */
                        _ = sc.AddTransient<ShellViewModel>()
                            .AddTransient<SettingsViewModel>()
                            .AddSingleton<StartHomeViewModel>()
                            .AddTransient<StartNewViewModel>()
                            .AddSingleton<StartOpenViewModel>()
                            .AddSingleton<StartViewModel>()
                            .AddTransient<MainViewModel>()
                            .AddTransient<SettingsPage>()
                            .AddTransient<ShellPage>()
                            .AddTransient<MainPage>();

                        // Persistent state DB context
                        var localStateFolder = finder.LocalAppState;
                        var dbPath = Path.Combine(localStateFolder, "state.db");
                        _ = sc.AddSqlite<PersistentState>($"Data Source={dbPath}; Mode=ReadWriteCreate");
                    })
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
}
