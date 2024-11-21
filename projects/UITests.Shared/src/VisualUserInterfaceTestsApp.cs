// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.VisualStudio.TestTools.UnitTesting.AppContainer;
using Windows.Graphics;

namespace DroidNet.Tests;

#if MSTEST_RUNNER
using Microsoft.Testing.Platform.Builder;
using Microsoft.VisualStudio.TestTools.UnitTesting;
#endif

/// <summary>
/// Shared base Application class for all Visual User Interface tests.
/// Properly initializes the test framework, with our without MSTest runner.
/// </summary>
[ExcludeFromCodeCoverage]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "The Application class must be public")]
public abstract class VisualUserInterfaceTestsApp : Application
{
    private Window? window;

    /// <summary>
    /// Gets the main window of the application.
    /// </summary>
    public static Window MainWindow => ((VisualUserInterfaceTestsApp)Current).window!;

    /// <summary>
    /// Gets or sets the content root of the main window.
    /// </summary>
    /// <remarks>
    /// In order to ensure that the content is fully realized, use the
    /// <see cref="VisualUserInterfaceTests.LoadTestContentAsync(FrameworkElement)"/> method.
    /// </remarks>
    public static FrameworkElement? ContentRoot
    {
        get => MainWindow.Content as FrameworkElement;
        set => MainWindow.Content = value;
    }

    /// <summary>
    /// Gets the DispatcherQueue for the main window.
    /// </summary>
    public static DispatcherQueue DispatcherQueue => MainWindow.DispatcherQueue;

    /// <summary>
    /// Invoked when the application is launched.
    /// </summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override
#if MSTEST_RUNNER
    async
#endif
    void OnLaunched(LaunchActivatedEventArgs args)
    {
#if !MSTEST_RUNNER
        Microsoft.VisualStudio.TestPlatform.TestExecutor.UnitTestClient.CreateDefaultUI();
#endif
        this.window = new MainWindow();
        this.window.AppWindow.Resize(new SizeInt32(800, 600));
        this.window.Activate();

        UITestMethodAttribute.DispatcherQueue = DispatcherQueue.GetForCurrentThread();

        // Replace back with e.Arguments when https://github.com/microsoft/microsoft-ui-xaml/issues/3368 is fixed
#if MSTEST_RUNNER
        // Ideally we would want to reuse the generated main so we don't have to manually handle all dependencies
        // but this type is generated too late in the build process so we fail before.
        // You can build, inspect the generated type to copy its content if you want.
        // await TestingPlatformEntryPoint.Main(Environment.GetCommandLineArgs().Skip(1).ToArray());
        try
        {
            var cliArgs = Environment.GetCommandLineArgs().Skip(1).ToArray();
            var builder = await TestApplication.CreateBuilderAsync(cliArgs).ConfigureAwait(false);
            Microsoft.Testing.Platform.MSBuild.TestingPlatformBuilderHook.AddExtensions(builder, cliArgs);
            Microsoft.Testing.Extensions.Telemetry.TestingPlatformBuilderHook.AddExtensions(builder, cliArgs);
            TestingPlatformBuilderHook.AddExtensions(builder, cliArgs);
            using var app = await builder.BuildAsync().ConfigureAwait(false);
            await app.RunAsync().ConfigureAwait(false);
        }
        finally
        {
            this.window.Close();
        }
#else
        Microsoft.VisualStudio.TestPlatform.TestExecutor.UnitTestClient.Run(Environment.CommandLine);
#endif
    }
}
