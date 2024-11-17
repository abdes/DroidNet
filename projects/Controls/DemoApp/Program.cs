// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;
using DroidNet.Bootstrap;
using DroidNet.Controls.Demo.DemoBrowser;
using DroidNet.Controls.Demo.DynamicTree;
using DroidNet.Hosting;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DryIoc;
using Microsoft.Extensions.Hosting;
using Microsoft.UI.Xaml;
using Serilog;

#if !DISABLE_XAML_GENERATED_MAIN
#error "This project only works with custom Main entry point. Must set DISABLE_XAML_GENERATED_MAIN to True."
#endif

namespace DroidNet.Controls.Demo;

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
/// <see cref="IHostApplicationBuilder.Properties" /> of the host builder. Currently, the
/// <see cref="BaseHostingContext.IsLifetimeLinked" /> property allows to specify if the User Interface thread lifetime is linked
/// to the application lifetime or not. When the two lifetimes are linked, terminating either of them will result in terminating
/// the other.
/// </para>
/// </remarks>
[ExcludeFromCodeCoverage]
public static partial class Program
{
    private static void ConfigureApplicationServices(this IContainer container)
    {
        // The Main Window is a singleton and its content can be re-assigned as needed. It is registered with a key that
        // corresponding to name of the special target <see cref="Target.Main" />.
        container.Register<Window, MainWindow>(Reuse.Singleton, serviceKey: Target.Main);

        container.Register<DemoBrowserView>(Reuse.Singleton);
        container.Register<DemoBrowserViewModel>(Reuse.Singleton);
        container.Register<ProjectLayoutView>(Reuse.Singleton);
        container.Register<ProjectLayoutViewModel>(Reuse.Singleton);
    }

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
                .WithConfiguration((_, _, _) => [], configureOptionsPattern: null)
                .WithLoggingAbstraction()
                .WithMvvm()
                .WithRouting(MakeRoutes())
                .WithWinUI<App>()
                .WithAppServices(ConfigureApplicationServices);

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
            Log.CloseAndFlush();
            bootstrap.Dispose();
        }
    }

    private static Routes MakeRoutes() => new(
    [
        new Route
        {
            Path = string.Empty,
            MatchMethod = PathMatch.Prefix,
            ViewModelType = typeof(DemoBrowserViewModel),
            Children = new Routes(
            [
                new Route()
                {
                    // The project browser is the root of a navigation view with multiple pages.
                    Path = "dynamic-tree",
                    ViewModelType = typeof(ProjectLayoutViewModel),
                },
            ]),
        },
    ]);
}
