// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;
using DroidNet.Bootstrap;
using DroidNet.Config;
using DroidNet.Controls;
using DroidNet.Hosting.WinUI;
using DryIoc;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Oxygen.Editor.Data;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Services;
using Serilog;
using Serilog.Events;

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
        WinRT.ComWrappersSupport.InitializeComWrappers();
        DynamicTreeXamlSettings.EnableOptimizations();

        var bootstrap = new Bootstrapper(args);
        try
        {
            // Configure aned Build the host so the final container is available.
            _ = bootstrap
                .Configure(options => options
                    .WithOutputConsole()

                    // The engine's logging verbosity, which the user sets, alone decides which engine messages show.
                    .WithMinimumLevel(EngineConstants.LogCategory, LogEventLevel.Verbose))
                .WithMvvm()
                .WithRouting(EditorModule.CreateRoutes())
                .WithWinUI<App>()
                .Build();

            // Final container is now available.
            // A failure here aborts startup: the editor never runs with a half-configured container.
            var container = bootstrap.Container;
            EditorModule.Register(container);
            InitializeSettings(container);

            var dbPath = container.Resolve<IOxygenPathFinder>().StateDatabasePath;
            Log.Information("DB path: {DbPath}", dbPath);

            // Apply pending migrations to ensure database schema is up to date
            using (var persistentState = container.Resolve<PersistentState>())
            {
                persistentState.Database.MigrateAsync().GetAwaiter().GetResult();
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

    private static void InitializeSettings(IContainer container)
    {
        // The editor module registered the `Config` module, which added a SettingsManager.
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
}
