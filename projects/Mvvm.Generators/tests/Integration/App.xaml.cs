// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm.Generators;

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.DependencyInjection;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators.Demo;
using DroidNet.Mvvm.Generators.ViewModels;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
#if MSTEST_RUNNER
using Microsoft.Testing.Platform.Builder;
using Microsoft.VisualStudio.TestTools.UnitTesting;
#endif
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.VisualStudio.TestTools.UnitTesting.AppContainer;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

/// <summary>
/// Provides application-specific behavior to supplement the default Application
/// class.
/// </summary>
[ExcludeFromCodeCoverage]
public partial class App
{
    // private Window? window;

    /// <summary>
    /// Initializes a new instance of the <see cref="App" /> class.
    /// </summary>
    public App() => this.InitializeComponent();

    /// <summary>
    /// Invoked when the application is launched normally by the end user.  Other entry
    /// points
    /// will be used such as when the application is launched to open a specific file.
    /// </summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        var host = Host.CreateDefaultBuilder()
            .ConfigureServices(
                (_, services) => services.AddTransient<DemoViewModel>()
                    .AddTransient<IViewFor<DemoViewModel>, DemoView>())
            .Build();

        Ioc.Default.ConfigureServices(host.Services);

        Microsoft.VisualStudio.TestPlatform.TestExecutor.UnitTestClient.CreateDefaultUI();

        try
        {
            UITestMethodAttribute.DispatcherQueue = DispatcherQueue.GetForCurrentThread();

#if MSTEST_RUNNER
            /*
             * Ideally we would want to reuse the generated main, so we don't have to manually handle all dependencies
             * but this type is generated too late in the build process, so we fail before. You can build, inspect the
             * generated type to copy its content if you want.
             */

            var cliArgs = Environment.GetCommandLineArgs().Skip(1).ToArray();
            var builder = await TestApplication.CreateBuilderAsync(cliArgs).ConfigureAwait(true);
            Microsoft.Testing.Platform.MSBuild.TestingPlatformBuilderHook.AddExtensions(builder, cliArgs);
            Microsoft.Testing.Extensions.Telemetry.TestingPlatformBuilderHook.AddExtensions(builder, cliArgs);
            TestingPlatformBuilderHook.AddExtensions(builder, cliArgs);
            using var app = await builder.BuildAsync().ConfigureAwait(true);
            await app.RunAsync().ConfigureAwait(true);
#else
            // Create a main window for UI tests that use controls.
            /*
            this.window = new MainWindow();
            this.window.Activate();
            */

            // Replace back with e.Arguments when https://github.com/microsoft/microsoft-ui-xaml/issues/3368 is fixed
            Microsoft.VisualStudio.TestPlatform.TestExecutor.UnitTestClient.Run(Environment.CommandLine);

            await Task.CompletedTask.ConfigureAwait(false);
#endif
        }
        finally
        {
            // this.window?.Close();
        }
    }
}
