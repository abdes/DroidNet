// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor;

using System.Diagnostics;
using System.IO.Abstractions;
using System.Reflection;
using System.Runtime.InteropServices;
using CommunityToolkit.Mvvm.DependencyInjection;
using DroidNet.Config;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
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
using Testably.Abstractions;
using UnhandledExceptionEventArgs = System.UnhandledExceptionEventArgs;

/// <summary>The Main entry of the application.</summary>
/// Overrides the usual WinUI XAML entry point in order to be able to control
/// the `HostBuilder` for the `EntityFramework` to work properly and generate
/// migrations for the project.
public static partial class Program
{
    /// <summary>
    /// Ensures that the process can run XAML, and provides a deterministic error if a
    /// check fails. Otherwise, it quietly does nothing.
    /// </summary>
    [LibraryImport("Microsoft.ui.xaml.dll")]
    private static partial void XamlCheckProcessRequirements();

    private static HostApplicationBuilder CreateBuilder(string[] args)
    {
        var builder = new HostApplicationBuilder(args);

        var finder = new DevelopmentPathFinder(new RealFileSystem());

        _ = builder.Configuration
            /* Additional configuration sources */
            .SetBasePath(finder.LocalAppData)
            .AddJsonFile("LocalSettings.json", true)
            .SetBasePath(finder.ProgramData)
            .AddJsonFile(
                $"{Assembly.GetAssembly(typeof(ProjectBrowserSettings))!.GetName().Name}/Config/ProjectBrowser.config.json");

        _ = builder.Services
            /* Core services */
            .AddSingleton<IFileSystem, RealFileSystem>()
            .AddSingleton<NativeStorageProvider>()
            .AddSingleton<IPathFinder, DevelopmentPathFinder>()
            .AddSingleton<IThemeSelectorService, ThemeSelectorService>()
            .AddSingleton<INavigationService, NavigationService>()
            .AddSingleton<IAppNotificationService, AppNotificationService>()
            .AddSingleton<IPageService, PageService>()
            .AddSingleton<IActivationService, ActivationService>()

            // Injected access to LocalSettings configuration
            .Configure<ProjectBrowserSettings>(
                builder.Configuration.GetSection(ProjectBrowserSettings.ConfigSectionName))
            .ConfigureWritable<ThemeSettings>(
                builder.Configuration.GetSection(nameof(ThemeSettings)),
                Path.Combine(finder.LocalAppData, "LocalSettings.json"));

        _ = builder.Services
            /* View related services */
            .AddSingleton<IKnownLocationsService, KnownLocationsService>()
            .AddSingleton<LocalTemplatesSource>()
            .AddSingleton<ITemplatesSource, TemplatesSource>()
            .AddSingleton<ITemplatesService, TemplatesService>()
            .AddSingleton<LocalProjectsSource>()
            .AddSingleton<IProjectSource, UniversalProjectSource>()
            .AddSingleton<IProjectsService, ProjectsService>();

        _ = builder.Services
            /* Views and ViewModels */
            .AddTransient<ShellViewModel>()
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
        _ = builder.Services
            /* Use Sqlite */
            .AddSqlite<PersistentState>($"Data Source={dbPath}; Mode=ReadWriteCreate");

        return builder;
    }

    [STAThread]
    private static void Main(string[] args)
    {
        var builder = CreateBuilder(args);

        var host = builder.Build();

        using (var scope = host.Services.CreateScope())
        {
            var db = scope.ServiceProvider.GetRequiredService<PersistentState>();
            db.Database.Migrate();
        }

        Ioc.Default.ConfigureServices(host.Services);

        host.StartAsync()
            .GetAwaiter()
            .GetResult();

        XamlCheckProcessRequirements();
        WinRT.ComWrappersSupport.InitializeComWrappers();

        Application.Start(
            _ =>
            {
                try
                {
                    var context = new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread());
                    SynchronizationContext.SetSynchronizationContext(context);
                    var app = new App();

                    app.UnhandledException += OnAppUnhandledException;
                    AppDomain.CurrentDomain.UnhandledException += OnCurrentDomainUnhandledException;
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"Error application start callback: {ex.Message}.");
                }
            });
    }

    private static void OnAppUnhandledException(object sender, Microsoft.UI.Xaml.UnhandledExceptionEventArgs e)
        => OnUnhandledException(e.Exception, true);

    private static void OnCurrentDomainUnhandledException(object sender, UnhandledExceptionEventArgs e)
        => OnUnhandledException((e.ExceptionObject as Exception)!, true);

    private static void OnUnhandledException(Exception ex, bool shouldShowNotification)
    {
        /*
         * TODO: Log and handle exceptions as appropriate.
         * https://docs.microsoft.com/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.application.unhandledexception.
         */

        var stackTrace = ex.StackTrace is not null ? $"\n--- STACKTRACE ---\n{ex.StackTrace}" : string.Empty;
        var source = ex.Source is not null ? $"\n--- SOURCE ---\n{ex.Source}" : string.Empty;
        var innerException = ex.InnerException is not null ? $"\n--- INNER ---\n{ex.InnerException}" : string.Empty;
        var message = $"""
                       --------- UNHANDLED EXCEPTION ---------
                       >>>> HRESULT: {ex.HResult}
                       --- MESSAGE ---
                       {ex.Message}{stackTrace}{source}{innerException}
                       ---------------------------------------
                       """;
        Debug.WriteLine(message);

        //App.Logger.LogError(ex, ex.Message);

        if ( /*!ShowErrorNotification ||*/!shouldShowNotification)
        {
            return;
        }

        /*
        var toastContent = new ToastContent()
        {
            Visual = new()
            {
                BindingGeneric = new ToastBindingGeneric()
                {
                    Children =
                    {
                        new AdaptiveText()
                        {
                            Text = "ExceptionNotificationHeader".GetLocalizedResource()
                        },
                        new AdaptiveText()
                        {
                            Text = "ExceptionNotificationBody".GetLocalizedResource()
                        }
                    },
                    AppLogoOverride = new()
                    {
                        Source = "ms-appx:///Assets/error.png"
                    }
                }
            },
            Actions = new ToastActionsCustom()
            {
                Buttons =
                {
                    new ToastButton("ExceptionNotificationReportButton".GetLocalizedResource(), Constants.GitHub.BugReportUrl)
                    {
                        ActivationType = ToastActivationType.Protocol
                    }
                }
            },
            ActivationType = ToastActivationType.Protocol
        };

        var toastNotif = new ToastNotification(toastContent.GetXml());

        // And send the notification
        ToastNotificationManager.CreateToastNotifier().Show(toastNotif);
        */

        _ = Ioc.Default.GetService<IAppNotificationService>()
            ?.Show(
                $"""
                         <toast>
                             <visual>
                                 <binding template="ToastGeneric">
                                     <text>Unhandled Exception</text>
                                     <text>{ex.Message}</text>
                                 </binding>
                             </visual>
                         </toast>
                 """);

        Process.GetCurrentProcess()
            .Kill();
    }
}
